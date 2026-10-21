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
    std::uint16_t n{0};  // samples (0 = no data in this interval)

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

// Per-metric 3-tier store. Every 1-s sample is added to all three running accumulators; each
// accumulator closes into its ring at its wall-clock boundary (local time for days).
class TieredHistory {
public:
    static constexpr std::size_t kMinutes = 60;
    static constexpr std::size_t kHours = 24;
    static constexpr std::size_t kDays = 31;

    // Memory: kMetricCount x (60 + 24 + 31 + 3 accumulators) x sizeof(Slot)
    static constexpr std::size_t kBytes = kMetricCount * (kMinutes + kHours + kDays + 3) * 16U;

    void add(Metric m, float v);

    // Called once per second with the current interval indices; closes accumulators whose index
    // changed and inserts empty slots for skipped intervals (e.g. power loss, clock jump).
    void advance(std::int64_t minute_idx, std::int64_t hour_idx, std::int64_t day_idx);
    // Switch the time base (uptime -> wall clock) keeping all data and running accumulators.
    void rebase(std::int64_t minute_idx, std::int64_t hour_idx, std::int64_t day_idx) {
        minute_idx_ = minute_idx;
        hour_idx_ = hour_idx;
        day_idx_ = day_idx;
    }

    const Slot& current(Metric m, Tier t) const;
    const Slot& slot(Metric m, Tier t, std::size_t i) const;  // i = 0 oldest .. size-1 newest closed
    static std::size_t size(Tier t);

    // Aggregate of the last `n` closed slots plus the running accumulator of a tier.
    bool window_value(Metric m, Tier t, std::size_t n, float& out) const;
    bool window_min(Metric m, Tier t, std::size_t n, float& out) const;
    bool window_max(Metric m, Tier t, std::size_t n, float& out) const;

    // Persistence of hour + day tiers (minutes are not worth flash wear).
    static constexpr std::uint32_t kMagic = 0x41484953U;  // "AHIS"
    static constexpr std::uint16_t kVersion = 2;  // v2: PM4 removed (PMSA003I)
    std::size_t serialized_size() const;
    std::size_t serialize(std::uint8_t* buf, std::size_t cap, std::int64_t unix_s) const;
    // Restores hour/day rings saved at `saved_unix`, shifting by the elapsed hours/days.
    bool deserialize(const std::uint8_t* buf, std::size_t len, std::int64_t now_hour_idx, std::int64_t now_day_idx);

    std::int64_t minute_idx() const { return minute_idx_; }
    std::int64_t hour_idx() const { return hour_idx_; }
    std::int64_t day_idx() const { return day_idx_; }

private:
    struct PerMetric {
        Ring<kMinutes> minutes;
        Ring<kHours> hours;
        Ring<kDays> days;
        Slot acc_min;
        Slot acc_hour;
        Slot acc_day;
    };
    std::array<PerMetric, kMetricCount> m_{};
    std::int64_t minute_idx_{-1};
    std::int64_t hour_idx_{-1};
    std::int64_t day_idx_{-1};
};

}  // namespace history
}  // namespace station
