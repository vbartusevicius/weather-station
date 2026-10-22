#include "WindCalibration.hpp"

#include <algorithm>
#include <cmath>

namespace station {
namespace wind {

float StillAirCalibrator::path_length_m() {
    const float half_d = 0.5F * UltrasonicWindSolver::kTransducerSeparationM;
    return 2.0F * std::sqrt(half_d * half_d + UltrasonicWindSolver::kReflectorGapM * UltrasonicWindSolver::kReflectorGapM);
}

float StillAirCalibrator::sound_speed_m_s(float air_temp_c, float rh_pct, float pressure_hpa) {
    // virtual temperature (moist air is lighter): Tv = T (1 + 0.32 e / p), c = sqrt(gamma R_d Tv)
    const float rh = std::min(100.0F, std::max(0.0F, rh_pct));
    const float p = std::min(1100.0F, std::max(500.0F, pressure_hpa));
    const float es = 6.112F * std::exp(17.67F * air_temp_c / (air_temp_c + 243.5F));
    const float tv = (air_temp_c + 273.15F) * (1.0F + 0.32F * (rh / 100.0F) * es / p);
    return std::sqrt(1.4F * 287.058F * tv);
}

void StillAirCalibrator::add(const TofQuad& q) {
    ++n_;
    sum_t_ += 0.25 * (static_cast<double>(q.t_ns_us) + q.t_sn_us + q.t_ew_us + q.t_we_us);
    const double dns = static_cast<double>(q.t_ns_us) - q.t_sn_us;
    const double dew = static_cast<double>(q.t_ew_us) - q.t_we_us;
    sum_dns_ += dns;
    sum_dns2_ += dns * dns;
    sum_dew_ += dew;
    sum_dew2_ += dew * dew;
}

StillAirResult StillAirCalibrator::result(float air_temp_c, float rh_pct, float pressure_hpa) const {
    StillAirResult r{};
    r.quads = n_;
    if (n_ < kMinQuads) {
        r.reason = "too few quads (need 30 s of wind data)";
        return r;
    }
    const double n = static_cast<double>(n_);
    const double mns = sum_dns_ / n, mew = sum_dew_ / n;
    const double sd = std::sqrt(std::max(0.0, 0.5 * ((sum_dns2_ / n - mns * mns) + (sum_dew2_ / n - mew * mew))));
    r.dt_spread_us = static_cast<float>(sd);
    const float t_expected_us = path_length_m() / sound_speed_m_s(air_temp_c, rh_pct, pressure_hpa) * 1e6F;
    r.us_delay_us = static_cast<float>(sum_t_ / n) - t_expected_us;
    r.zero.dt_ns_offset_us = static_cast<float>(mns);
    r.zero.dt_ew_offset_us = static_cast<float>(mew);
    if (r.dt_spread_us > kMaxDtSpreadUs) {
        r.reason = "air not still (reciprocal spread too large): cover the station and retry";
        return r;
    }
    if (r.us_delay_us < kMinDelayUs || r.us_delay_us > kMaxDelayUs) {
        r.reason = "delay out of range: check the transducers / temperature reading";
        return r;
    }
    r.ok = true;
    r.reason = "ok";
    return r;
}

}  // namespace wind
}  // namespace station
