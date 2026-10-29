#pragma once

#include <array>
#include <cstdint>

#include "../airquality/GasIndex.hpp"
#include "../environment/MetCalculations.hpp"
#include "../history/TieredHistory.hpp"
#include "../rain/HapticRain.hpp"
#include "../wind/UltrasonicWind.hpp"
#include "Config.hpp"
#include "DerivedState.hpp"
#include "RawState.hpp"
#include "../history/Climate.hpp"
#include "../history/HistoryStore.hpp"
#include "../quality/Quality.hpp"

namespace station {
namespace core {

// Rolling 3-hour buffer of 5-minute means (37 points = exactly 180 min span) for the barometric
// tendency and the wind-direction change used by the Sager / Buys-Ballot forecast.
class FiveMinuteRing {
public:
    static constexpr std::size_t kSlots = 37;
    struct Entry {
        float qnh{0.0F};
        float u{0.0F};
        float v{0.0F};
        bool qnh_ok{false};
        bool wind_ok{false};
    };
    void accumulate(bool qnh_ok, float qnh, bool wind_ok, float u, float v);
    void close();  // every 5 minutes
    bool change_3h_qnh(float& out) const;  // newest closed 5-min mean minus the one 180 min earlier
    bool dir_change_3h_deg(float& out) const;  // + veering (clockwise)

private:
    std::array<Entry, kSlots> ring_{};
    std::size_t head_{0};
    std::size_t count_{0};
    double sq_{0.0}, su_{0.0}, sv_{0.0};
    std::uint16_t nq_{0}, nw_{0};
};

class Pipeline {
public:
    // Runs all processors for one second. `raw` is a consistent copy of the RawState, events are
    // drained. `out` is fully rewritten.
    void step(const RawState& raw, EventQueues& events, const StationConfig& cfg, DerivedState& out);

    void reset_rain_today() { rain_.reset_daily(); }
    // HEAT_CHECK: drive the reflector heater at a fixed 2 W for 10 s and compare the measured current.
    void request_heater_check() { heater_check_s_ = kHeaterCheckS; heater_check_hold_s_ = 0; }
    static constexpr std::uint8_t kHeaterCheckS = 10;
    // SGP41 VOC gas-index state persistence (see airquality::GasIndexPair).
    bool voc_state(float& s0, float& s1) const { return gas_.voc_state(s0, s1); }
    void restore_voc_state(float s0, float s1) { gas_.restore_voc_state(s0, s1); }
    const history::TieredHistory& history() const { return hist_; }
    history::TieredHistory& history() { return hist_; }
    // Records + wind rose (Trends tab); records persist in NVS, the rose in the HistoryStore records.
    const history::Climate& climate() const { return climate_; }
    history::Climate& climate() { return climate_; }
    // Closed hours / days (wall-clock time only) waiting to be appended to the HistoryStore; false when none.
    bool take_record(history::Tier& tier, history::ClosedInterval& out);
    // After a reboot, once the wall clock is known: 24-h RAM cache, today's running day and wind rose from the store.
    void restore(const history::HistoryStore& store);

private:
    void process_wind(const RawState& raw, DerivedState& out);
    void process_astro(const RawState& raw, const StationConfig& cfg, DerivedState& out);
    void process_solar(const RawState& raw, DerivedState& out);
    void process_thermo(const RawState& raw, DerivedState& out);
    void process_baro(const RawState& raw, const StationConfig& cfg, DerivedState& out);
    void process_wind_load(DerivedState& out);
    void process_rain(const RawState& raw, EventQueues& events, const StationConfig& cfg, DerivedState& out);
    void process_air(const RawState& raw, DerivedState& out);
    void process_lightning(const RawState& raw, EventQueues& events, DerivedState& out);
    void process_seismic(const RawState& raw, DerivedState& out);
    void process_heater(const RawState& raw, DerivedState& out);
    void record_history(const DerivedState& out, float rain_mm_this_second, std::uint16_t strikes_this_second);
    bool minute_trend(history::Metric m, float& per_hour) const;

    wind::WmoWindAggregator wind_{};
    quality::QualityControl quality_{};
    history::Climate climate_{};
    std::array<history::ClosedInterval, 2> pending_{};
    std::array<history::Tier, 2> pending_tier_{};
    std::size_t n_pending_{0};
    rain::HapticRainProcessor rain_{};
    environment::EnvLongevityManager longevity_{};
    airquality::GasIndexPair gas_{};
    std::uint32_t last_sgp_ms_{0};
    bool sgp_seen_{false};
    std::uint8_t heater_check_s_{0};
    const char* heater_check_result_{"—"};
    std::uint8_t heater_check_hold_s_{0};   // keep showing the result for a minute
    history::TieredHistory hist_{};
    FiveMinuteRing five_min_{};

    std::uint32_t seconds_{0};
    bool clock_was_valid_{false};
    std::int64_t last_day_idx_{-1};
    std::int64_t five_min_idx_{-1};
    float last_rain_mm_{0.0F};

    // smoothed / cached state
    float cloud_ema_{-1.0F};
    float lux_per_uva_{0.0F};      // learnt TSL25911 lux / photodiode UV-A W/m^2 (lux estimate while the TSL saturates)
    float thermo_hold_c_{0.0F};
    float thermo_hold_rh_{0.0F};
    bool thermo_hold_ok_{false};
    std::int64_t riseset_key_{-1};
    struct {
        Value<float> sunrise, sunset, daylight, moonrise, moonset;
    } riseset_{};

    // lightning
    std::uint32_t strikes_total_{0};
    bool have_strike_{false};
    std::uint32_t last_strike_ms_{0};
    float last_rf_km_{0.0F};
    bool last_rf_in_range_{false};
    bool last_acoustic_{false};
    float last_delay_s_{0.0F};
    float last_acoustic_km_{0.0F};
};

}  // namespace core
}  // namespace station
