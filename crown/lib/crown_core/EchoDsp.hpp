#pragma once
// Ultrasonic echo timing for the crown STM32 (host-testable, no HAL).
//
// Method (after QingStation's measured lessons and TI's reciprocal-ToF notes):
//   1. absolute arrival  = 50 %-of-peak crossing of the 40 kHz envelope (I/Q demodulation, one-cycle boxcar). The
//      envelope shape does not depend on amplitude, so this is gain-invariant; it only needs ~0.5 us accuracy because
//      it sets the speed of sound / mean time of flight, not the wind.
//   2. reciprocal delay  = cross-correlation of the A->B and B->A waveforms with parabolic peak interpolation
//      (tens of ns). Strong wind shifts the echoes by more than one 25 us carrier period, so the correlation lobe is
//      chosen around the coarse envelope difference first (no cycle slips), then refined.
//   3. t_AB = t_mean - dt / 2, t_BA = t_mean + dt / 2  ->  wind::UltrasonicWindSolver on the station.

#include <cstddef>
#include <cstdint>

namespace crown {
namespace dsp {

constexpr int kAdcFullScale = 2048;   // 12-bit ADC, samples centred at 0 (raw - 2048)

struct EchoParams {
    float fs_hz{2.0e6F};       // ADC sample rate
    float f0_hz{40.0e3F};      // transducer resonance
    float t0_s{0.0F};          // time of sample 0 after the first drive edge
    float min_tof_s{150e-6F};  // earliest geometrically possible arrival (window start for the search)
    float max_tof_s{600e-6F};
};

struct Echo {
    bool ok{false};
    bool clipped{false};
    float arrival_s{0.0F};     // 50 % envelope crossing, seconds after the drive edge
    float peak{0.0F};          // envelope peak (ADC counts, amplitude)
    float noise_rms{0.0F};     // RMS before min_tof (ADC counts)
    float snr_db{0.0F};
    std::size_t peak_index{0};
};

// `scratch` must hold 2 * n floats (envelope + carrier-removed signal).
Echo analyze(const std::int16_t* x, std::size_t n, const EchoParams& p, float* scratch);

struct Reciprocal {
    bool ok{false};
    float dt_s{0.0F};          // arrival(b) - arrival(a)
    float coeff{0.0F};         // normalised correlation at the peak (0..1)
};

// Delay of `b` relative to `a`. `coarse_dt_s` (envelope difference) selects the correlation lobe; the search spans
// +/- half a carrier period around it. `start`/`len` restrict the correlation to the echo region of `a`.
Reciprocal reciprocal_delay(const std::int16_t* a, const std::int16_t* b, std::size_t n, std::size_t start,
                            std::size_t len, float fs_hz, float f0_hz, float coarse_dt_s);

struct PairTimes {
    bool ok{false};
    float t_ab_s{0.0F};
    float t_ba_s{0.0F};
    float coeff{0.0F};
};

// Combine both directions of one transducer pair. `delay_s` = electronic + transducer delay (calibrated).
PairTimes pair_times(const std::int16_t* ab, const std::int16_t* ba, std::size_t n, const EchoParams& p,
                     float delay_s, float* scratch, Echo* e_ab = nullptr, Echo* e_ba = nullptr);

// Automatic gain: index 0..4 = x2, x4, x8, x16, x32 (STM32 OPAMP PGA). Aim for an envelope peak of 25..75 % FS.
std::uint8_t next_gain(std::uint8_t gain, const Echo& e);

}  // namespace dsp
}  // namespace crown
