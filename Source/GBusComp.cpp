#include "GBusComp.h"
#include "ratio_tables.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    inline double clampd (double v, double lo, double hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

    // Global seed source for the per-instance 4-lane noise generators.
    std::atomic<uint32_t> globalSeed { 0x2545F491u };   // ASSUMPTION: initial global seed value is unknown

    uint32_t nextGlobalSeed() noexcept
    {
        uint32_t g = globalSeed.load (std::memory_order_relaxed), n;
        do { n = g * 0x0B9C86E1u + 0x361D6EAFu; }
        while (! globalSeed.compare_exchange_weak (g, n, std::memory_order_relaxed));
        return n;
    }

    inline float laneToFloat (uint32_t bits) noexcept
    {
        // (bits >> 9 | 0x40000000) reinterpreted as float is in [2, 4); minus 3 -> [-1, 1)
        const uint32_t f = (bits >> 9) | 0x40000000u;
        float out;
        std::memcpy (&out, &f, sizeof (out));
        return out - 3.0f;
    }
}

//==============================================================================
void GBusComp::Rng4::seed() noexcept
{
    for (auto& v : s)
        v = nextGlobalSeed();
}

void GBusComp::Rng4::next (double out[4]) noexcept
{
    s[0] = s[0] * 0x0BB38535u + 0x3619630Fu;
    s[1] = s[1] * 0x0BB6F1F9u + 0x3617A2FFu;
    s[2] = s[2] * 0x0BB74709u + 0x3620FC21u;
    s[3] = s[3] * 0x0B9BA8A5u + 0x369D4845u;
    for (int i = 0; i < 4; ++i)
        out[i] = (double) laneToFloat (s[i]);
}

//==============================================================================
GBusComp::GBusComp()
{
    rngA.seed();
    table = kRatio4;
    setSampleRate (88200.0);
    reset();
}

const float* GBusComp::getRatioTable (int index) noexcept
{
    switch (index)
    {
        case 0:  return kRatio2;
        case 2:  return kRatio10;
        default: return kRatio4;
    }
}

double GBusComp::lookupTable (const float* T, double x) noexcept
{
    // f = (float) x * 510.99976f + 0.5f ; i = (float)(int) f ; clamp i to [-255, 255] ; fr = f - i
    // Outside the table, fr leaves [0, 1) and the end segment is extrapolated linearly (as the original).
    const float f = (float) x * TABLE_SCALE + 0.5f;
    float i = (float) (int) f;
    i = std::min (255.0f, std::max (-255.0f, i));
    const float fr = f - i;
    const int ii = (int) i;
    return (double) ((1.0f - fr) * T[255 + ii] + fr * T[256 + ii]);
}

//==============================================================================
// 2.2 setSampleRate(fs) - fs is the oversampled rate
//==============================================================================
void GBusComp::setSampleRate (double newFs)
{
    fs = newFs;
    T = 1.0 / fs;

    aCouple = 1.0 - std::exp (-2.0 * kPi * 1.1 * T);   // fixed sidechain coupling HPF
    aDc     = 1.0 - std::exp (-2.0 * kPi * 2.0 * T);   // I/O DC blockers
    setAnalogNoise (noiseOn);                           // noiseAmp = sqrt(fs/176400)*1e-7

    // 15.9 kHz "matched" one-pole for the control-voltage paths
    const double w  = 99902.64638415542 / fs;
    const double wc = std::min (w, kPi * 0.5);
    const double d  = 1.0 / (w * w);
    const double A  = 1.0 / (wc * wc * d + 1.0);
    const double B  = 1.0 / (d * kPi * kPi + 1.0);
    const double c  = std::cos (wc);                    // ASSUMPTION: the recovered call was opaque, assumed cos(wc)
    const double t  = (A - B) * c;
    const double s  = 2.0 * std::sqrt ((A - 1.0) * (A - B) * (c * c - 1.0));
    double p = ((2.0 * B - 2.0 * A) + 2.0 * t + s) / ((1.0 - 2.0 * A) + B + c - c * B);
    p = std::min (p, 1.0);
    const double q = std::sqrt (B * p * p * (p - 2.0) * (p - 2.0)) * (0.5 / (p * p)) + 0.5;
    lpA  = 1.0 - p;
    lpB0 = q * p;
    lpB1 = (1.0 - q) * p;                               // y = lpB0*x + lpB1*x[n-1] + lpA*y[n-1]

    meterG = 2.0 * std::tan (2.0 * kPi * 2.625 * T);    // ASSUMPTION: the meter's opaque call is tan

    updateCoefficients();
    snapSmoothers = true;
}

void GBusComp::setAnalogNoise (bool on) noexcept
{
    noiseOn = on;
    noiseAmp = on ? std::sqrt (fs / 176400.0) * 1e-7 : 0.0;
}

void GBusComp::setAttack (int index) noexcept
{
    index = std::min (kNumAttacks - 1, std::max (0, index));
    if (index != attackIdx)
    {
        attackIdx = index;
        updateCoefficients();
    }
}

