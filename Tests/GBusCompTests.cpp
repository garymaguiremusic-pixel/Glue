// Console tests for GBusComp (no JUCE).
//
// Renders test signals through the DSP core to 32-bit float WAV files (input and output, so the
// same inputs can be run through a reference plugin and nulled), prints the static curves and
// runs a handful of behavioural checks.
//
// The core runs directly at kFs (= 2 x 44.1 kHz, the plugin's default 2x oversampled rate);
// no resampler is involved here.
//
// Usage: GBusCompTests [output-directory]

#include "../Source/GBusComp.h"
#include "../Source/ratio_tables.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    constexpr double kFs = 88200.0;
    constexpr double kPi = 3.14159265358979323846;
    int failures = 0;

    double dbToGain (double db) { return std::pow (10.0, db / 20.0); }
    double gainToDb (double g)  { return 20.0 * std::log10 (std::max (g, 1e-30)); }

    void check (bool ok, const char* what)
    {
        std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
        if (! ok) ++failures;
    }

    //==========================================================================
    struct Stereo
    {
        std::vector<double> l, r;
        explicit Stereo (size_t n = 0) : l (n, 0.0), r (n, 0.0) {}
        size_t size() const { return l.size(); }
    };

    void writeWav (const std::string& path, const Stereo& s, double fs)
    {
        FILE* f = std::fopen (path.c_str(), "wb");
        if (f == nullptr) { std::printf ("  could not write %s\n", path.c_str()); return; }

        const uint32_t n = (uint32_t) s.size(), channels = 2, bytesPerSample = 4;
        const uint32_t dataBytes = n * channels * bytesPerSample;
        auto u32 = [f] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
        auto u16 = [f] (uint16_t v) { std::fwrite (&v, 2, 1, f); };

        std::fwrite ("RIFF", 1, 4, f); u32 (36 + dataBytes); std::fwrite ("WAVE", 1, 4, f);
        std::fwrite ("fmt ", 1, 4, f); u32 (16); u16 (3 /* IEEE float */); u16 ((uint16_t) channels);
        u32 ((uint32_t) fs); u32 ((uint32_t) fs * channels * bytesPerSample);
        u16 ((uint16_t) (channels * bytesPerSample)); u16 (32);
        std::fwrite ("data", 1, 4, f); u32 (dataBytes);
        for (uint32_t i = 0; i < n; ++i)
        {
            const float frame[2] = { (float) s.l[i], (float) s.r[i] };
            std::fwrite (frame, 4, 2, f);
        }
        std::fclose (f);
    }

    struct Settings
    {
        double threshold = -20.0, makeup = 0.0, range = -106.0, mix = 1.0, scHpf = 0.0;
        int attack = 2, release = 3, ratio = 1;
        bool compIn = true, peakClip = false, noise = false;
    };

    void apply (GBusComp& c, const Settings& s)
    {
        c.setThreshold (s.threshold); c.setMakeup (s.makeup); c.setRange (s.range); c.setMix (s.mix);
        c.setScHpf (s.scHpf); c.setAttack (s.attack); c.setRelease (s.release); c.setRatio (s.ratio);
        c.setCompIn (s.compIn); c.setPeakClip (s.peakClip); c.setAnalogNoise (s.noise);
    }

    /** Processes in 64-sample blocks (sidechain = input); optionally records GR per sample. */
    Stereo process (GBusComp& c, const Stereo& in, std::vector<double>* grOut = nullptr)
    {
        Stereo out = in;
        constexpr int block = 64;
        for (size_t pos = 0; pos < in.size(); pos += block)
        {
            const int n = (int) std::min<size_t> (block, in.size() - pos);
            c.processBlock (&out.l[pos], &out.r[pos], &in.l[pos], &in.r[pos], n);
            if (grOut != nullptr)
                for (int i = 0; i < n; ++i) grOut->push_back (c.getGainReductionDb());
        }
        return out;
    }

    Stereo sine (double freq, double dbfs, double seconds)
    {
        Stereo s ((size_t) (seconds * kFs));
        const double a = dbToGain (dbfs);
        for (size_t i = 0; i < s.size(); ++i)
            s.l[i] = s.r[i] = a * std::sin (2.0 * kPi * freq * (double) i / kFs);
        return s;
    }

    double rms (const std::vector<double>& v, size_t from, size_t to)
    {
        double acc = 0.0;
        for (size_t i = from; i < to; ++i) acc += v[i] * v[i];
        return std::sqrt (acc / (double) std::max<size_t> (1, to - from));
    }

    double peak (const std::vector<double>& v, size_t from, size_t to)
    {
        double p = 0.0;
        for (size_t i = from; i < to; ++i) p = std::max (p, std::fabs (v[i]));
        return p;
    }

    //==========================================================================
    void testTableLookup()
    {
        std::printf ("\n== Ratio-table lookup ==\n");
        const char* names[] = { "2:1", "4:1", "10:1" };
        for (int r = 0; r < 3; ++r)
        {
            const float* T = GBusComp::getRatioTable (r);
            for (double x : { 0.0, 0.25, -0.25, 0.499, -0.499 })
            {
                const float f = (float) x * GBusComp::TABLE_SCALE + 0.5f;
                const int i = std::max (-255, std::min (255, (int) f));
                const float fr = f - (float) i;
                const double expected = (double) ((1.0f - fr) * T[255 + i] + fr * T[256 + i]);
                const double got = GBusComp::lookupTable (T, x);
                std::printf ("  %-4s x=%+.3f  idx=%3d fr=%+.4f  T[%d]=%+.6f T[%d]=%+.6f  -> %+.6f\n",
                             names[r], x, i, fr, 255 + i, T[255 + i], 256 + i, T[256 + i], got);
                if (got != expected) { std::printf ("  [FAIL] mismatch (expected %+.9f)\n", expected); ++failures; }
            }
        }
        check (std::memcmp (GBusComp::getRatioTable (0), kRatio2, sizeof (kRatio2)) == 0
               && std::memcmp (GBusComp::getRatioTable (1), kRatio4, sizeof (kRatio4)) == 0
               && std::memcmp (GBusComp::getRatioTable (2), kRatio10, sizeof (kRatio10)) == 0,
               "ratio index -> table mapping 0=2:1 1=4:1 2=10:1");
    }

    //==========================================================================
    void testSampleRates()
    {
        std::printf ("\n== Sample rates (1x..8x of 44.1/48/96 kHz): stability and level ==\n");
        bool ok = true;
        for (double base : { 44100.0, 48000.0, 96000.0 })
            for (int os : { 1, 2, 4, 8 })
            {
                const double rate = base * os;
                GBusComp c; c.setSampleRate (rate);
                Settings s; s.threshold = -20.0;
                apply (c, s); c.reset();
                Stereo in ((size_t) (rate * 0.5));
                for (size_t i = 0; i < in.size(); ++i)
                    in.l[i] = in.r[i] = 0.5 * std::sin (2.0 * kPi * 1000.0 * (double) i / rate);
                Stereo out = in;
                for (size_t pos = 0; pos < in.size(); pos += 256)
                {
                    const int n = (int) std::min<size_t> (256, in.size() - pos);
                    c.processBlock (&out.l[pos], &out.r[pos], &in.l[pos], &in.r[pos], n);
                }
                bool finite = true;
                for (double v : out.l) finite = finite && std::isfinite (v);
                const double gr = c.getGainReductionDb();
                std::printf ("  %7.0f Hz  GR %.2f dB  %s\n", rate, gr, finite ? "" : "NON-FINITE OUTPUT");
                ok = ok && finite && gr > 1.0 && gr < 20.0;
            }
        check (ok, "finite output and similar gain reduction at every rate");
    }

    //==========================================================================
    void testStaticCurves()
    {
        std::printf ("\n== Static curves (1 kHz sine, attack 0.01 ms, release 0.1 s, threshold -20 dB) ==\n");
        const char* names[] = { "2:1", "4:1", "10:1" };

        for (int r = 0; r < 3; ++r)
        {
            std::printf ("\n  Ratio %s\n  in dBFS   out dBFS   GR dB   slope\n", names[r]);
            double prevOut = 0.0;
            for (int level = -40; level <= 0; level += 2)
            {
                GBusComp c;
                c.setSampleRate (kFs);
                Settings s; s.attack = 0; s.release = 0; s.ratio = r;
                apply (c, s);
                c.reset();

                const auto in = sine (1000.0, (double) level, 1.5);
                std::vector<double> gr;
                const auto out = process (c, in, &gr);
                const size_t a = in.size() - (size_t) (0.2 * kFs), b = in.size();
                const double outDb = gainToDb (rms (out.l, a, b) * std::sqrt (2.0));   // peak-equivalent dBFS
                double grMean = 0.0;
                for (size_t i = a; i < b; ++i) grMean += gr[i];
                grMean /= (double) (b - a);

                if (level == -40) std::printf ("  %6d   %8.2f  %6.2f\n", level, outDb, grMean);
                else              std::printf ("  %6d   %8.2f  %6.2f   %5.3f\n", level, outDb, grMean, (outDb - prevOut) / 2.0);
                prevOut = outDb;
            }
        }
    }

    //==========================================================================
    void testRange()
    {
        std::printf ("\n== Range ==\n");
        for (double range : { -6.0, -106.0 })
        {
            GBusComp c; c.setSampleRate (kFs);
            Settings s; s.threshold = -40.0; s.ratio = 2; s.attack = 0; s.release = 0; s.range = range;
            apply (c, s); c.reset();
            std::vector<double> gr;
            process (c, sine (1000.0, 0.0, 1.0), &gr);
            double g = 0.0; for (size_t i = gr.size() - 4410; i < gr.size(); ++i) g += gr[i]; g /= 4410.0;
            std::printf ("  range %s: steady GR %.2f dB (thr -40, 10:1, 0 dBFS)\n", range < -80 ? "Full" : "-6 dB", g);
            if (range > -80) check (g > 4.0 && g < 9.0, "Range -6 dB caps gain reduction near 6 dB");
            else             check (g > 15.0, "Range Full is unlimited");
        }
    }

    //==========================================================================
    void testPeakClip()
    {
        std::printf ("\n== Peak clip ==\n");
        {
            GBusComp c; c.setSampleRate (kFs);
            Settings s; s.compIn = false; s.peakClip = true;
            apply (c, s); c.reset();
            const auto out = process (c, sine (1000.0, 6.0, 0.5));
            const double p = peak (out.l, out.size() / 2, out.size());
            std::printf ("  +6 dBFS sine in -> peak %.4f (%.2f dBFS)\n", p, gainToDb (p));
            check (p < 0.9441 && p > 0.93, "peak clip limits the output to about -0.5 dBFS");
        }
        {
            GBusComp c; c.setSampleRate (kFs);
            Settings s; s.compIn = false; s.peakClip = true;
            apply (c, s); c.reset();
            const auto in = sine (1000.0, -2.0, 0.5);
            const auto out = process (c, in);
            double maxDiff = 0.0;
            for (size_t i = 0; i < in.size(); ++i) maxDiff = std::max (maxDiff, std::fabs (out.l[i] - in.l[i]));
            std::printf ("  -2 dBFS sine: max |out - in| = %.3g\n", maxDiff);
            check (maxDiff < 1e-9, "peak clip is transparent below about -2 dBFS");
        }
    }

    //==========================================================================
    void testAutoRelease()
    {
        std::printf ("\n== Release recovery (GR after a 1 s, 0 dBFS burst; thr -30, 4:1) ==\n");
        std::printf ("  release      t=+0.02  +0.05   +0.1   +0.2   +0.5   +1.0   +2.0 s\n");
        const char* labels[] = { "0.1 s", "0.2 s", "0.4 s", "0.6 s", "0.8 s", "1.2 s", "Auto" };
        for (int rel = 0; rel < 7; ++rel)
        {
            GBusComp c; c.setSampleRate (kFs);
            Settings s; s.threshold = -30.0; s.attack = 3; s.release = rel;
            apply (c, s); c.reset();
            auto in = sine (1000.0, 0.0, 3.5);
            const size_t stop = (size_t) kFs;
            for (size_t i = stop; i < in.size(); ++i) in.l[i] = in.r[i] = 0.0;
            std::vector<double> gr;
            process (c, in, &gr);
            std::printf ("  %-8s   ", labels[rel]);
            for (double t : { 0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0 })
                std::printf ("%6.2f ", gr[stop + (size_t) (t * kFs)]);
            std::printf ("  (GR at end of burst %.2f)\n", gr[stop - 1]);
        }
        std::printf ("  Auto should drop fast at first and then settle slowly (a slow tail).\n");
    }

    //==========================================================================
    Stereo pinkNoise (double seconds, double dbfs)
    {
        Stereo s ((size_t) (seconds * kFs));
        uint32_t seed = 12345;
        auto white = [&seed] { seed = seed * 1664525u + 1013904223u; return (double) (seed >> 8) / 8388608.0 - 1.0; };
        double b[2][7] {};
        const double gain = dbToGain (dbfs) * 0.11;
        for (size_t i = 0; i < s.size(); ++i)
            for (int ch = 0; ch < 2; ++ch)
            {
                const double w = white();   // Paul Kellet pink filter
                auto& p = b[ch];
                p[0] = 0.99886 * p[0] + w * 0.0555179; p[1] = 0.99332 * p[1] + w * 0.0750759;
                p[2] = 0.96900 * p[2] + w * 0.1538520; p[3] = 0.86650 * p[3] + w * 0.3104856;
                p[4] = 0.55000 * p[4] + w * 0.5329522; p[5] = -0.7616 * p[5] - w * 0.0168980;
                const double pink = p[0] + p[1] + p[2] + p[3] + p[4] + p[5] + p[6] + w * 0.5362;
                p[6] = w * 0.115926;
                (ch == 0 ? s.l : s.r)[i] = pink * gain;
            }
        return s;
    }

    Stereo drumTrain (double seconds)
    {
        // Kick-like (pitch-dropping sine) on beats, snare-like (noise) on off-beats, 120 BPM.
        Stereo s ((size_t) (seconds * kFs));
        const size_t beat = (size_t) (0.5 * kFs);
        uint32_t seed = 777;
        for (size_t i = 0; i < s.size(); ++i)
        {
            const size_t k = i % beat, n = i / beat;
            const double t = (double) k / kFs;
            double v;
            if (n % 2 == 0)
            {
                const double f = 50.0 + 120.0 * std::exp (-t * 30.0);
                v = 0.9 * std::sin (2.0 * kPi * f * t) * std::exp (-t * 9.0);
            }
            else
            {
                seed = seed * 1664525u + 1013904223u;
                const double w = (double) (seed >> 8) / 8388608.0 - 1.0;
                v = 0.6 * w * std::exp (-t * 22.0) + 0.3 * std::sin (2.0 * kPi * 190.0 * t) * std::exp (-t * 15.0);
            }
            s.l[i] = s.r[i] = v;
        }
        return s;
    }

    void renderFiles (const std::string& dir)
    {
        std::printf ("\n== Rendering WAV files to %s (%.0f Hz, threshold -20, 4:1, attack 0.3 ms, release 0.6 s) ==\n",
                     dir.c_str(), kFs);
        auto render = [&dir] (const std::string& name, const Stereo& in)
        {
            GBusComp c; c.setSampleRate (kFs);
            Settings s;   // defaults: thr -20, 4:1, 0.3 ms, 0.6 s
            apply (c, s); c.reset();
            std::vector<double> gr;
            const auto out = process (c, in, &gr);
            writeWav (dir + "/" + name + "_in.wav", in, kFs);
            writeWav (dir + "/" + name + "_out.wav", out, kFs);
            double maxGr = 0.0; for (double g : gr) maxGr = std::max (maxGr, g);
            std::printf ("  %-22s max GR %.2f dB\n", name.c_str(), maxGr);
        };

        for (int level : { -30, -20, -10, 0 })
        {
            // 0.5 s bursts with 0.5 s gaps, 4 s
            auto in = sine (1000.0, (double) level, 4.0);
            for (size_t i = 0; i < in.size(); ++i)
                if (((size_t) (i / (0.5 * kFs))) % 2 == 1) in.l[i] = in.r[i] = 0.0;
            render ("sine_burst_" + std::to_string (-level) + "dB", in);
        }
        render ("pink_noise_-12dB", pinkNoise (6.0, -12.0));
        render ("drum_train", drumTrain (6.0));
    }
}

int main (int argc, char** argv)
{
    const std::string outDir = argc > 1 ? argv[1] : ".";

    testTableLookup();
    testSampleRates();
    testStaticCurves();
    testRange();
    testPeakClip();
    testAutoRelease();
    renderFiles (outDir);

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED",
                 failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
