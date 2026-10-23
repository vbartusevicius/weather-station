#pragma once
// Home calibration procedures (Web UI "Calibrate" tab, CALIBRATION.md "Home calibration"). Host-testable.
//
// Every procedure is a short session fed with one Sample per second (plus rain impacts from the crown). A session
// collects what it needs, computes a result with suggested StationConfig values, and nothing changes until the user
// presses Apply. All references come from household items or public data:
//   compass   rotate the assembled station 2 turns on a table            -> magnetometer hard / soft-iron fit
//   heading   N-mark bearing from a sun shadow, a map landmark or a compass -> heading offset or fixed heading
//   car_run   steady known speed in calm air (car, hand-held walk / cycle; both directions) or a reference
//             anemometer next to the station                               -> wind speed gain
//   drop_test drops of known size from a known height                      -> rain impact scale
//   rain_gauge rain total of a straight-walled container / cheap gauge     -> rain gain
//   pressure  map elevation + height above ground, or a reference sea-level pressure (METAR / met service)
//                                                                           -> station elevation
//   clear_sky cloudless sky, sun >= 25 deg, against the clear-sky model     -> UV / light cover transmission
//   uv_reference LTR-390 behind the calibrated cover (no reference), TEMIS clear-sky noon UVI or a UV meter
//                                                                           -> GUVB-C31SM scale
//   salt_rh   probe sealed over saturated table salt (75 % RH)              -> RH offset
//   ice_point probe sealed in a bag in an ice-water bath (0.0 C)            -> temperature offset
//   co2_fresh_air station in fresh outdoor air                              -> SCD41 forced recalibration

#include <cstdint>
#include <string>

#include "../core/Config.hpp"
#include "../core/DerivedState.hpp"
#include "../core/RawState.hpp"

namespace station {
namespace calibration {

enum class Kind : std::uint8_t {
    None = 0,
    Compass,
    Heading,
    CarRun,
    DropTest,
    RainGauge,
    Pressure,
    ClearSky,
    UvReference,
    SaltRh,
    IcePoint,
    Co2FreshAir,
    Count
};
const char* kind_name(Kind k);
Kind kind_from_name(const char* name);

// One second of the readings the procedures use, always before the correction that the procedure itself sets.
struct Sample {
    std::uint32_t t_ms{0};
    bool mag_ok{false};
    core::Vec3 mag_ut{};          // raw magnetometer (before mag_offset_ut / mag_scale)
    bool heading_ok{false};
    float heading_deg{0.0F};      // magnetometer heading with every stored correction (also in fixed-heading mode)
    bool tilt_ok{false};
    float tilt_deg{0.0F};
    bool sun_ok{false};
    float sun_alt_deg{0.0F};
    float sun_az_deg{0.0F};
    bool solar_noon_ok{false};
    float solar_noon_min{0.0F};   // local minutes after midnight
    bool wind_ok{false};
    float wind_m_s{0.0F};         // calibrated 1-s mean speed
    float wind_raw_m_s{0.0F};     // the same before wind_speed_gain
    bool pressure_ok{false};
    float station_hpa{0.0F};      // wind-compensated station pressure
    bool temp_ok{false};
    float temp_c{0.0F};           // SHT40 before temp_offset_c
    bool rh_ok{false};
    float rh_pct{0.0F};           // SHT40 before rh_offset_pct
    bool lux_ok{false};
    float lux_raw{0.0F};          // illuminance behind the cover (before optics_transmission)
    bool ltr_uvi_ok{false};
    float ltr_uvi_raw{0.0F};      // LTR-390 UV index behind the cover
    bool guvb_ok{false};
    float guvb_counts{0.0F};      // GUVB-C31SM counts behind the cover
    bool rain_ok{false};
    float rain_total_mm{0.0F};    // pipeline rain total (rain_gain applied)
    bool co2_ok{false};
    float co2_ppm{0.0F};
};

// Builds a Sample from the firmware state. `mag_raw` = the uncorrected magnetometer reading (nullptr if none).
Sample make_sample(const core::RawState& raw, const core::DerivedState& d, const core::StationConfig& cfg,
                   const core::Vec3* mag_raw);

// Applies the magnetometer correction (mag_offset_ut / mag_scale).
core::Vec3 corrected_mag(const core::Vec3& raw, const core::StationConfig& cfg);

// ---- pure computations (shared with the tests) -------------------------------------------------------------------
struct CompassFit {
    bool ok{false};
    const char* reason{"no data"};
    float offset_x{0.0F}, offset_y{0.0F};
    float scale_x{1.0F}, scale_y{1.0F};
    float radius_ut{0.0F};
    int coverage_bins{0};          // 30-deg bins of the horizontal field direction that were visited (of 12)
};
CompassFit fit_compass(const float* x, const float* y, int n);

float circular_mean_deg(const float* deg, int n);
float wrap180(float deg);
float wrap360(float deg);
float initial_bearing_deg(double lat1, double lon1, double lat2, double lon2);
// Impact amplitude the rain model (HapticRain) expects for a water drop of `volume_mm3` falling `height_m` (mV).
float expected_drop_mv(float volume_mm3, float height_m);
float drop_terminal_velocity_m_s(float diameter_mm);
// Station elevation that makes the pipeline's QNH reduction return `qnh_hpa` for `station_hpa` at `temp_c`.
float elevation_from_qnh_m(float station_hpa, float qnh_hpa, float temp_c);
// Equilibrium relative humidity over saturated NaCl (Greenspan 1977), 0..50 C.
float salt_rh_pct(float temp_c);
constexpr float kLuxPerWm2 = 122.0F;   // same luminous efficacy as the pipeline

// ---- sessions ------------------------------------------------------------------------------------------------------
struct CarRunRecord {
    float ref_m_s{0.0F};
    float station_raw_m_s{0.0F};
    std::uint32_t seconds{0};
};

class Calibrator {
public:
    static constexpr int kMaxSamples = 600;   // 10 min at 1 Hz
    static constexpr int kMaxPeaks = 64;
    static constexpr int kMaxRuns = 8;

