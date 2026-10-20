#include "TieredHistory.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace station {
namespace history {

namespace {
constexpr float kDegToRad = 3.14159265358979F / 180.0F;

constexpr MetricInfo kMetrics[kMetricCount] = {
    {"temp_c", "°C", Agg::Mean},
    {"rh_pct", "%", Agg::Mean},
    {"dew_point_c", "°C", Agg::Mean},
    {"qnh_hpa", "hPa", Agg::Mean},
    {"wind_m_s", "m/s", Agg::Mean},
    {"wind_dir_deg", "°", Agg::Direction},
    {"rain_mm", "mm", Agg::Sum},
    {"rain_rate_mm_h", "mm/h", Agg::Max},
    {"hail_mm", "mm", Agg::Sum},
    {"hail_rate_mm_h", "mm/h", Agg::Max},
    {"strikes", "", Agg::Sum},
    {"lightning_km", "km", Agg::Min},
    {"solar_w_m2", "W/m²", Agg::Mean},
    {"uv_index", "", Agg::Mean},
    {"lux", "lx", Agg::Mean},
    {"cloud_pct", "%", Agg::Mean},
    {"pm1", "µg/m³", Agg::Mean},
    {"pm25", "µg/m³", Agg::Mean},
    {"pm4", "µg/m³", Agg::Mean},
    {"pm10", "µg/m³", Agg::Mean},
    {"co2_ppm", "ppm", Agg::Mean},
    {"voc_index", "", Agg::Mean},
    {"nox_index", "", Agg::Mean},
    {"ppv_mm_s", "mm/s", Agg::Max},
    {"pga_cm_s2", "cm/s²", Agg::Max},
    {"heater_w", "W", Agg::Mean},
    {"vin_v", "V", Agg::Mean},
};

void merge(Agg agg, Slot& into, const Slot& s) {
    if (s.n == 0) {
        return;
    }
    if (into.n == 0) {
        into = s;
        return;
    }
    switch (agg) {
        case Agg::Mean:
            into.a += s.a;
            into.b = std::min(into.b, s.b);
            into.c = std::max(into.c, s.c);
            break;
        case Agg::Sum:
            into.a += s.a;
            break;
        case Agg::Max:
            into.a = std::max(into.a, s.a);
            break;
        case Agg::Min:
            into.a = std::min(into.a, s.a);
            break;
        case Agg::Direction:
            into.a += s.a;
            into.b += s.b;
            break;
    }
    into.n = static_cast<std::uint16_t>(std::min<std::uint32_t>(65535U, std::uint32_t{into.n} + s.n));
}

template <std::size_t N>
void close_into(Ring<N>& ring, Slot& acc, std::int64_t old_idx, std::int64_t new_idx) {
    if (old_idx < 0 || new_idx == old_idx) {
        return;
    }
    ring.push(acc);
    acc = Slot{};
    if (new_idx > old_idx) {
        const std::int64_t gaps = std::min<std::int64_t>(new_idx - old_idx - 1, static_cast<std::int64_t>(N));
        for (std::int64_t i = 0; i < gaps; ++i) {
            ring.push(Slot{});
        }
    }
}

void put_slot(std::uint8_t*& p, const Slot& s) {
    std::memcpy(p, &s.a, 4);
    std::memcpy(p + 4, &s.b, 4);
    std::memcpy(p + 8, &s.c, 4);
    std::memcpy(p + 12, &s.n, 2);
    p += 14;
}

Slot get_slot(const std::uint8_t*& p) {
    Slot s{};
    std::memcpy(&s.a, p, 4);
    std::memcpy(&s.b, p + 4, 4);
    std::memcpy(&s.c, p + 8, 4);
    std::memcpy(&s.n, p + 12, 2);
    p += 14;
    return s;
}
constexpr std::size_t kHeaderBytes = 4 + 2 + 2 + 8 + 8;
constexpr std::size_t kSlotBytes = 14;
}  // namespace

const MetricInfo& metric_info(Metric m) {
    return kMetrics[static_cast<std::size_t>(m)];
}

bool metric_from_key(const char* key, Metric& out) {
    for (std::size_t i = 0; i < kMetricCount; ++i) {
        if (std::strcmp(kMetrics[i].key, key) == 0) {
            out = static_cast<Metric>(i);
            return true;
        }
    }
    return false;
}

void Slot::add(Agg agg, float v) {
    if (!std::isfinite(v)) {
        return;
    }
    if (agg == Agg::Direction) {
        a += std::sin(v * kDegToRad);
        b += std::cos(v * kDegToRad);
    } else if (n == 0) {
        a = v;
        b = v;
        c = v;
    } else {
        switch (agg) {
            case Agg::Mean:
                a += v;
                b = std::min(b, v);
                c = std::max(c, v);
                break;
            case Agg::Sum:
                a += v;
                break;
            case Agg::Max:
                a = std::max(a, v);
                break;
            case Agg::Min:
                a = std::min(a, v);
                break;
            case Agg::Direction:
                break;
        }
    }
    if (n < 65535U) {
        ++n;
    }
}

bool Slot::value(Agg agg, float& out) const {
    if (n == 0) {
        return false;
    }
    switch (agg) {
        case Agg::Mean:
            out = a / static_cast<float>(n);
            return true;
        case Agg::Direction: {
            if (std::fabs(a) < 1e-6F && std::fabs(b) < 1e-6F) {
                return false;
            }
            float deg = std::atan2(a, b) / kDegToRad;
            out = deg < 0.0F ? deg + 360.0F : deg;
            return true;
        }
        default:
            out = a;
            return true;
    }
}

bool Slot::min(Agg agg, float& out) const {
    if (n == 0 || agg == Agg::Direction) {
        return false;
    }
    out = (agg == Agg::Mean) ? b : a;
    return true;
}

bool Slot::max(Agg agg, float& out) const {
    if (n == 0 || agg == Agg::Direction) {
        return false;
    }
    out = (agg == Agg::Mean) ? c : a;
    return true;
}

void TieredHistory::add(Metric m, float v) {
    PerMetric& pm = m_[static_cast<std::size_t>(m)];
    const Agg agg = metric_info(m).agg;
    pm.acc_min.add(agg, v);
    pm.acc_hour.add(agg, v);
    pm.acc_day.add(agg, v);
}

void TieredHistory::advance(std::int64_t minute_idx, std::int64_t hour_idx, std::int64_t day_idx) {
    for (PerMetric& pm : m_) {
        close_into(pm.minutes, pm.acc_min, minute_idx_, minute_idx);
        close_into(pm.hours, pm.acc_hour, hour_idx_, hour_idx);
        close_into(pm.days, pm.acc_day, day_idx_, day_idx);
    }
    minute_idx_ = minute_idx;
    hour_idx_ = hour_idx;
    day_idx_ = day_idx;
}

const Slot& TieredHistory::current(Metric m, Tier t) const {
    const PerMetric& pm = m_[static_cast<std::size_t>(m)];
    return t == Tier::Minute ? pm.acc_min : (t == Tier::Hour ? pm.acc_hour : pm.acc_day);
}

const Slot& TieredHistory::slot(Metric m, Tier t, std::size_t i) const {
    const PerMetric& pm = m_[static_cast<std::size_t>(m)];
    if (t == Tier::Minute) {
        return pm.minutes.slots[std::min(i, kMinutes - 1)];
    }
    if (t == Tier::Hour) {
        return pm.hours.slots[std::min(i, kHours - 1)];
    }
    return pm.days.slots[std::min(i, kDays - 1)];
}

std::size_t TieredHistory::size(Tier t) {
    return t == Tier::Minute ? kMinutes : (t == Tier::Hour ? kHours : kDays);
}

namespace {
Slot window(const TieredHistory& h, Metric m, Tier t, std::size_t n) {
    const Agg agg = metric_info(m).agg;
    Slot acc = h.current(m, t);
    const std::size_t sz = TieredHistory::size(t);
    const std::size_t take = std::min(n, sz);
    for (std::size_t i = sz - take; i < sz; ++i) {
        merge(agg, acc, h.slot(m, t, i));
    }
    return acc;
}
}  // namespace

bool TieredHistory::window_value(Metric m, Tier t, std::size_t n, float& out) const {
    return window(*this, m, t, n).value(metric_info(m).agg, out);
}

bool TieredHistory::window_min(Metric m, Tier t, std::size_t n, float& out) const {
    return window(*this, m, t, n).min(metric_info(m).agg, out);
}

bool TieredHistory::window_max(Metric m, Tier t, std::size_t n, float& out) const {
    return window(*this, m, t, n).max(metric_info(m).agg, out);
}

std::size_t TieredHistory::serialized_size() const {
    return kHeaderBytes + kMetricCount * (kHours + kDays + 2) * kSlotBytes;
}

std::size_t TieredHistory::serialize(std::uint8_t* buf, std::size_t cap, std::int64_t /*unix_s*/) const {
    const std::size_t need = serialized_size();
    if (cap < need) {
        return 0;
    }
    std::uint8_t* p = buf;
    const std::uint32_t magic = kMagic;
    const std::uint16_t ver = kVersion;
    const std::uint16_t count = static_cast<std::uint16_t>(kMetricCount);
    std::memcpy(p, &magic, 4);
    std::memcpy(p + 4, &ver, 2);
    std::memcpy(p + 6, &count, 2);
    std::memcpy(p + 8, &hour_idx_, 8);
    std::memcpy(p + 16, &day_idx_, 8);
    p += kHeaderBytes;
    for (const PerMetric& pm : m_) {
        for (const Slot& s : pm.hours.slots) {
            put_slot(p, s);
        }
        put_slot(p, pm.acc_hour);
        for (const Slot& s : pm.days.slots) {
            put_slot(p, s);
        }
        put_slot(p, pm.acc_day);
    }
    return need;
}

bool TieredHistory::deserialize(const std::uint8_t* buf, std::size_t len, std::int64_t now_hour_idx,
                                std::int64_t now_day_idx) {
    if (len != serialized_size()) {
        return false;
    }
    std::uint32_t magic = 0;
    std::uint16_t ver = 0;
    std::uint16_t count = 0;
    std::int64_t saved_hour = 0;
    std::int64_t saved_day = 0;
    std::memcpy(&magic, buf, 4);
    std::memcpy(&ver, buf + 4, 2);
    std::memcpy(&count, buf + 6, 2);
    std::memcpy(&saved_hour, buf + 8, 8);
    std::memcpy(&saved_day, buf + 16, 8);
    if (magic != kMagic || ver != kVersion || count != kMetricCount || saved_hour > now_hour_idx ||
        saved_day > now_day_idx) {
        return false;
    }
    const std::uint8_t* p = buf + kHeaderBytes;
    for (PerMetric& pm : m_) {
        for (Slot& s : pm.hours.slots) {
            s = get_slot(p);
        }
        pm.acc_hour = get_slot(p);
        for (Slot& s : pm.days.slots) {
            s = get_slot(p);
        }
        pm.acc_day = get_slot(p);
        close_into(pm.hours, pm.acc_hour, saved_hour, now_hour_idx);
        close_into(pm.days, pm.acc_day, saved_day, now_day_idx);
    }
    hour_idx_ = now_hour_idx;
    day_idx_ = now_day_idx;
    return true;
}

}  // namespace history
}  // namespace station
