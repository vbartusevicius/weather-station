#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace station {
namespace history {

// How 1-second samples combine into a slot.
enum class Agg : std::uint8_t {
    Mean = 0,       // mean + min + max of samples (temperature, wind speed, PM...)
    Sum = 1,        // total of samples (rain mm, strike count)
    Max = 2,        // peak only (rain rate, vibration)
    Min = 3,        // lowest only (closest lightning distance)
    Direction = 4,  // unit-vector mean of degrees (wind direction)
};

enum class Metric : std::uint8_t {
    TempC = 0,
    RhPct,
    DewPointC,
    QnhHpa,
    WindMs,        // fed with the 3-s mean each second: slot mean = average, min = lull, max = gust
    WindDirDeg,
    RainMm,
    RainRateMmH,
    HailMm,
    HailRateMmH,
    Strikes,
    LightningKm,
    SolarWm2,
    UvIndex,
    Lux,
    CloudPct,
    Pm1,
    Pm25,
    Pm10,
    Co2Ppm,
    VocIndex,
    NoxIndex,
    PpvMmS,
    PgaCmS2,
    HeaterW,
    VinV,
    Count,
};

constexpr std::size_t kMetricCount = static_cast<std::size_t>(Metric::Count);

struct MetricInfo {
    const char* key;
    const char* unit;
    Agg agg;
};

const MetricInfo& metric_info(Metric m);
bool metric_from_key(const char* key, Metric& out);

// One slot (or a running accumulator). For Agg::Direction, a/b hold the summed unit vector (x, y).
struct Slot {
    float a{0.0F};   // Mean: sum of samples; Sum: total; Max/Min: extreme; Direction: sum sin
    float b{0.0F};   // Mean: min sample;     Direction: sum cos
    float c{0.0F};   // Mean: max sample
    std::uint32_t n{0};  // samples (0 = no data in this interval); 32 bit: a day has 86 400 one-second samples

    void add(Agg agg, float v);
    // Representative value of the slot (mean, sum, extreme or direction); false when empty.
    bool value(Agg agg, float& out) const;
    bool min(Agg agg, float& out) const;
    bool max(Agg agg, float& out) const;
};

enum class Tier : std::uint8_t { Minute = 0, Hour = 1, Day = 2 };

template <std::size_t N>
struct Ring {
    std::array<Slot, N> slots{};  // slots[i] = interval (N - i) ago ... slots[N-1] = most recent closed
    void push(const Slot& s) {
        for (std::size_t i = 0; i + 1 < N; ++i) {
            slots[i] = slots[i + 1];
        }
        slots[N - 1] = s;
    }
};

class HistoryStore;

// Per-metric 3-tier history. RAM holds the last 60 minutes, a cache of the last 24 closed hours (used every second by
// the pipeline: 24-h extremes, the 12-h-ago temperature, ...) and the running minute / hour / day accumulators.
// Every closed hour and day is handed out once (take_closed) for the long-term HistoryStore on flash, which serves
// everything older (long_slot). Accumulators close at their wall-clock boundary (local time for hours / days).
class TieredHistory {
public:
    static constexpr std::size_t kMinutes = 60;
    static constexpr std::size_t kHours = 24;   // RAM cache; the store keeps 14 days

    // Memory: kMetricCount x (60 + 24 + 3 accumulators) x sizeof(Slot), plus one closed hour / day snapshot
    static constexpr std::size_t kBytes = kMetricCount * (kMinutes + kHours + 3 + 2) * 16U;

    void add(Metric m, float v);

    // Called once per second with the current interval indices; closes accumulators whose index
    // changed and inserts empty slots for skipped intervals (e.g. power loss, clock jump).
    void advance(std::int64_t minute_idx, std::int64_t hour_idx, std::int64_t day_idx);
    // Switch the time base (uptime -> wall clock) keeping all data and running accumulators.
    void rebase(std::int64_t minute_idx, std::int64_t hour_idx, std::int64_t day_idx) {
        minute_idx_ = minute_idx;
        hour_idx_ = hour_idx;
        day_idx_ = day_idx;
        closed_hour_ok_ = closed_day_ok_ = false;   // intervals of the uptime time base are not stored
    }

    const Slot& current(Metric m, Tier t) const;
    const Slot& slot(Metric m, Tier t, std::size_t i) const;  // RAM rings: i = 0 oldest .. size-1 newest closed
    static std::size_t size(Tier t);                           // RAM ring length (Day: 0)

    // Aggregate of the last `n` closed RAM slots plus the running accumulator of a tier (Minute / Hour).
    bool window_value(Metric m, Tier t, std::size_t n, float& out) const;
    bool window_min(Metric m, Tier t, std::size_t n, float& out) const;
    bool window_max(Metric m, Tier t, std::size_t n, float& out) const;

    // The interval that just closed (all metrics), once; false when nothing closed or it held no samples.
    bool take_closed(Tier t, std::int64_t& idx, std::array<Slot, kMetricCount>& slots);

    // Long-term view: `back` = 0 running interval, 1 = newest closed ... span(t); RAM for minutes and the last 24 h,
    // the attached store for older hours and all days. False when there is no data for that interval.
    void attach(const HistoryStore* store) { store_ = store; }
    const HistoryStore* store() const { return store_; }
    static std::size_t span(Tier t);
    bool long_slot(Metric m, Tier t, std::size_t back, Slot& out) const;
    // After a reboot (wall clock known): refill the 24-h RAM cache and today's running day from the store.
    void restore(const HistoryStore& store);

    std::int64_t minute_idx() const { return minute_idx_; }
    std::int64_t hour_idx() const { return hour_idx_; }
    std::int64_t day_idx() const { return day_idx_; }

private:
    struct PerMetric {
        Ring<kMinutes> minutes;
        Ring<kHours> hours;
        Slot acc_min;
        Slot acc_hour;
        Slot acc_day;
    };
    std::array<PerMetric, kMetricCount> m_{};
    std::int64_t minute_idx_{-1};
    std::int64_t hour_idx_{-1};
    std::int64_t day_idx_{-1};
    std::array<Slot, kMetricCount> closed_hour_{}, closed_day_{};
    std::int64_t closed_hour_idx_{0}, closed_day_idx_{0};
    bool closed_hour_ok_{false}, closed_day_ok_{false};
    const HistoryStore* store_{nullptr};
};

// Combines two slots of the same aggregation (closed hours -> a day, ...).
void merge_slot(Agg agg, Slot& into, const Slot& s);

}  // namespace history
}  // namespace station