void GBusComp::setRelease (int index) noexcept
{
    index = std::min (kNumReleases - 1, std::max (0, index));
    if (index != releaseIdx)
    {
        // Switching to a fixed release clears the auto branch.
        // ASSUMPTION: the spec's "paired state" is unidentified; only vR2 is cleared.
        if (index != kReleaseAuto)
            vR2 = 0.0;

        releaseIdx = index;
        updateCoefficients();
    }
}

void GBusComp::setRatio (int index) noexcept
{
    ratioIdx = std::min (kNumRatios - 1, std::max (0, index));
    table = getRatioTable (ratioIdx);
}

//==============================================================================
// 2.3 updateCoefficients() - on attack / release / sample-rate change
//==============================================================================
void GBusComp::updateCoefficients() noexcept
{
    const double Ra = ATTACK_R[attackIdx];
    vmax = ATTACK_VMAX[attackIdx];

    isAuto = (releaseIdx == kReleaseAuto);
    const double Rr = isAuto ? AUTO_R1 : RELEASE_R[releaseIdx];

    GC      = 2.0 * C_T * fs;                 // trapezoidal cap conductance
    ga      = std::min (GC, 1.0 / Ra);
    Gr      = 1.0 / Rr;
    G       = Gr + GC;
    invG    = 1.0 / G;
    invGsum = 1.0 / (G + ga);
    kdiv    = ga / (G + ga);
    sSeries = (1.0 - kdiv) * ga;              // = ga*G/(G+ga)
    kAuto   = isAuto ? (1.0 / C2) / fs : 0.0; // = 147058.82488414977/fs
    noiseRr = std::sqrt (Gr);

    if (! isAuto)
        vR2 = 0.0;
}

//==============================================================================
void GBusComp::reset()
{
    vD = vC = vR2 = iC = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        fbCV[ch] = fbX[ch] = scLp[ch] = cpLp[ch] = vcaY[ch] = vcaX[ch] = dcIn[ch] = dcOut[ch] = 0.0;

    grDb = grPeakHold = meterVel = meterPos = 0.0;
    snapSmoothers = true;
}

//==============================================================================
// 2.4 Per-block
//==============================================================================
void GBusComp::processBlock (double* left, double* right, const double* scLeft, const double* scRight, int n) noexcept
{
    const double aScHpTarget = 1.0 - std::exp (-2.0 * kPi * scHz * T);

    if (snapSmoothers || n < 1)
    {
        thr = thrTarget; mk = mkTarget; rng = rngTarget; mix = mixTarget; aScHp = aScHpTarget;
        thrInc = mkInc = rngInc = mixInc = aScHpInc = 0.0;
        snapSmoothers = false;
        if (n < 1)
            return;
    }
    else
    {
        const double invN = 1.0 / (double) n;
        thrInc   = (thrTarget - thr) * invN;
        mkInc    = (mkTarget - mk) * invN;
        rngInc   = (rngTarget - rng) * invN;
        mixInc   = (mixTarget - mix) * invN;
        aScHpInc = (aScHpTarget - aScHp) * invN;
    }

    for (int i = 0; i < n; ++i)
    {
        double x[2]  = { left[i], right[i] };
        const double sc[2] = { scLeft[i], scRight[i] };
        tick (x, sc);
        left[i]  = x[0];
        right[i] = x[1];
    }

    // land exactly on the targets (no drift from accumulated increments)
    thr = thrTarget; mk = mkTarget; rng = rngTarget; mix = mixTarget; aScHp = aScHpTarget;
}

