#include "EchoDsp.hpp"

#include <algorithm>
#include <cmath>

namespace crown {
namespace dsp {

namespace {
constexpr float kPi = 3.14159265358979F;
constexpr int kClipLevel = kAdcFullScale - 16;
}  // namespace

Echo analyze(const std::int16_t* x, std::size_t n, const EchoParams& p, float* scratch) {
    Echo e{};
    if (n < 64) {
        return e;
    }
    float* env = scratch;
    float* sig = scratch + n;

    double mean = 0.0;
    for (std::size_t k = 0; k < n; ++k) {
        mean += x[k];
        if (x[k] >= kClipLevel || x[k] <= -kClipLevel) {
            e.clipped = true;
        }
    }
    mean /= static_cast<double>(n);
    for (std::size_t k = 0; k < n; ++k) {
        sig[k] = static_cast<float>(x[k] - mean);
    }

    // I/Q demodulation at f0 with a one-carrier-period running mean -> envelope amplitude.
    const std::size_t period = std::max<std::size_t>(4, static_cast<std::size_t>(std::lround(p.fs_hz / p.f0_hz)));
    const float w = 2.0F * kPi * p.f0_hz / p.fs_hz;
    double si = 0.0, sq = 0.0;
    float hist_i[128];
    float hist_q[128];
    const std::size_t hp = std::min<std::size_t>(period, 128);
    for (std::size_t k = 0; k < n; ++k) {
        const float ii = sig[k] * std::cos(w * static_cast<float>(k));
        const float qq = sig[k] * std::sin(w * static_cast<float>(k));
        si += ii;
        sq += qq;
        if (k >= hp) {
            si -= hist_i[k % hp];
            sq -= hist_q[k % hp];
        }
        hist_i[k % hp] = ii;
        hist_q[k % hp] = qq;
        const float m = static_cast<float>(std::min(k + 1, hp));
        // amplitude of a sinusoid = 2 x |mean(I + jQ)|; centre the boxcar (group delay hp / 2)
        const std::size_t c = (k >= hp / 2) ? k - hp / 2 : 0;
        env[c] = 2.0F * static_cast<float>(std::sqrt(si * si + sq * sq)) / m;
    }
    for (std::size_t k = n - hp / 2; k < n; ++k) {
        env[k] = env[n - hp / 2 - 1];
    }

    const auto idx = [&](float t) {
        const float i = (t - p.t0_s) * p.fs_hz;
        return static_cast<std::size_t>(std::clamp(i, 0.0F, static_cast<float>(n - 1)));
    };
    const std::size_t k_min = idx(p.min_tof_s);
    const std::size_t k_max = idx(p.max_tof_s);
    if (k_min < 16 || k_max <= k_min + period) {
        return e;
    }
    double nsum = 0.0;
    const std::size_t k_noise_end = k_min - k_min / 8;
    for (std::size_t k = 0; k < k_noise_end; ++k) {
        nsum += static_cast<double>(sig[k]) * sig[k];
    }
    e.noise_rms = static_cast<float>(std::sqrt(nsum / static_cast<double>(std::max<std::size_t>(1, k_noise_end))));

    std::size_t kp = k_min;
    for (std::size_t k = k_min; k <= k_max; ++k) {
        if (env[k] > env[kp]) {
            kp = k;
        }
    }
    e.peak = env[kp];
    e.peak_index = kp;
    const float noise_amp = std::max(1.0F, e.noise_rms * 1.41421356F);
    e.snr_db = 20.0F * std::log10(e.peak / noise_amp);
    if (e.snr_db < 12.0F) {
        return e;   // no usable echo (kPathNoEcho)
    }
    // Rising edge between 25 % and 75 % of the peak: least-squares line through it, evaluated at 50 %. Averaging the
    // whole edge instead of interpolating one crossing cuts the envelope-noise jitter roughly in half.
    const auto first_below = [&](float level) {
        std::size_t k = kp;
        while (k > k_min && env[k - 1] >= level) {
            --k;
        }
        return k;
    };
    const std::size_t k25 = first_below(0.25F * e.peak);
    const std::size_t k75 = first_below(0.75F * e.peak);
    if (k25 == k_min || k75 <= k25 + 2) {
        return e;   // the echo is already high at the window start: direct-path leak / ringing (kPathEarly)
    }
    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    const double m = static_cast<double>(k75 - k25 + 1);
    for (std::size_t k = k25; k <= k75; ++k) {
        const double xk = static_cast<double>(k - k25);
        sx += xk;
        sy += env[k];
        sxx += xk * xk;
        sxy += xk * env[k];
    }
    const double slope = (m * sxy - sx * sy) / (m * sxx - sx * sx);
    const double icpt = (sy - slope * sx) / m;
    if (slope <= 0.0) {
        return e;
    }
    const double k50 = static_cast<double>(k25) + (0.5 * e.peak - icpt) / slope;
    e.arrival_s = p.t0_s + static_cast<float>(k50 / p.fs_hz);
    e.ok = true;
    return e;
}

Reciprocal reciprocal_delay(const std::int16_t* a, const std::int16_t* b, std::size_t n, std::size_t start,
                            std::size_t len, float fs_hz, float f0_hz, float coarse_dt_s) {
    Reciprocal r{};
    const int period = std::max(4, static_cast<int>(std::lround(fs_hz / f0_hz)));
    const int centre = static_cast<int>(std::lround(coarse_dt_s * fs_hz));
    const int lo = centre - period / 2 - 1, hi = centre + period / 2 + 1;
    if (start + len > n) {
        len = n - start;
    }
    double ma = 0.0, mb = 0.0;
    for (std::size_t k = 0; k < n; ++k) {
        ma += a[k];
        mb += b[k];
    }
    ma /= static_cast<double>(n);
    mb /= static_cast<double>(n);

    auto corr = [&](int lag, double* ea, double* eb) {
        double s = 0.0, sa = 0.0, sb = 0.0;
        for (std::size_t k = start; k < start + len; ++k) {
            const long j = static_cast<long>(k) + lag;
            if (j < 0 || j >= static_cast<long>(n)) {
                continue;
            }
            const double va = a[k] - ma, vb = b[static_cast<std::size_t>(j)] - mb;
            s += va * vb;
            sa += va * va;
            sb += vb * vb;
        }
        if (ea) *ea = sa;
        if (eb) *eb = sb;
        return s;
    };
    int best = lo;
    double best_v = -1e300;
    for (int lag = lo; lag <= hi; ++lag) {
        const double v = corr(lag, nullptr, nullptr);
        if (v > best_v) {
            best_v = v;
            best = lag;
        }
    }
    double ea = 0.0, eb = 0.0;
    const double c0 = corr(best, &ea, &eb);
    const double cm = corr(best - 1, nullptr, nullptr);
    const double cp = corr(best + 1, nullptr, nullptr);
    const double den = cm - 2.0 * c0 + cp;
    const double delta = (den < 0.0) ? 0.5 * (cm - cp) / den : 0.0;
    r.dt_s = static_cast<float>((best + std::clamp(delta, -0.5, 0.5)) / fs_hz);
    r.coeff = (ea > 0.0 && eb > 0.0) ? static_cast<float>(c0 / std::sqrt(ea * eb)) : 0.0F;
    r.ok = r.coeff > 0.0F;
    return r;
}

PairTimes pair_times(const std::int16_t* ab, const std::int16_t* ba, std::size_t n, const EchoParams& p,
                     float delay_s, float* scratch, Echo* e_ab, Echo* e_ba) {
    PairTimes out{};
    const Echo ea = analyze(ab, n, p, scratch);
    const Echo eb = analyze(ba, n, p, scratch);
    if (e_ab) *e_ab = ea;
    if (e_ba) *e_ba = eb;
    if (!ea.ok || !eb.ok) {
        return out;
    }
    // correlate over the rising edge + first cycles of the echo, where both directions are least disturbed
    const float k50 = (ea.arrival_s - p.t0_s) * p.fs_hz;
    const std::size_t period = static_cast<std::size_t>(std::lround(p.fs_hz / p.f0_hz));
    const std::size_t start = static_cast<std::size_t>(std::max(0.0F, k50 - 2.0F * static_cast<float>(period)));
    const std::size_t len = 10 * period;
    const Reciprocal rc = reciprocal_delay(ab, ba, n, start, len, p.fs_hz, p.f0_hz, eb.arrival_s - ea.arrival_s);
    if (!rc.ok) {
        return out;
    }
    const float t_mean = 0.5F * (ea.arrival_s + eb.arrival_s) - delay_s;
    out.t_ab_s = t_mean - 0.5F * rc.dt_s;
    out.t_ba_s = t_mean + 0.5F * rc.dt_s;
    out.coeff = rc.coeff;
    out.ok = true;
    return out;
}

std::uint8_t next_gain(std::uint8_t gain, const Echo& e) {
    if (e.clipped || e.peak > 0.75F * kAdcFullScale) {
        return gain > 0 ? gain - 1 : 0;
    }
    if (e.peak < 0.25F * kAdcFullScale && gain < 4) {
        return gain + 1;
    }
    return gain;
}

}  // namespace dsp
}  // namespace crown
