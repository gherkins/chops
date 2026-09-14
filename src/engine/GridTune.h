#pragma once

// Grid tuner: how far is a signal's spectrum from the 12-tone equal-tempered
// grid? Every spectral peak has a deviation from its nearest semitone in
// cents; material that is in tune with itself (any chord, any loop) shares
// one deviation across all its notes and harmonics, so a magnitude-weighted
// circular mean of those deviations gives the offset the whole output sits
// at, plus a confidence (1 == every peak agrees, ~0 == inharmonic or noise).
// Deliberately JUCE-free and allocation-free, like Playback.h, so it can be
// unit-tested standalone and run on any thread.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace chops::tune
{

constexpr int kWindow = 8192;                 // power of two
constexpr int kBins = kWindow / 2 + 1;

struct Reading
{
    float offsetCents = 0.0f;     // (-50, 50]: how far the output is off-grid
    float confidence = 0.0f;      // |resultant| / weight sum
    int pitchClass = -1;          // 0 == C .. 11 == B, -1 == none
    int numPeaks = 0;

    // Resultant vector on the 100-cent circle, normalised by weight, so
    // callers can smooth readings across windows before deciding.
    float re = 0.0f;
    float im = 0.0f;
};

// Caller-owned scratch: ~100 KB, heap-allocate it once.
struct Scratch
{
    float re[kWindow];
    float im[kWindow];
    float db[kBins];
};

namespace detail
{
    constexpr double kTwoPi = 6.283185307179586;

    // In-place iterative radix-2 complex FFT. n must be a power of two.
    inline void fft (float* re, float* im, int n) noexcept
    {
        for (int i = 1, j = 0; i < n; ++i)
        {
            int bit = n >> 1;
            for (; (j & bit) != 0; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
            {
                const float tr = re[i]; re[i] = re[j]; re[j] = tr;
                const float ti = im[i]; im[i] = im[j]; im[j] = ti;
            }
        }

        for (int len = 2; len <= n; len <<= 1)
        {
            const double ang = -kTwoPi / (double) len;
            const double wr = std::cos (ang), wi = std::sin (ang);
            const int half = len / 2;
            for (int i = 0; i < n; i += len)
            {
                double cr = 1.0, ci = 0.0;
                for (int j = 0; j < half; ++j)
                {
                    const int a = i + j, b = a + half;
                    const float tr = (float) (re[b] * cr - im[b] * ci);
                    const float ti = (float) (re[b] * ci + im[b] * cr);
                    re[b] = re[a] - tr;
                    im[b] = im[a] - ti;
                    re[a] += tr;
                    im[a] += ti;
                    const double ncr = cr * wr - ci * wi;
                    ci = cr * wi + ci * wr;
                    cr = ncr;
                }
            }
        }
    }

    inline float wrapCents (double cents) noexcept
    {
        while (cents > 50.0) cents -= 100.0;
        while (cents <= -50.0) cents += 100.0;
        return (float) cents;
    }
}

// Analyse the newest kWindow frames of x (n >= kWindow, mono). Peaks are
// local maxima within [fMin, fMax] Hz that sit above -50 dB of the strongest
// peak and above an absolute -80 dBFS floor; each is refined by parabolic
// interpolation on the log magnitude and weighted by dB above that floor.
// The note name is the pitch class of the lowest strong peak (within 12 dB
// of the strongest) after the offset is removed: the bass note you land on.
inline Reading analyse (const float* x, int n, double sampleRate, Scratch& s,
                        double fMin = 60.0, double fMax = 5000.0) noexcept
{
    Reading r;
    if (x == nullptr || n < kWindow || sampleRate <= 0.0)
        return r;

    const float* w = x + (n - kWindow);
    for (int i = 0; i < kWindow; ++i)
    {
        const float hann = 0.5f - 0.5f * (float) std::cos (detail::kTwoPi * i / kWindow);
        s.re[i] = w[i] * hann;
        s.im[i] = 0.0f;
    }
    detail::fft (s.re, s.im, kWindow);

    // dBFS: a full-scale sine under a Hann window peaks at N/4.
    const double fullScale = (double) kWindow / 4.0;
    for (int k = 0; k < kBins; ++k)
    {
        const double mag = std::sqrt ((double) s.re[k] * s.re[k] + (double) s.im[k] * s.im[k]);
        s.db[k] = (float) (20.0 * std::log10 (mag / fullScale + 1.0e-12));
    }

    const double binHz = sampleRate / (double) kWindow;
    const int kLo = std::max (2, (int) std::ceil (fMin / binHz));
    const int kHi = std::min (kBins - 2, (int) std::floor (fMax / binHz));
    if (kHi <= kLo)
        return r;

    float maxDb = -200.0f;
    for (int k = kLo; k <= kHi; ++k)
        maxDb = std::max (maxDb, s.db[k]);

    const float floorDb = std::max (maxDb - 50.0f, -80.0f);
    if (maxDb <= floorDb)
        return r;

    // Pass 1: circular mean of every peak's deviation from the grid.
    double sumRe = 0.0, sumIm = 0.0, sumW = 0.0;
    int count = 0;
    double lowestStrongHz = 0.0;

    for (int k = kLo; k <= kHi; ++k)
    {
        const float a = s.db[k - 1], b = s.db[k], c = s.db[k + 1];
        if (b <= floorDb || b <= a || b < c)
            continue;

        const float denom = a - 2.0f * b + c;
        const float p = denom != 0.0f ? 0.5f * (a - c) / denom : 0.0f;
        const double hz = ((double) k + (double) p) * binHz;
        const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
        const double dev = midi - std::round (midi);          // [-0.5, 0.5)
        const double weight = (double) (b - floorDb);

        sumRe += weight * std::cos (detail::kTwoPi * dev);
        sumIm += weight * std::sin (detail::kTwoPi * dev);
        sumW += weight;
        ++count;

        if (b >= maxDb - 12.0f && (lowestStrongHz == 0.0 || hz < lowestStrongHz))
            lowestStrongHz = hz;
    }

    if (count == 0 || sumW <= 0.0)
        return r;

    r.numPeaks = count;
    r.re = (float) (sumRe / sumW);
    r.im = (float) (sumIm / sumW);
    r.confidence = std::sqrt (r.re * r.re + r.im * r.im);
    r.offsetCents = detail::wrapCents (100.0 * std::atan2 (sumIm, sumRe) / detail::kTwoPi);

    const double corrected = 69.0 + 12.0 * std::log2 (lowestStrongHz / 440.0)
                           - (double) r.offsetCents / 100.0;
    const int nearest = (int) std::lround (corrected);
    r.pitchClass = ((nearest % 12) + 12) % 12;
    return r;
}

} // namespace chops::tune
