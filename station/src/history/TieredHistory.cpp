#include "TieredHistory.hpp"

#include "HistoryStore.hpp"

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
    {"pm10", "µg/m³", Agg::Mean},
    {"co2_ppm", "ppm", Agg::Mean},
    {"voc_index", "", Agg::Mean},
    {"nox_index", "", Agg::Mean},
    {"ppv_mm_s", "mm/s", Agg::Max},
    {"pga_cm_s2", "cm/s²", Agg::Max},
    {"heater_w", "W", Agg::Mean},
    {"vin_v", "V", Agg::Mean},
};

}  // namespace

void merge_slot(Agg agg, Slot& into, const Slot& s) {
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
    into.n += s.n;
}

namespace {

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
    ++n;
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
    // snapshot the hour / day that closes now (all metrics) for the long-term store
    auto snapshot = [this](bool hour, std::int64_t old_idx, std::int64_t new_idx) {
        if (old_idx < 0 || new_idx == old_idx) return;
        auto& out = hour ? closed_hour_ : closed_day_;
        bool any = false;
        for (std::size_t i = 0; i < kMetricCount; ++i) {
            out[i] = hour ? m_[i].acc_hour : m_[i].acc_day;
            any = any || out[i].n > 0;
        }
        (hour ? closed_hour_idx_ : closed_day_idx_) = old_idx;
        (hour ? closed_hour_ok_ : closed_day_ok_) = any;
    };
    snapshot(true, hour_idx_, hour_idx);
    snapshot(false, day_idx_, day_idx);
    for (PerMetric& pm : m_) {
        close_into(pm.minutes, pm.acc_min, minute_idx_, minute_idx);
        close_into(pm.hours, pm.acc_hour, hour_idx_, hour_idx);
        if (day_idx_ >= 0 && day_idx != day_idx_) pm.acc_day = Slot{};
    }
    minute_idx_ = minute_idx;
    hour_idx_ = hour_idx;
    day_idx_ = day_idx;
}

bool TieredHistory::take_closed(Tier t, std::int64_t& idx, std::array<Slot, kMetricCount>& slots) {
    bool& ok = t == Tier::Day ? closed_day_ok_ : closed_hour_ok_;
    if (t == Tier::Minute || !ok) return false;
    ok = false;
    idx = t == Tier::Day ? closed_day_idx_ : closed_hour_idx_;
    slots = t == Tier::Day ? closed_day_ : closed_hour_;
    return true;
}

std::size_t TieredHistory::span(Tier t) {
    return t == Tier::Minute ? kMinutes : (t == Tier::Hour ? HistoryStore::kHourRecords : HistoryStore::kDayRecords);
}

bool TieredHistory::long_slot(Metric m, Tier t, std::size_t back, Slot& out) const {
    const PerMetric& pm = m_[static_cast<std::size_t>(m)];
    if (back == 0) {
        out = current(m, t);
        return out.n > 0;
    }
    if (t == Tier::Minute) {
        if (back > kMinutes) return false;
        out = pm.minutes.slots[kMinutes - back];
        return out.n > 0;
    }
    if (t == Tier::Hour && back <= kHours) {
        out = pm.hours.slots[kHours - back];
        return out.n > 0;
    }
    const std::int64_t idx = (t == Tier::Hour ? hour_idx_ : day_idx_) - static_cast<std::int64_t>(back);
    return store_ != nullptr && store_->read_slot(t, idx, m, out) && out.n > 0;
}

void TieredHistory::restore(const HistoryStore& store) {
    ClosedInterval rec{};
    for (std::size_t back = 1; back <= kHours; ++back) {
        if (!store.read(Tier::Hour, hour_idx_ - static_cast<std::int64_t>(back), rec)) continue;
        for (std::size_t i = 0; i < kMetricCount; ++i) m_[i].hours.slots[kHours - back] = rec.slots[i];
    }
    // today so far = the stored hours since local midnight + what was sampled since this boot
    for (std::int64_t h = day_idx_ * 24; h < hour_idx_; ++h) {
        if (!store.read(Tier::Hour, h, rec)) continue;
        for (std::size_t i = 0; i < kMetricCount; ++i) merge_slot(metric_info(static_cast<Metric>(i)).agg, m_[i].acc_day, rec.slots[i]);
    }
}

const Slot& TieredHistory::current(Metric m, Tier t) const {
    const PerMetric& pm = m_[static_cast<std::size_t>(m)];
    return t == Tier::Minute ? pm.acc_min : (t == Tier::Hour ? pm.acc_hour : pm.acc_day);
}

const Slot& TieredHistory::slot(Metric m, Tier t, std::size_t i) const {
    static const Slot kEmpty{};
    const PerMetric& pm = m_[static_cast<std::size_t>(m)];
    if (t == Tier::Minute) {
        return pm.minutes.slots[std::min(i, kMinutes - 1)];
    }
    if (t == Tier::Hour) {
        return pm.hours.slots[std::min(i, kHours - 1)];
    }
    return kEmpty;
}

std::size_t TieredHistory::size(Tier t) {
    return t == Tier::Minute ? kMinutes : (t == Tier::Hour ? kHours : 0);
}

namespace {
Slot window(const TieredHistory& h, Metric m, Tier t, std::size_t n) {
    const Agg agg = metric_info(m).agg;
    Slot acc = h.current(m, t);
    const std::size_t sz = TieredHistory::size(t);
    const std::size_t take = std::min(n, sz);
    for (std::size_t i = sz - take; i < sz; ++i) {
        merge_slot(agg, acc, h.slot(m, t, i));
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

}  // namespace history
}  // namespace station