//==============================================================================
// 2.5 Per-sample tick
//==============================================================================
void GBusComp::tick (double* x, const double* sc) noexcept
{
    // DC blocker states are zeroed while the blockers are disabled
    for (int ch = 0; ch < 2; ++ch)
    {
        dcIn[ch]  *= dcBlock;
        dcOut[ch] *= dcBlock;
    }

    // ---- noise ------------------------------------------------------------------
    double u[4], w[4];
    rngA.next (u);
    const double nTinyA = (u[0] + u[1]) * TINY_NOISE;
    const double nTinyB = (u[2] + u[3]) * TINY_NOISE;
    const double nAll   = (u[0] + u[1] + u[2] + u[3]) * noiseAmp;
    const double nRr    = nAll * noiseRr;
    const double nR2    = nAll * NOISE_R2;
    rngA.next (w);
    const double nC     = (w[0] + w[1]) * TINY_NOISE;
    const double nMain  = (w[2] + w[3]) * TINY_NOISE;

    // ---- advance smoothers ----------------------------------------------------------
    thr += thrInc; mk += mkInc; rng += rngInc; mix += mixInc; aScHp += aScHpInc;

    // ---- sidechain (per channel) ---------------------------------------------------
    double v[2];
    const double r = std::fabs (rng * 1.2 * INV_VCA) - 0.99;   // range soft floor position

    for (int ch = 0; ch < 2; ++ch)
    {
        // feedback sidechain VCA, driven by the smoothed control voltage of the previous sample
        const double gSc = std::pow (10.0, (VCA_DB_PER_V * fbCV[ch] - thr - SC_PAD_DB) / 20.0);

        const double xs = clampd (sc[ch] * scInGain, -20.0, 20.0) + nTinyA;
        scLp[ch] += (xs - scLp[ch]) * aScHp;                // user SC HPF
        const double h = xs - scLp[ch];

        const double y = gSc * h + nC;
        cpLp[ch] += (y - cpLp[ch]) * aCouple;               // 1.1 Hz coupling HPF
        const double dsc = y - cpLp[ch];

        // ratio / rectifier table (truncating cast, exactly as the original)
        v[ch] = lookupTable (table, dsc + (ch == 0 ? nTinyB : nTinyA));

        // range soft floor
        if (v[ch] < -r)
        {
            const double t = clampd (v[ch] + r, -2.0, 2.0);
            v[ch] = t * (1.0 - t * t * 0.25 + std::fabs (t) * t * t * 0.0625) - r;
        }
    }

    // ---- linked diode-OR peak detector (single node for both channels) ---------------
    const double vPred = (iC + nRr) * invG + vR2;

    if (vPred > v[0] || vPred > v[1])
    {
        const double vHist = (vR2 * G + iC + nRr) * invGsum;
        const double J = vHist * ga;

        for (int it = 0; it < 12; ++it)
        {
            const double vDprev = vD, vCprev = vC;
            double gd[2], I0[2];

            for (int ch = 0; ch < 2; ++ch)
            {
                const double xD = clampd (vD - v[ch], 0.0, vmax);
                const double e  = IS * std::exp (xD * INV_NVT);
                gd[ch] = e * INV_NVT;
                I0[ch] = (e - IS) - gd[ch] * xD;
            }

            vD = (gd[0] * v[0] + gd[1] * v[1] - (I0[0] + I0[1]) + J) / (gd[0] + gd[1] + sSeries);
            vC = vHist + kdiv * vD;

            // ASSUMPTION: the first convergence test lost its argument in the decompile;
            // assumed |dvD| < |vD|*1e-5 + 1e-7 (same form as the vC test).
            if (std::fabs (vD - vDprev) < std::fabs (vD) * 1e-5 + 1e-7
                && std::fabs (vC - vCprev) < std::fabs (vC) * 1e-5 + 1e-7)
                break;
        }
    }
    else
    {
        vD = vC = vPred;
    }

    const double cv = vC + nTinyA;
    const double vR2old = vR2;
    vR2 = ((((vC - vR2) * G - vR2 * INV_R2) + nRr - nR2) - iC) * kAuto + vR2;
    iC  = ((vC - vR2old) * GC - iC) + (vC - vR2) * GC;

    for (int ch = 0; ch < 2; ++ch)
    {
        fbCV[ch] = lpB1 * fbX[ch] + lpB0 * cv + lpA * fbCV[ch];
        fbX[ch] = cv;
    }

    // ---- main VCA ------------------------------------------------------------------------
    const double vcaIn = (VCA_DB_PER_V * vC + mk + nC) * compIn;

    for (int ch = 0; ch < 2; ++ch)
    {
        vcaY[ch] = lpB0 * vcaIn + lpB1 * vcaX[ch] + lpA * vcaY[ch];
        vcaX[ch] = vcaIn;
        const double g = std::pow (10.0, vcaY[ch] / 20.0);

        const double in = x[ch] + nMain;
        dcIn[ch] += dcBlock * aDc * (in - dcIn[ch]);
        const double xi = clampd (in - dcIn[ch], -20.0, 20.0);

        double yv = (g * mix + (1.0 - mix)) * xi + nC;
        dcOut[ch] += dcBlock * aDc * (yv - dcOut[ch]);
        yv -= dcOut[ch];

        // ---- peak clip ----------------------------------------------------------------
        const double dp = yv * CLIP_PRE;
        double yc;
        if (std::fabs (dp) > CLIP_KNEE)
        {
            const double t = clampd ((std::fabs (dp) - CLIP_KNEE) * CLIP_INV_W, -2.0, 2.0);
            yc = std::copysign (((std::fabs (t) * t * t * 0.0625 - t * t * 0.25 + 1.0) * t * CLIP_W + CLIP_KNEE) * CLIP_POST, dp);
        }
        else
        {
            yc = yv;
        }

        x[ch] = yc * peakClip + (1.0 - peakClip) * yv;
    }

    // ---- meter -------------------------------------------------------------------------
    grDb = std::max (0.0, -compIn * VCA_DB_PER_V * vC);
    grPeakHold = std::max (grPeakHold, grDb);

    const double v1 = ((grDb + nTinyB - meterPos) - meterVel * meterK) * meterG + meterVel;
    const double vel = clampd (meterG * v1, -20.0, 20.0);
    meterVel = v1;
    meterPos = clampd (meterPos + vel, 0.0, 20.0);
}
