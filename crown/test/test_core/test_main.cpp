// Host tests of the crown DSP / protocol core with a simulated 40 kHz transducer link.
#include <unity.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "EchoDsp.hpp"
#include "RainDetector.hpp"
#include "RegisterBank.hpp"
#include "crown_protocol.hpp"

using namespace crown;

namespace {
constexpr float kPi = 3.14159265358979F;
constexpr float kFs = 2.0e6F;
constexpr std::size_t kN = 2048;   // 1.024 ms window
constexpr float kT0 = 50e-6F;      // first sample 50 us after the drive edge (drive ringing skipped)

struct Rng {
    std::uint32_t s;
    float uniform() {
        s = s * 1664525U + 1013904223U;
        return static_cast<float>(s >> 8) / 16777216.0F;
    }
    float gauss() {  // Irwin-Hall approximation, unit variance
        float a = 0.0F;
        for (int i = 0; i < 12; ++i) a += uniform();
        return a - 6.0F;
    }
};

// Closed-face transducer receiving an 8-cycle burst: envelope rises with tau while driven, then rings down.
std::vector<std::int16_t> echo(float t_arr, float amp, float noise_rms, std::uint32_t seed, float dc = 37.0F,
                               float phase = 0.0F) {
    std::vector<std::int16_t> x(kN);
    Rng rng{seed};
    const float tau = 120e-6F, td = 8.0F / 40e3F;
    for (std::size_t k = 0; k < kN; ++k) {
        const float u = kT0 + static_cast<float>(k) / kFs - t_arr;
        float env = 0.0F;
        if (u > 0.0F) {
            env = (u < td) ? 1.0F - std::exp(-u / tau) : (1.0F - std::exp(-td / tau)) * std::exp(-(u - td) / tau);
        }
        float v = amp * env * std::sin(2.0F * kPi * 40e3F * u + phase) + dc + noise_rms * rng.gauss();
        v = std::fmax(-2047.0F, std::fmin(2047.0F, std::round(v)));
        x[k] = static_cast<std::int16_t>(v);
    }
    return x;
}

dsp::EchoParams params() {
    dsp::EchoParams p{};
    p.fs_hz = kFs;
    p.t0_s = kT0;
    p.min_tof_s = 200e-6F;
    p.max_tof_s = 900e-6F;
    return p;
}

std::vector<float> scratch(2 * kN);
}  // namespace

void test_crc8_matches_sensirion_vector(void) {
    const std::uint8_t data[2] = {0xBE, 0xEF};
    TEST_ASSERT_EQUAL_HEX8(0x92, crc8(data, 2));
}

void test_arrival_is_gain_invariant(void) {
    const auto p = params();
    float first = 0.0F;
    // AGC keeps the envelope peak at 25..75 % of full scale; inside that range the 50 % point does not move
    for (float amp : {500.0F, 1000.0F, 1500.0F}) {
        const auto x = echo(300e-6F, amp, 0.0F, 1);
        const auto e = dsp::analyze(x.data(), kN, p, scratch.data());
        TEST_ASSERT_TRUE(e.ok);
        TEST_ASSERT_FALSE(e.clipped);
        const float off = e.arrival_s - 300e-6F;
        if (amp == 500.0F) first = off;
        TEST_ASSERT_FLOAT_WITHIN(0.15e-6F, first, off);   // same envelope shape -> same 50 % point
    }
    // with 30 dB SNR noise the absolute arrival jitters ~1 us (envelope noise / envelope slope). It only sets the mean
    // time of flight (speed of sound, ~0.3 % per quad, averaged over 16 quads/s); the wind uses the reciprocal delay.
    for (std::uint32_t seed = 2; seed < 8; ++seed) {
        const auto x = echo(300e-6F, 600.0F, 600.0F / 31.6F / 1.414F, seed);
        const auto e = dsp::analyze(x.data(), kN, p, scratch.data());
        TEST_ASSERT_TRUE(e.ok);
        TEST_ASSERT_FLOAT_WITHIN(1.5e-6F, first, e.arrival_s - 300e-6F);
    }
}

