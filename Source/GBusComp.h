#pragma once

#include <cstdint>

//==============================================================================
/**
    Stereo, fully linked VCA bus compressor (circuit model).

    Pure DSP: no JUCE dependency, double precision throughout, no allocation.
    Every sample is processed at the (oversampled) rate passed to setSampleRate().

    Signal flow per sample:
        sidechain -> SC HPF -> feedback SC VCA (-Threshold -18 dB) -> 1.1 Hz coupling HPF
        -> ratio/rectifier table -> Range soft floor -> diode-OR peak detector (Newton)
        -> control voltage vC -> 15.9 kHz smoothing -> main VCA (7.8 dB/V, + makeup)
        -> dry/wet -> optional DC blockers -> optional peak clip
*/
class GBusComp
{
public:
    //==========================================================================
    // Constants (spec section 2.1)
    static constexpr double VCA_DB_PER_V = 7.8;
    static constexpr double INV_VCA      = 0.12820512507010423;   // 1/7.8
    static constexpr double IS           = 6.813240194247783e-9;  // diode saturation current
    static constexpr double NVT          = 0.052;
    static constexpr double INV_NVT      = 19.23076883399276;
    static constexpr double C_T          = 0.47e-6;               // release/attack timing cap
    static constexpr double C2           = 6.8e-6;                // auto-release second branch
    static constexpr double R2           = 750000.0;
    static constexpr double INV_R2       = 1.3333333333333334e-6;
    static constexpr double SC_PAD_DB    = 18.0;
    static constexpr float  TABLE_SCALE  = 510.99976f;            // ratio-table index scale
    static constexpr double CLIP_PRE     = 1.0592537251772889;
    static constexpr double CLIP_POST    = 0.9440608762859234;
    static constexpr double CLIP_KNEE    = 0.841395141645195;
    static constexpr double CLIP_INV_W   = 6.304977100783147;
    static constexpr double CLIP_W       = 0.15860485835480498;
    static constexpr double AUTO_R1      = 91000.0;
    static constexpr double TINY_NOISE   = 7.071067811865475e-21; // denormal guard
    static constexpr double NOISE_R2     = 0.0011547005269676447; // sqrt(1/750k)

    static constexpr int kNumAttacks = 7, kNumReleases = 7, kNumRatios = 3;
    static constexpr int kReleaseAuto = 6;

    static constexpr double ATTACK_R[kNumAttacks]    = { 82.0, 820.0, 2700.0, 8200.0, 27000.0, 82000.0, 270000.0 };
    static constexpr double ATTACK_VMAX[kNumAttacks] = { 0.40361890481199225, 0.38866682663789476, 0.38866682663789476,
                                                         0.38866682663789476, 0.38866682663789476, 0.38866682663789476,
                                                         0.38866682663789476 };
    static constexpr double RELEASE_R[kNumReleases - 1] = { 170689.65625, 249579.84375, 340760.875,
                                                            478756.46875, 643902.4375, 880000.0 };

    //==========================================================================
    GBusComp();

    /** fs is the oversampled rate. Computes all rate-dependent coefficients. */
    void setSampleRate (double fs);
    /** Zeroes every state and snaps all smoothed parameters to their targets. */
    void reset();

    // Parameter targets (safe to call every block; smoothed per block where specified)
    void setThreshold (double dB) noexcept   { thrTarget = dB; }
    void setMakeup (double dB) noexcept      { mkTarget = dB; }
    /** Range in dB (negative). Values below -80 dB mean "Full" and map to -106 dB. */
    void setRange (double dB) noexcept       { rngTarget = dB < -80.0 ? -106.0 : dB; }
    void setMix (double mix01) noexcept      { mixTarget = mix01; }
    /** Sidechain HPF cutoff in Hz. Values below 20 Hz mean "Off" (2 Hz internally). */
    void setScHpf (double hz) noexcept       { scHz = hz < 20.0 ? 2.0 : hz; }
    void setAttack (int index) noexcept;
    void setRelease (int index) noexcept;
    void setRatio (int index) noexcept;
    void setCompIn (bool on) noexcept        { compIn = on ? 1.0 : 0.0; }
    void setPeakClip (bool on) noexcept      { peakClip = on ? 1.0 : 0.0; }
    void setDcBlock (bool on) noexcept       { dcBlock = on ? 1.0 : 0.0; }
    void setScInGain (double g) noexcept     { scInGain = g; }
    void setAnalogNoise (bool on) noexcept;

    /** Process one block in place. sc may alias x (each tick reads sc[i] before writing x[i]). */
    void processBlock (double* left, double* right, const double* scLeft, const double* scRight, int numSamples) noexcept;

    // Metering
    double getGainReductionDb() const noexcept { return grDb; }       // instantaneous, >= 0
    double getMeterNeedleDb() const noexcept   { return meterPos; }   // smoothed needle 0..20
    double getPeakHoldDb() const noexcept      { return grPeakHold; }
    void resetPeakHold() noexcept              { grPeakHold = 0.0; }

    /** Ratio-table lookup exactly as the original (truncating cast, linear extrapolation outside). */
    static double lookupTable (const float* table, double x) noexcept;
    static const float* getRatioTable (int index) noexcept;

private:
    struct Rng4
    {
        uint32_t s[4] {};
        void seed() noexcept;
        void next (double out[4]) noexcept;
    };

    void updateCoefficients() noexcept;
    void tick (double* x, const double* sc) noexcept;

    // Rate-dependent constants
    double fs = 88200.0, T = 1.0 / 88200.0;
    double aCouple = 0.0, aDc = 0.0, noiseAmp = 0.0;
    double lpA = 0.0, lpB0 = 1.0, lpB1 = 0.0;
    double meterG = 0.0;
    static constexpr double meterK = 1.5;
    bool noiseOn = true;

    // Detector coefficients
    int attackIdx = 2, releaseIdx = 3, ratioIdx = 1;
    bool isAuto = false;
    double vmax = ATTACK_VMAX[2];
    double GC = 0.0, ga = 0.0, Gr = 0.0, G = 0.0, invG = 0.0, invGsum = 0.0;
    double kdiv = 0.0, sSeries = 0.0, kAuto = 0.0, noiseRr = 0.0;
    const float* table = nullptr;

    // Switches
    double compIn = 1.0, peakClip = 0.0, dcBlock = 0.0, scInGain = 1.0;

    // Smoothed parameters (linear per-block ramps)
    double thrTarget = 0.0, mkTarget = 0.0, rngTarget = -106.0, mixTarget = 1.0, scHz = 2.0;
    double thr = 0.0, mk = 0.0, rng = -106.0, mix = 1.0, aScHp = 0.0;
    double thrInc = 0.0, mkInc = 0.0, rngInc = 0.0, mixInc = 0.0, aScHpInc = 0.0;
    bool snapSmoothers = true;

    // Detector state
    double vD = 0.0, vC = 0.0, vR2 = 0.0, iC = 0.0;

    // Per-channel filter states
    double fbCV[2] {}, fbX[2] {};
    double scLp[2] {}, cpLp[2] {};
    double vcaY[2] {}, vcaX[2] {};
    double dcIn[2] {}, dcOut[2] {};

    // Meter
    double grDb = 0.0, grPeakHold = 0.0, meterVel = 0.0, meterPos = 0.0;

    Rng4 rngA;
};