    // Line-protocol entry used by the HTTP handler and the simulator: `body` = {"cmd": "start" | "finish" | "apply" |
    // "cancel" | "clear_runs", "kind": "...", "p1": .., "p2": .., "p3": ..}. Returns the status JSON; sets
    // `cfg_changed` when Apply modified `cfg`.
    std::string handle(const char* body, core::StationConfig& cfg, std::int64_t unix_s, std::uint32_t now_ms,
                       bool& cfg_changed);
    std::string status_json(const core::StationConfig& cfg, std::uint32_t now_ms) const;

    bool start(Kind kind, float p1, float p2, float p3, const core::StationConfig& cfg, std::uint32_t now_ms);
    bool finish(float p1, const core::StationConfig& cfg, std::uint32_t now_ms);
    bool apply(core::StationConfig& cfg, std::int64_t unix_s);
    void cancel();
    void clear_runs() { n_runs_ = 0; }

    void add_sample(const Sample& s, const core::StationConfig& cfg);
    void add_rain_peak(std::uint16_t raw_peak_mv);

    // SCD41 forced recalibration requested by an applied co2_fresh_air result (executed by the firmware).
    bool take_frc_request(float& ppm);
    void report_frc(bool ok, int correction_ppm);

    Kind kind() const { return kind_; }
    bool running() const { return state_ == State::Running; }
    bool result_ok() const { return state_ == State::Done && ok_; }

private:
    enum class State : std::uint8_t { Idle, Running, Done };
    std::uint32_t duration_s(Kind k) const;
    void compute(const core::StationConfig& cfg);
    void set_message(const char* fmt, ...);
    void suggest(const char* key, float value);

    Kind kind_{Kind::None};
    State state_{State::Idle};
    bool ok_{false};
    std::uint32_t start_ms_{0};
    std::uint32_t last_ms_{0};
    bool applied_{false};
    float p1_{0.0F}, p2_{0.0F}, p3_{0.0F};
    float finish_p1_{0.0F};
    bool fixed_{false};          // heading: fixed heading instead of a magnetometer correction
    float bearing_{0.0F};        // heading: true bearing of the N mark
    char message_[200]{};
    Sample last_{};
    bool have_last_{false};
    bool last_sun_ok_{false};
    float last_sun_az_{0.0F}, last_sun_alt_{0.0F};

    // sample storage (reused by every procedure)
    int n_{0};
    float a_[kMaxSamples]{};
    float b_[kMaxSamples]{};
    float c_[kMaxSamples]{};
    // rain
    std::uint16_t peaks_[kMaxPeaks]{};
    int n_peaks_{0};
    float rain_last_{0.0F};
    bool rain_seen_{false};
    float rain_acc_mm_{0.0F};
    // car runs
    CarRunRecord runs_[kMaxRuns]{};
    int n_runs_{0};
    // suggested config values
    static constexpr int kMaxSuggest = 6;
    const char* sug_key_[kMaxSuggest]{};
    float sug_val_[kMaxSuggest]{};
    int n_sug_{0};
    // SCD41 FRC hand-off
    bool frc_pending_{false};
    float frc_ppm_{0.0F};
    char frc_msg_[64]{};
};

}  // namespace calibration
}  // namespace station
