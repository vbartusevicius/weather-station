#pragma once
// Photodiode `dome` (hardware/circuits/variants/v1/dome.py): Genicom GUVB-C21SD (UV-B) and GUVA-C22SD (UV-A) at zero
// bias into TLV9062 transimpedance stages, read by an ADS1115 (I2C 0x49) differentially against the bias VB:
// V = I x Rf. Pure conversions (host-tested); the I2C side lives in firmware/drivers.
#include <cstdint>

namespace station {
namespace environment {
namespace uvpd {

constexpr float kRfUvbOhm = 10.0e6F;     // dome R1
constexpr float kRfUvaOhm = 4.7e6F;      // dome R2
// Genicom datasheets (Vr = 0 V, 25 C): GUVB-C21SD 1.4 nA per UV index in sunlight, 71 nA per mW/cm^2 at 306 nm;
// GUVA-C22SD 113 nA per mW/cm^2 at 352 nm (1 mW/cm^2 = 10 W/m^2).
constexpr float kUvbNaPerUvi = 1.4F;
constexpr float kUvbNaPerWm2 = 7.1F;
constexpr float kUvaNaPerWm2 = 11.3F;

// ADS1115 PGA full scales (V) and their config codes, smallest first.
constexpr float kFullScaleV[4] = {0.256F, 0.512F, 1.024F, 2.048F};
constexpr std::uint8_t kPgaCode[4] = {0b101, 0b100, 0b011, 0b010};

inline float na_from_volts(float volts, float rf_ohm) { return volts / rf_ohm * 1.0e9F; }

// Next PGA step for a channel after a reading of `volts` on step `current`: up when above 90 % of the full scale,
// down when the reading would fit below 40 % of the next smaller one.
std::uint8_t next_range(std::uint8_t current, float volts);

// Dark offset: learnt only at night (sun below -6 deg), as a slow average of the photocurrent; subtracted by day.
class DarkZero {
public:
    void update(float na, bool night);
    float apply(float na) const;
    float offset_na() const { return offset_na_; }

private:
    float offset_na_{0.0F};
    bool learnt_{false};
};

// Free-sky values from the dark-zeroed photocurrents behind the cover (`transmission`: PTFE ~0.11, JGS1 ~0.92).
// `uvb_na_per_uvi` > 0 is a field calibration in nA behind the cover per UV index (it already includes the cover).
float uv_index(float uvb_na, float transmission, float uvb_na_per_uvi);
float uvb_w_m2(float uvb_na, float transmission);
float uva_w_m2(float uva_na, float transmission);   // GUVA-C22SD band 240-370 nm (includes the small UV-B share)

}  // namespace uvpd
}  // namespace environment
}  // namespace station
