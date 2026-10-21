#pragma once

#include <cstdint>

#include "Reading.hpp"

namespace station {
namespace core {

// ---------------------------------------------------------------------------------------------
// RawState: the real-time buffer of corrected sensor measurements.
//
// Written only by the acquisition layer (drivers + signal conditioners). Every value is already
// converted to SI-style engineering units and has sensor-local corrections applied (CRC checked,
// calibration offsets, ultrasonic tilt/north rotation, auto-gain scaling). Cross-sensor physics
// (wind-driven pressure error, solar heating, hygroscopic PM growth...) belongs to processors.
//
// Fast sensors (ultrasonic 4 Hz, IMU 200 Hz, barometer 100 Hz) are reduced to 1-second summaries
// by their conditioners so that every processor runs at a uniform 1 Hz.
// ---------------------------------------------------------------------------------------------

struct WindSecond {
    float v_east_m_s{0.0F};      // vector-mean east component over the second
    float v_north_m_s{0.0F};     // vector-mean north component over the second
    float speed_mean_m_s{0.0F};  // scalar mean of sample speeds over the second
    float sound_speed_m_s{343.2F};
    float virtual_temp_c{20.0F};  // sonic (virtual) temperature from c^2 / (gamma R)
    std::uint8_t samples{0};
};

struct ThermoRaw {           // SHT40 on the probe tip (inside the radiation shield)
    Reading<float> temp_c;
    Reading<float> rh_pct;
};

struct BaroRaw {             // BMP581, 100 Hz FIFO reduced to 1 s
    Reading<float> pressure_hpa;  // 1-s mean station pressure (uncompensated for wind)
    Reading<float> sensor_temp_c;
    Reading<float> ripple_pa;     // RMS of high-passed pressure over the last 10 s (gust / infrasound activity)
};

struct WindRaw {             // reciprocal ultrasonic ToF, already rotated to true north and tilt-corrected
    Reading<WindSecond> second;
    Reading<float> tilt_deg;
    Reading<float> heading_deg;   // crown orientation from MMC5603 + LSM6DS3 tilt compensation
};

struct OpticalRaw {          // dome: TSL25911 (vis+NIR, auto-gain), LTR-390 (ALS + UVS), GUVB-C31SM (UV-B)
    Reading<float> tsl_lux;
    Reading<float> tsl_nir_ratio;  // ch1 / ch0 (0..1): near-infrared share of the TSL25911 signal
    Reading<float> ltr_lux;        // LTR-390 ALS lux (covers > 88 klx when TSL saturates)
    Reading<float> ltr_uvi;        // LTR-390 UVS converted to UV index
    Reading<float> guvb_uvi;       // GUVB-C31SM UV index from its factory NVM calibration (or the configured scale)
    Reading<float> guvb_counts;    // range-normalised UV-B counts minus the factory dark offset (diagnostics)
};

struct AirRaw {              // PMSA003I (PM1/2.5/10), SGP41 (raw MOx ticks), SCD41 (CO2, in-sensor pressure comp.)
    Reading<float> pm1_ug_m3;
    Reading<float> pm25_ug_m3;
    Reading<float> pm10_ug_m3;
    Reading<std::uint16_t> sgp_sraw_voc;  // SGP41 raw VOC ticks, humidity/temperature compensated in-sensor
    Reading<std::uint16_t> sgp_sraw_nox;  // SGP41 raw NOx ticks (invalid during the 10 s conditioning)
    Reading<float> co2_ppm;
    Reading<float> scd41_temp_c;   // SCD41 on-chip temperature = probe heater zone (heater resistors right behind it)
};

struct VibrationSecond {     // LSM6DS3 accelerometer, 200 Hz high-passed (gravity removed), 1-s summary
    float pga_mg{0.0F};           // peak |a| of the high-passed vector
    float ppv_mm_s{0.0F};         // peak |v| of the band-limited integrated velocity
    float dominant_hz{0.0F};      // zero-crossing frequency of the dominant axis
};

struct ImuRaw {
    Reading<Vec3> gravity_g;      // low-passed accelerometer (tilt)
    Reading<VibrationSecond> vibration;
};

struct PowerRaw {
    Reading<float> vin_v;
    Reading<float> reflector_c;   // reflector heater NTC
    Reading<float> refl_heater_a; // ADS1115: mean reflector heater current (0.1 ohm shunt, PWM averaged)
    Reading<float> probe_heater_a;// ADS1115: probe heater current (1 ohm shunt in its return)
    Reading<float> rail_heat;     // ADS1115 AIN2: +12V_HEAT rail (power board U2): both heaters + crown transducer drive
    Reading<float> rail_5v_logic; // ADS1115 AIN3: +5V electronics rail (power board U1)
    bool service_button{false};
    bool rain_adc_ok{false};      // rain AFE sampling is running
};

struct ClockRaw {
    bool valid{false};            // true once SNTP (or a browser time push) set the wall clock
    std::int64_t unix_s{0};
    std::int32_t utc_offset_s{0}; // current local offset incl. DST (from the configured POSIX TZ)
};

struct RawState {
    std::uint32_t now_ms{0};      // monotonic time of this snapshot
    ClockRaw clock;
    ThermoRaw thermo;
    BaroRaw baro;
    WindRaw wind;
    OpticalRaw optical;
    AirRaw air;
    ImuRaw imu;
    PowerRaw power;
};

// ---------------------------------------------------------------------------------------------
// Discrete events: things that happen at an instant and must not be lost between 1 Hz cycles.
// ---------------------------------------------------------------------------------------------

struct RainImpact {          // one piezo pulse from the rain AFE (RAIN_ADC)
    std::uint32_t t_ms{0};
    std::uint16_t peak_mv{0};
    std::uint16_t baseline_mv{0};
    std::uint16_t width_us{0};
};

struct LightningStrike {     // AS3935 lightning interrupt
    std::uint32_t t_ms{0};
    std::uint8_t distance_km{0};  // AS3935 storm-front estimate: 1 = overhead, 63 = out of range
    std::uint32_t energy{0};      // unitless AS3935 energy
};

struct ThunderArrival {      // acoustic impulse onset detected in the BMP581 100 Hz stream
    std::uint32_t t_ms{0};
    float peak_pa{0.0F};
};

// Fixed-capacity FIFO. Producers push from acquisition tasks, the pipeline drains it each second.
// Not internally synchronised: the firmware guards it with the same lock as RawState.
template <typename T, std::uint16_t N>
class EventQueue {
public:
    bool push(const T& ev) {
        if (count_ == N) {
            ++dropped_;
            return false;
        }
        buf_[(head_ + count_) % N] = ev;
        ++count_;
        return true;
    }
    bool pop(T& out) {
        if (count_ == 0) {
            return false;
        }
        out = buf_[head_];
        head_ = static_cast<std::uint16_t>((head_ + 1) % N);
        --count_;
        return true;
    }
    std::uint16_t size() const { return count_; }
    std::uint32_t dropped() const { return dropped_; }

private:
    T buf_[N]{};
    std::uint16_t head_{0};
    std::uint16_t count_{0};
    std::uint32_t dropped_{0};
};

struct EventQueues {
    EventQueue<RainImpact, 256> rain;
    EventQueue<LightningStrike, 16> lightning;
    EventQueue<ThunderArrival, 16> thunder;
};

}  // namespace core
}  // namespace station