void test_reciprocal_delay_is_subsample_and_slip_free(void) {
    const auto p = params();
    for (float dt : {0.0F, 0.37e-6F, 7.9e-6F, 31.2e-6F, -64.6e-6F}) {
        const auto ab = echo(310e-6F, 900.0F, 0.0F, 3);
        const auto ba = echo(310e-6F + dt, 880.0F, 0.0F, 4);
        const auto r = dsp::pair_times(ab.data(), ba.data(), kN, p, 0.0F, scratch.data());
        TEST_ASSERT_TRUE(r.ok);
        TEST_ASSERT_FLOAT_WITHIN(40e-9F, dt, r.t_ba_s - r.t_ab_s);   // > 1 carrier period without cycle slips
        TEST_ASSERT_TRUE(r.coeff > 0.95F);
    }
    // 30 dB SNR: still within 80 ns (~0.05 m/s on this geometry)
    for (std::uint32_t seed = 10; seed < 16; ++seed) {
        const float dt = 2.5e-6F;
        const float noise = 900.0F / 31.6F / 1.414F;
        const auto ab = echo(310e-6F, 900.0F, noise, seed);
        const auto ba = echo(310e-6F + dt, 900.0F, noise, seed + 100);
        const auto r = dsp::pair_times(ab.data(), ba.data(), kN, p, 0.0F, scratch.data());
        TEST_ASSERT_TRUE(r.ok);
        TEST_ASSERT_FLOAT_WITHIN(80e-9F, dt, r.t_ba_s - r.t_ab_s);
    }
}

void test_wind_end_to_end_on_a_reflected_path(void) {
    // 0.1 m acoustic path, wind along the path axis; c = 343 m/s
    const float path = 0.1F, c = 343.0F;
    const auto p = params();
    const float delay = 22e-6F;   // electronic + transducer delay
    // still-air calibration: the constant offset between the true arrival and the envelope 50 % point
    const auto ref = echo(300e-6F, 700.0F, 0.0F, 20);
    const float cal = dsp::analyze(ref.data(), kN, p, scratch.data()).arrival_s - 300e-6F;
    for (float v : {0.3F, 5.0F, 18.0F, 42.0F}) {
        const float t_ab = path / (c + v), t_ba = path / (c - v);
        const auto ab = echo(t_ab + delay, 700.0F, 8.0F, 21);
        const auto ba = echo(t_ba + delay, 700.0F, 8.0F, 22);
        const auto r = dsp::pair_times(ab.data(), ba.data(), kN, p, delay + cal, scratch.data());
        TEST_ASSERT_TRUE(r.ok);
        const float v_est = 0.5F * path * (1.0F / r.t_ab_s - 1.0F / r.t_ba_s);
        TEST_ASSERT_FLOAT_WITHIN(0.1F + 0.01F * v, v, v_est);
    }
}

void test_no_echo_clipping_and_gain_control(void) {
    const auto p = params();
    const auto silent = echo(300e-6F, 0.0F, 20.0F, 5);
    const auto e0 = dsp::analyze(silent.data(), kN, p, scratch.data());
    TEST_ASSERT_FALSE(e0.ok);
    TEST_ASSERT_EQUAL_UINT8(3, dsp::next_gain(2, e0));   // weak -> more gain

    const auto loud = echo(300e-6F, 3000.0F, 0.0F, 6);
    const auto e1 = dsp::analyze(loud.data(), kN, p, scratch.data());
    TEST_ASSERT_TRUE(e1.clipped);
    TEST_ASSERT_EQUAL_UINT8(1, dsp::next_gain(2, e1));   // clipped -> less gain
    TEST_ASSERT_EQUAL_UINT8(0, dsp::next_gain(0, e1));

    const auto ok = echo(300e-6F, 1000.0F, 0.0F, 7);
    TEST_ASSERT_EQUAL_UINT8(2, dsp::next_gain(2, dsp::analyze(ok.data(), kN, p, scratch.data())));

    // an echo already high at the window start (direct-path leak / ringing) is rejected, not mistimed
    const auto early = echo(120e-6F, 1000.0F, 0.0F, 8);
    TEST_ASSERT_FALSE(dsp::analyze(early.data(), kN, p, scratch.data()).ok);
}

void test_rain_detector_impacts(void) {
    RainDetector rd(50000.0F, 40.0F);
    RainImpact imp{};
    std::uint32_t t = 0;
    int events = 0;
    auto feed = [&](float mv) {
        const auto raw = static_cast<std::uint16_t>(std::fmax(0.0F, std::fmin(4095.0F, std::round(mv / RainDetector::kMvPerCount))));
        if (rd.add(raw, t, imp)) ++events;
        t += 20;
    };
    for (int i = 0; i < 5000; ++i) feed(1650.0F);                   // 0.1 s idle
    for (int i = 0; i < 200; ++i) {                                  // 4 ms decaying ring, 400 mV peak
        const float u = static_cast<float>(i) * 20e-6F;
        feed(1650.0F + 400.0F * std::exp(-u / 0.6e-3F) * std::cos(2.0F * kPi * 3000.0F * u));
    }
    for (int i = 0; i < 100; ++i) feed(1650.0F);
    TEST_ASSERT_EQUAL_INT(1, events);
    TEST_ASSERT_UINT16_WITHIN(15, 400, imp.peak_mv);
    TEST_ASSERT_TRUE(imp.width_us > 500 && imp.width_us < 4000);
    TEST_ASSERT_FALSE(imp.clipped);
    for (int i = 0; i < 50; ++i) feed(3400.0F);                      // hail: rail-to-rail
    for (int i = 0; i < 100; ++i) feed(1650.0F);
    TEST_ASSERT_EQUAL_INT(2, events);
    TEST_ASSERT_TRUE(imp.clipped);
}

