#pragma once
// Station climatology on top of TieredHistory (Web UI Trends): records (today / yesterday / this month / all time,
// each with its time, persisted in NVS) and the speed-binned wind rose. The rose keeps only the running hour and day
// in RAM; each closed bucket goes into that hour's / day's HistoryStore record, and any period's rose is the sum of
// those records (WindRose::Bucket).

#include <array>
#include <cstddef>
#include <cstdint>

namespace station {
namespace history {

enum class Rec : std::uint8_t {
    TempMax = 0, TempMin, RhMax, RhMin, QnhMax, QnhMin, GustMax, RainRateMax, RainDayMax, UvMax, SolarMax, Pm25Max, Count
};
enum class Period : std::uint8_t { Today = 0, Yesterday, Month, AllTime, Count };
constexpr std::size_t kRecCount = static_cast<std::size_t>(Rec::Count);
constexpr std::size_t kPeriodCount = static_cast<std::size_t>(Period::Count);

const char* rec_key(Rec r);
const char* period_key(Period p);
bool rec_is_max(Rec r);

struct Record {
    float v{0.0F};
    std::int64_t unix_s{0};   // 0 = no record yet
};

struct RecordInputs {        // current values; ok == false skips the metric
    bool temp_ok{false}; float temp_c{0};
    bool rh_ok{false}; float rh_pct{0};
    bool qnh_ok{false}; float qnh_hpa{0};
    bool gust_ok{false}; float gust_m_s{0};      // 3-s wind
    bool rain_ok{false}; float rain_rate_mm_h{0}; float rain_today_mm{0};
    bool uv_ok{false}; float uv_index{0};
    bool solar_ok{false}; float solar_w_m2{0};
    bool pm25_ok{false}; float pm25_ug_m3{0};
};

// Local calendar helpers (days since 1970-01-01 <-> y/m/d).
void civil_from_days(std::int64_t days, int& y, unsigned& m, unsigned& d);

class Records {
public:
    // `local_day_idx` = local days since epoch; rolls today -> yesterday at local midnight and resets the month.
    void update(const RecordInputs& in, std::int64_t unix_s, std::int64_t local_day_idx);
    const Record& get(Period p, Rec r) const { return rec_[static_cast<std::size_t>(p)][static_cast<std::size_t>(r)]; }

private:
    void offer(Rec r, float v, std::int64_t unix_s);
    std::array<std::array<Record, kRecCount>, kPeriodCount> rec_{};
    std::int64_t day_idx_{-1};
    std::int32_t month_key_{-1};   // year * 12 + month - 1 (local)
    friend class Climate;
};

class WindRose {
public:
    static constexpr std::size_t kSectors = 16;
    static constexpr std::size_t kBins = 6;
    static constexpr float kEdges[kBins] = {0.5F, 2.0F, 4.0F, 6.0F, 8.0F, 11.0F};   // m/s, below 0.5 = calm
    struct Bucket {
        std::uint16_t c[kSectors][kBins]{};
        std::uint16_t calm{0};
    };
    // One closed minute (mean speed, vector-mean direction the wind blows FROM).
    void add_minute(float speed_m_s, float dir_deg);
    // Closes the running hour / day bucket at the history boundaries (indices as in TieredHistory::advance).
    void advance(std::int64_t hour_idx, std::int64_t day_idx);
    // The bucket of the hour / day that just closed (once), for the HistoryStore record.
    bool take_closed(bool day, std::int64_t& idx, Bucket& out);
    const Bucket& current(bool day) const { return day ? cur_day_ : cur_hour_; }
    void restore_day(const Bucket& b) { add(cur_day_, b); }   // after a reboot: today's stored hours
    static void add(Bucket& into, const Bucket& b);

private:
    Bucket cur_hour_{}, cur_day_{};
    Bucket closed_hour_{}, closed_day_{};
    std::int64_t closed_hour_idx_{0}, closed_day_idx_{0};
    bool closed_hour_ok_{false}, closed_day_ok_{false};
    std::int64_t hour_idx_{-1}, day_idx_{-1};
};

class Climate {
public:
    Records& records() { return records_; }
    const Records& records() const { return records_; }
    WindRose& rose() { return rose_; }
    const WindRose& rose() const { return rose_; }

    // Records persistence (NVS blob; the rose lives in the HistoryStore records).
    static constexpr std::uint32_t kMagic = 0x41434C4DU;   // "ACLM"
    static constexpr std::uint16_t kVersion = 2;
    std::size_t serialized_size() const;
    std::size_t serialize(std::uint8_t* buf, std::size_t cap) const;
    bool deserialize(const std::uint8_t* buf, std::size_t len);

private:
    Records records_{};
    WindRose rose_{};
};

}  // namespace history
}  // namespace station
