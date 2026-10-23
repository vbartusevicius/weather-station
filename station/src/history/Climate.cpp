#include "Climate.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace station {
namespace history {

namespace {
const char* const kRecKeys[kRecCount] = {"temp_max", "temp_min", "rh_max", "rh_min", "qnh_max", "qnh_min",
                                         "gust_max", "rain_rate_max", "rain_day_max", "uv_max", "solar_max", "pm25_max"};
const char* const kPeriodKeys[kPeriodCount] = {"today", "yesterday", "month", "all_time"};

template <typename T>
void put(std::uint8_t*& p, const T& v) {
    std::memcpy(p, &v, sizeof v);
    p += sizeof v;
}
template <typename T>
void get(const std::uint8_t*& p, T& v) {
    std::memcpy(&v, p, sizeof v);
    p += sizeof v;
}

}  // namespace

const char* rec_key(Rec r) { return kRecKeys[static_cast<std::size_t>(r)]; }
const char* period_key(Period p) { return kPeriodKeys[static_cast<std::size_t>(p)]; }
bool rec_is_max(Rec r) { return r != Rec::TempMin && r != Rec::RhMin && r != Rec::QnhMin; }

void civil_from_days(std::int64_t z, int& y, unsigned& m, unsigned& d) {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y = static_cast<int>(static_cast<std::int64_t>(yoe) + era * 400 + (m <= 2 ? 1 : 0));
}

// ------------------------------------------------------------------------------------------------- records
void Records::offer(Rec r, float v, std::int64_t unix_s) {
    if (!std::isfinite(v)) return;
    const auto i = static_cast<std::size_t>(r);
    for (Period p : {Period::Today, Period::Month, Period::AllTime}) {
        Record& rec = rec_[static_cast<std::size_t>(p)][i];
        if (rec.unix_s == 0 || (rec_is_max(r) ? v > rec.v : v < rec.v)) {
            rec.v = v;
            rec.unix_s = unix_s;
        }
    }
}

void Records::update(const RecordInputs& in, std::int64_t unix_s, std::int64_t local_day_idx) {
    if (unix_s <= 0) return;
    if (local_day_idx != day_idx_) {
        auto& today = rec_[static_cast<std::size_t>(Period::Today)];
        auto& yesterday = rec_[static_cast<std::size_t>(Period::Yesterday)];
        if (day_idx_ >= 0 && local_day_idx == day_idx_ + 1) {
            yesterday = today;
        } else {
            yesterday.fill(Record{});
        }
        today.fill(Record{});
        int y = 0;
        unsigned m = 0, d = 0;
        civil_from_days(local_day_idx, y, m, d);
        const std::int32_t mk = y * 12 + static_cast<std::int32_t>(m) - 1;
        if (mk != month_key_) {
            rec_[static_cast<std::size_t>(Period::Month)].fill(Record{});
            month_key_ = mk;
        }
        day_idx_ = local_day_idx;
    }
    if (in.temp_ok) {
        offer(Rec::TempMax, in.temp_c, unix_s);
        offer(Rec::TempMin, in.temp_c, unix_s);
    }
    if (in.rh_ok) {
        offer(Rec::RhMax, in.rh_pct, unix_s);
        offer(Rec::RhMin, in.rh_pct, unix_s);
    }
    if (in.qnh_ok) {
        offer(Rec::QnhMax, in.qnh_hpa, unix_s);
        offer(Rec::QnhMin, in.qnh_hpa, unix_s);
    }
    if (in.gust_ok) offer(Rec::GustMax, in.gust_m_s, unix_s);
    if (in.rain_ok) {
        if (in.rain_rate_mm_h > 0.0F) offer(Rec::RainRateMax, in.rain_rate_mm_h, unix_s);
        if (in.rain_today_mm > 0.0F) offer(Rec::RainDayMax, in.rain_today_mm, unix_s);
    }
    if (in.uv_ok) offer(Rec::UvMax, in.uv_index, unix_s);
    if (in.solar_ok) offer(Rec::SolarMax, in.solar_w_m2, unix_s);
    if (in.pm25_ok) offer(Rec::Pm25Max, in.pm25_ug_m3, unix_s);
}

// ------------------------------------------------------------------------------------------------- wind rose
constexpr float WindRose::kEdges[WindRose::kBins];

void WindRose::add_minute(float speed_m_s, float dir_deg) {
    if (!std::isfinite(speed_m_s) || speed_m_s < 0.0F) return;
    if (speed_m_s < kEdges[0] || !std::isfinite(dir_deg)) {
        ++cur_hour_.calm;
        ++cur_day_.calm;
        return;
    }
    std::size_t bin = 0;
    while (bin + 1 < kBins && speed_m_s >= kEdges[bin + 1]) ++bin;
    float d = std::fmod(dir_deg, 360.0F);
    if (d < 0.0F) d += 360.0F;
    const auto sec = static_cast<std::size_t>((d + 11.25F) / 22.5F) % kSectors;
    ++cur_hour_.c[sec][bin];
    ++cur_day_.c[sec][bin];
}

void WindRose::add(Bucket& into, const Bucket& b) {
    for (std::size_t s = 0; s < kSectors; ++s)
        for (std::size_t k = 0; k < kBins; ++k) into.c[s][k] = static_cast<std::uint16_t>(into.c[s][k] + b.c[s][k]);
    into.calm = static_cast<std::uint16_t>(into.calm + b.calm);
}

void WindRose::advance(std::int64_t hour_idx, std::int64_t day_idx) {
    if (hour_idx_ >= 0 && hour_idx != hour_idx_) {
        closed_hour_ = cur_hour_;
        closed_hour_idx_ = hour_idx_;
        closed_hour_ok_ = true;
        cur_hour_ = Bucket{};
    }
    if (day_idx_ >= 0 && day_idx != day_idx_) {
        closed_day_ = cur_day_;
        closed_day_idx_ = day_idx_;
        closed_day_ok_ = true;
        cur_day_ = Bucket{};
    }
    hour_idx_ = hour_idx;
    day_idx_ = day_idx;
}

bool WindRose::take_closed(bool day, std::int64_t& idx, Bucket& out) {
    bool& ok = day ? closed_day_ok_ : closed_hour_ok_;
    if (!ok) return false;
    ok = false;
    idx = day ? closed_day_idx_ : closed_hour_idx_;
    out = day ? closed_day_ : closed_hour_;
    return true;
}

// ------------------------------------------------------------------------------------------------- persistence
std::size_t Climate::serialized_size() const {
    return sizeof(kMagic) + sizeof(kVersion) + sizeof(records_.rec_) + sizeof(records_.day_idx_) + sizeof(records_.month_key_);
}

std::size_t Climate::serialize(std::uint8_t* buf, std::size_t cap) const {
    if (cap < serialized_size()) return 0;
    std::uint8_t* p = buf;
    put(p, kMagic);
    put(p, kVersion);
    put(p, records_.rec_);
    put(p, records_.day_idx_);
    put(p, records_.month_key_);
    return static_cast<std::size_t>(p - buf);
}

bool Climate::deserialize(const std::uint8_t* buf, std::size_t len) {
    if (len != serialized_size()) return false;
    const std::uint8_t* p = buf;
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    get(p, magic);
    get(p, version);
    if (magic != kMagic || version != kVersion) return false;
    Records r{};
    get(p, r.rec_);
    get(p, r.day_idx_);
    get(p, r.month_key_);
    records_ = r;
    return true;
}

}  // namespace history
}  // namespace station