void test_register_bank_protocol(void) {
    RegisterBank bank({1, 2, 3});
    std::uint8_t buf[64];
    TEST_ASSERT_EQUAL(1, bank.read(kWhoAmI, buf, sizeof buf));
    TEST_ASSERT_EQUAL_HEX8(kWhoAmIValue, buf[0]);
    TEST_ASSERT_FALSE(bank.irq_level());

    WindBlock w{};
    w.seq = 7;
    w.tof_ps[kSN] = 291500000U;
    w.flags[kEW] = kPathLowCorr;
    bank.publish_wind(w);
    TEST_ASSERT_TRUE(bank.irq_level());
    const std::size_t n = bank.read(kWind, buf, sizeof buf);
    WindBlock got{};
    TEST_ASSERT_TRUE(decode(buf, n, got));
    TEST_ASSERT_EQUAL_UINT32(291500000U, got.tof_ps[kSN]);
    TEST_ASSERT_EQUAL_UINT8(kPathLowCorr, got.flags[kEW]);
    TEST_ASSERT_FALSE(bank.irq_level());
    buf[5] ^= 0x01;                                    // a flipped bit on the cable is caught by the CRC
    TEST_ASSERT_FALSE(decode(buf, n, got));

    bank.set_now_ms(1000);
    bank.push_rain({0, 412, 1500, false});
    bank.push_strike(1, 12, 77000, false, 1000);
    bank.push_strike(2, 0, 0, true, 1100);
    bank.set_now_ms(1250);
    TEST_ASSERT_TRUE(bank.irq_level());
    TEST_ASSERT_EQUAL(1, bank.read(kRainCount, buf, sizeof buf));
    TEST_ASSERT_EQUAL_UINT8(1, buf[0]);
    RainEvent re{};
    TEST_ASSERT_TRUE(decode(buf, bank.read(kRainPop, buf, sizeof buf), re));
    TEST_ASSERT_EQUAL_UINT16(412, re.peak_mv);
    TEST_ASSERT_EQUAL_UINT32(250, re.age_ms);
    StrikeEvent se{};
    TEST_ASSERT_TRUE(decode(buf, bank.read(kStrikePop, buf, sizeof buf), se));
    TEST_ASSERT_EQUAL_UINT8(1, se.kind);
    TEST_ASSERT_EQUAL_UINT8(12, se.distance_km);
    TEST_ASSERT_EQUAL_UINT32(250, se.age_ms);
    TEST_ASSERT_TRUE(decode(buf, bank.read(kStrikePop, buf, sizeof buf), se));
    TEST_ASSERT_EQUAL_UINT8(1, se.during_burst);
    TEST_ASSERT_FALSE(bank.irq_level());

    // config: only valid, CRC-sealed, in-range blocks are accepted
    ConfigBlock c = bank.config();
    c.quads_per_s = 8;
    seal(c);
    std::uint8_t wr[1 + sizeof(ConfigBlock)] = {kConfig};
    std::memcpy(wr + 1, &c, sizeof c);
    bank.write(wr, sizeof wr);
    TEST_ASSERT_EQUAL_UINT8(8, bank.config().quads_per_s);
    c.quads_per_s = 50;
    seal(c);
    std::memcpy(wr + 1, &c, sizeof c);
    bank.write(wr, sizeof wr);
    TEST_ASSERT_EQUAL_UINT8(8, bank.config().quads_per_s);

    const std::uint8_t cmd[2] = {kCommand, kCmdEnterBootloader};
    bank.write(cmd, 2);
    TEST_ASSERT_EQUAL(kCmdEnterBootloader, bank.take_command());
    TEST_ASSERT_EQUAL(kCmdNone, bank.take_command());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_crc8_matches_sensirion_vector);
    RUN_TEST(test_arrival_is_gain_invariant);
    RUN_TEST(test_reciprocal_delay_is_subsample_and_slip_free);
    RUN_TEST(test_wind_end_to_end_on_a_reflected_path);
    RUN_TEST(test_no_echo_clipping_and_gain_control);
    RUN_TEST(test_rain_detector_impacts);
    RUN_TEST(test_register_bank_protocol);
    return UNITY_END();
}
