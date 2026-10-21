#include "Sensors.hpp"

#include <algorithm>
#include <cmath>

#include <freertos/task.h>

namespace station {
namespace firmware {

namespace {
void delay_ms(std::uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

std::uint16_t be16(const std::uint8_t* p) { return static_cast<std::uint16_t>((p[0] << 8) | p[1]); }

bool sensirion_cmd(I2cBus::Device& d, std::uint16_t cmd) {
    const std::uint8_t b[2] = {static_cast<std::uint8_t>(cmd >> 8), static_cast<std::uint8_t>(cmd & 0xFF)};
    return d.write(b, 2);
}

bool sensirion_cmd_word(I2cBus::Device& d, std::uint16_t cmd, std::uint16_t word) {
    std::uint8_t b[5] = {static_cast<std::uint8_t>(cmd >> 8), static_cast<std::uint8_t>(cmd & 0xFF),
                         static_cast<std::uint8_t>(word >> 8), static_cast<std::uint8_t>(word & 0xFF), 0};
    b[4] = sensirion_crc8(b + 2, 2);
    return d.write(b, 5);
}

// Reads `n` CRC-protected 16-bit words.
bool sensirion_read_words(I2cBus::Device& d, std::uint16_t* words, int n) {
    std::uint8_t buf[30];
    if (n * 3 > static_cast<int>(sizeof(buf)) || !d.read(buf, static_cast<std::size_t>(n * 3))) {
        return false;
    }
    for (int i = 0; i < n; ++i) {
        if (sensirion_crc8(buf + i * 3, 2) != buf[i * 3 + 2]) {
            return false;
        }
        words[i] = be16(buf + i * 3);
    }
    return true;
}
}  // namespace

// ----------------------------------------------------------------------------------------- SHT4x
bool Sht4x::begin(I2cBus& bus, std::uint8_t addr) {
    present_ = bus.probe(addr);
    if (present_) {
        dev_ = bus.add(addr);
        const std::uint8_t reset = 0x94;
        dev_.write(&reset, 1);
        delay_ms(2);
    }
    return present_;
}

bool Sht4x::read_result(std::uint8_t cmd, std::uint32_t wait_ms, float& temp_c, float& rh_pct) {
    if (!present_ || !dev_.write(&cmd, 1)) {
        return false;
    }
    delay_ms(wait_ms);
    std::uint8_t b[6];
    if (!dev_.read(b, 6) || sensirion_crc8(b, 2) != b[2] || sensirion_crc8(b + 3, 2) != b[5]) {
        return false;
    }
    temp_c = -45.0F + 175.0F * static_cast<float>(be16(b)) / 65535.0F;
    rh_pct = std::min(100.0F, std::max(0.0F, -6.0F + 125.0F * static_cast<float>(be16(b + 3)) / 65535.0F));
    return true;
}

bool Sht4x::measure(float& temp_c, float& rh_pct) {
    return read_result(0xFD, 10, temp_c, rh_pct);
}

bool Sht4x::heater_pulse(float& temp_c, float& rh_pct) {
    return read_result(0x39, 1110, temp_c, rh_pct);
}

// ---------------------------------------------------------------------------------------- BMP581
namespace bmp {
constexpr std::uint8_t kChipId = 0x01;
constexpr std::uint8_t kFifoConfig = 0x16;
constexpr std::uint8_t kFifoCount = 0x17;
constexpr std::uint8_t kFifoSel = 0x18;
constexpr std::uint8_t kTempData = 0x1D;
constexpr std::uint8_t kFifoData = 0x29;
constexpr std::uint8_t kDspIir = 0x31;
constexpr std::uint8_t kOsrConfig = 0x36;
constexpr std::uint8_t kOdrConfig = 0x37;
constexpr std::uint8_t kCmd = 0x7E;
constexpr std::uint8_t kOdr100Hz = 0x0A;  // 100.2 Hz
}  // namespace bmp

bool Bmp581::begin(I2cBus& bus, std::uint8_t addr) {
    if (!bus.probe(addr)) {
        return false;
    }
    dev_ = bus.add(addr);
    dev_.write_reg(bmp::kCmd, 0xB6);  // soft reset
    delay_ms(3);
    std::uint8_t id = 0;
    if (!dev_.read_regs(bmp::kChipId, &id, 1) || (id != 0x50 && id != 0x51)) {
        return false;
    }
    dev_.write_reg(bmp::kOdrConfig, 0x80);  // deep standby disabled, standby (FIFO is configured in standby)
    delay_ms(3);
    dev_.write_reg(bmp::kOsrConfig, 0x40);  // pressure enabled, OSR_P = OSR_T = 1x (100 Hz needs low OSR)
    dev_.write_reg(bmp::kDspIir, 0x00);     // no IIR: thunder impulses must stay sharp
    dev_.write_reg(bmp::kFifoSel, 0x02);    // pressure-only frames, no decimation
    dev_.write_reg(bmp::kFifoConfig, 0x00); // streaming mode
    present_ = dev_.write_reg(bmp::kOdrConfig, static_cast<std::uint8_t>(0x80 | (bmp::kOdr100Hz << 2) | 0x01));
    return present_;
}

int Bmp581::read_fifo(float* out, int max_samples) {
    if (!present_) {
        return 0;
    }
    std::uint8_t count = 0;
    if (!dev_.read_regs(bmp::kFifoCount, &count, 1)) {
        return 0;
    }
    const int frames = std::min<int>(count & 0x3F, std::min(max_samples, 32));
    if (frames <= 0) {
        return 0;
    }
    std::uint8_t buf[96];
    if (!dev_.read_regs(bmp::kFifoData, buf, static_cast<std::size_t>(frames * 3))) {
        return 0;
    }
    int n = 0;
    for (int i = 0; i < frames; ++i) {
        const std::uint32_t raw = static_cast<std::uint32_t>(buf[i * 3]) | (static_cast<std::uint32_t>(buf[i * 3 + 1]) << 8) |
                                  (static_cast<std::uint32_t>(buf[i * 3 + 2]) << 16);
        if (raw == 0x7F7F7FU || raw == 0) {
            continue;  // empty frame
        }
        out[n++] = static_cast<float>(raw) / 64.0F;
    }
    return n;
}

bool Bmp581::read_temperature(float& temp_c) {
    std::uint8_t b[3];
    if (!present_ || !dev_.read_regs(bmp::kTempData, b, 3)) {
        return false;
    }
    std::int32_t raw = static_cast<std::int32_t>(b[0] | (b[1] << 8) | (b[2] << 16));
    if (raw & 0x800000) {
        raw -= 0x1000000;
    }
    temp_c = static_cast<float>(raw) / 65536.0F;
    return true;
}

// ----------------------------------------------------------------------------------------- SCD41
bool Scd41::begin(I2cBus& bus, std::uint8_t addr) {
    present_ = bus.probe(addr);
    if (!present_) {
        return false;
    }
    dev_ = bus.add(addr);
    stop();
    return start();
}

bool Scd41::start() {
    running_ = present_ && sensirion_cmd(dev_, 0x21B1);
    return running_;
}

bool Scd41::stop() {
    const bool ok = present_ && sensirion_cmd(dev_, 0x3F86);
    delay_ms(500);
    running_ = false;
    return ok;
}

bool Scd41::data_ready() {
    std::uint16_t w = 0;
    if (!running_ || !sensirion_cmd(dev_, 0xE4B8)) {
        return false;
    }
    delay_ms(2);
    return sensirion_read_words(dev_, &w, 1) && (w & 0x07FF) != 0;
}

bool Scd41::read(float& co2_ppm, float& temp_c, float& rh_pct) {
    std::uint16_t w[3];
    if (!running_ || !sensirion_cmd(dev_, 0xEC05)) {
        return false;
    }
    delay_ms(2);
    if (!sensirion_read_words(dev_, w, 3) || w[0] == 0) {
        return false;
    }
    co2_ppm = static_cast<float>(w[0]);
    temp_c = -45.0F + 175.0F * static_cast<float>(w[1]) / 65535.0F;
    rh_pct = 100.0F * static_cast<float>(w[2]) / 65535.0F;
    return true;
}

bool Scd41::set_ambient_pressure(float hpa) {
    if (!running_ || hpa < 700.0F || hpa > 1200.0F) {
        return false;
    }
    return sensirion_cmd_word(dev_, 0xE000, static_cast<std::uint16_t>(std::lround(hpa)));
}

// -------------------------------------------------------------------------------------- PMSA003I
bool Pmsa003i::begin(I2cBus& bus, std::uint8_t addr, std::uint32_t now_ms) {
    present_ = bus.probe(addr);
    if (present_) {
        dev_ = bus.add(addr, 100000);  // datasheet: 100 kHz standard-mode I2C
        started_ms_ = now_ms;
    }
    return present_;
}

bool Pmsa003i::read(std::uint32_t now_ms, PmData& out) {
    if (!present_ || now_ms - started_ms_ < kWarmupMs) {
        return false;
    }
    std::uint8_t f[32];
    if (!dev_.read_regs(0x00, f, sizeof(f)) || f[0] != 0x42 || f[1] != 0x4D || ((f[2] << 8) | f[3]) != 28) {
        return false;
    }
    std::uint16_t sum = 0;
    for (std::size_t i = 0; i < 30; ++i) {
        sum = static_cast<std::uint16_t>(sum + f[i]);
    }
    if (sum != static_cast<std::uint16_t>((f[30] << 8) | f[31]) || f[29] != 0) {  // checksum, error code
        return false;
    }
    auto word = [&f](std::size_t i) { return static_cast<float>((f[i] << 8) | f[i + 1]); };
    out.pm1 = word(10);   // data 4: PM1.0 atmospheric
    out.pm25 = word(12);  // data 5: PM2.5 atmospheric
    out.pm10 = word(14);  // data 6: PM10 atmospheric
    return true;
}

// --------------------------------------------------------------------------------------- ADS1115
bool Ads1115::begin(I2cBus& bus, std::uint8_t addr) {
    present_ = bus.probe(addr);
    if (present_) {
        dev_ = bus.add(addr);
    }
    return present_;
}

bool Ads1115::start(std::uint8_t channel, Range range) {
    if (!present_ || channel > 3) {
        return false;
    }
    // OS=1 (start), MUX=1xx (AINx vs GND), PGA, MODE=1 (single-shot), DR=001 (16 SPS), comparator disabled
    const std::uint16_t cfg = static_cast<std::uint16_t>(0x8000U | ((0x4U | channel) << 12) |
                                                         (static_cast<std::uint16_t>(range) << 9) | 0x0100U |
                                                         (0x1U << 5) | 0x0003U);
    const std::uint8_t b[3] = {0x01, static_cast<std::uint8_t>(cfg >> 8), static_cast<std::uint8_t>(cfg & 0xFF)};
    fs_v_ = range == Range::Fs256mV ? 0.256F : 4.096F;
    return dev_.write(b, sizeof(b));
}

bool Ads1115::read(float& volts) {
    std::uint8_t b[2];
    if (!present_ || !dev_.read_regs(0x00, b, sizeof(b))) {
        return false;
    }
    const auto raw = static_cast<std::int16_t>((b[0] << 8) | b[1]);
    volts = static_cast<float>(raw) * fs_v_ / 32768.0F;
    return true;
}

// ----------------------------------------------------------------------------------------- SGP41
namespace {
std::uint16_t rh_ticks(float rh) { return static_cast<std::uint16_t>(std::lround(std::min(100.0F, std::max(0.0F, rh)) * 65535.0F / 100.0F)); }
std::uint16_t t_ticks(float t) { return static_cast<std::uint16_t>(std::lround((std::min(130.0F, std::max(-45.0F, t)) + 45.0F) * 65535.0F / 175.0F)); }
}  // namespace

bool Sgp41::begin(I2cBus& bus, std::uint8_t addr) {
    present_ = bus.probe(addr);
    if (present_) {
        dev_ = bus.add(addr);
    }
    conditioned_s_ = 0;
    return present_;
}

bool Sgp41::measure(bool comp_ok, float rh_pct, float temp_c, std::uint16_t& sraw_voc, std::uint16_t& sraw_nox, bool& nox_ok) {
    if (!present_) {
        return false;
    }
    // Without a valid SHT40 reading use Sensirion's defaults (50 % RH, 25 C).
    const std::uint16_t rh = comp_ok ? rh_ticks(rh_pct) : 0x8000;
    const std::uint16_t t = comp_ok ? t_ticks(temp_c) : 0x6666;
    const bool conditioning = conditioned_s_ < kConditioningSeconds;
    const std::uint16_t cmd = conditioning ? 0x2612 : 0x2619;  // execute_conditioning / measure_raw_signals
    std::uint8_t b[8] = {static_cast<std::uint8_t>(cmd >> 8), static_cast<std::uint8_t>(cmd & 0xFF),
                         static_cast<std::uint8_t>(rh >> 8), static_cast<std::uint8_t>(rh & 0xFF), 0,
                         static_cast<std::uint8_t>(t >> 8), static_cast<std::uint8_t>(t & 0xFF), 0};
    b[4] = sensirion_crc8(b + 2, 2);
    b[7] = sensirion_crc8(b + 5, 2);
    if (!dev_.write(b, sizeof(b))) {
        return false;
    }
    delay_ms(50);
    std::uint16_t w[2]{};
    if (!sensirion_read_words(dev_, w, conditioning ? 1 : 2)) {
        return false;
    }
    sraw_voc = w[0];
    sraw_nox = w[1];
    nox_ok = !conditioning;
    if (conditioning) {
        ++conditioned_s_;
    }
    return true;
}

// --------------------------------------------------------------------------------------- LSM6DS3
bool Lsm6ds3::begin(I2cBus& bus, std::uint8_t addr) {
    if (!bus.probe(addr)) {
        return false;
    }
    dev_ = bus.add(addr);
    std::uint8_t who = 0;
    if (!dev_.read_regs(0x0F, &who, 1) || who != 0x6A) {
        return false;
    }
    dev_.write_reg(0x12, 0x44);  // CTRL3_C: BDU + register auto-increment
    present_ = dev_.write_reg(0x10, 0x60);  // CTRL1_XL: 416 Hz, +/-2 g
    return present_;
}

bool Lsm6ds3::read_accel(core::Vec3& g) {
    std::uint8_t b[6];
    if (!present_ || !dev_.read_regs(0x28, b, 6)) {
        return false;
    }
    constexpr float kMgPerLsb = 0.061F;
    g.x = static_cast<float>(static_cast<std::int16_t>(b[0] | (b[1] << 8))) * kMgPerLsb / 1000.0F;
    g.y = static_cast<float>(static_cast<std::int16_t>(b[2] | (b[3] << 8))) * kMgPerLsb / 1000.0F;
    g.z = static_cast<float>(static_cast<std::int16_t>(b[4] | (b[5] << 8))) * kMgPerLsb / 1000.0F;
    return true;
}

// -------------------------------------------------------------------------------------- MMC5603
bool Mmc5603::begin(I2cBus& bus, std::uint8_t addr) {
    if (!bus.probe(addr)) {
        return false;
    }
    dev_ = bus.add(addr);
    std::uint8_t pid = 0;
    if (!dev_.read_regs(0x39, &pid, 1) || pid != 0x10) {
        return false;
    }
    dev_.write_reg(0x1C, 0x00);  // Control1: BW 6.6 ms
    present_ = true;
    return true;
}

bool Mmc5603::read(core::Vec3& field_ut) {
    if (!present_) {
        return false;
    }
    if ((reads_++ % 100U) == 0U) {
        dev_.write_reg(0x1B, 0x08);  // SET pulse: removes sensor offset drift / magnetisation
        delay_ms(1);
    }
    dev_.write_reg(0x1B, 0x01);  // TM_M: one magnetic measurement
    std::uint8_t st = 0;
    for (int i = 0; i < 10; ++i) {
        delay_ms(2);
        if (dev_.read_regs(0x18, &st, 1) && (st & 0x40)) {
            break;
        }
    }
    if ((st & 0x40) == 0) {
        return false;
    }
    std::uint8_t b[9];
    if (!dev_.read_regs(0x00, b, 9)) {
        return false;
    }
    auto axis = [](std::uint8_t hi, std::uint8_t mid, std::uint8_t lo4) {
        const std::uint32_t raw = (static_cast<std::uint32_t>(hi) << 12) | (static_cast<std::uint32_t>(mid) << 4) | (lo4 >> 4);
        return (static_cast<float>(raw) - 524288.0F) / 16384.0F * 100.0F;  // 16384 counts / gauss
    };
    field_ut.x = axis(b[0], b[1], b[6]);
    field_ut.y = axis(b[2], b[3], b[7]);
    field_ut.z = axis(b[4], b[5], b[8]);
    return true;
}

// --------------------------------------------------------------------------------------- LTR390
bool Ltr390::begin(I2cBus& bus, std::uint8_t addr) {
    if (!bus.probe(addr)) {
        return false;
    }
    dev_ = bus.add(addr);
    std::uint8_t id = 0;
    if (!dev_.read_regs(0x06, &id, 1) || (id & 0xF0) != 0xB0) {
        return false;
    }
    present_ = configure(true);
    return present_;
}

bool Ltr390::configure(bool uvs) {
    uvs_mode_ = uvs;
    dev_.write_reg(0x04, 0x22);                              // 18-bit, 100 ms
    dev_.write_reg(0x05, uvs ? 0x04 : 0x00);                  // UVS 18x, ALS 1x (no saturation to 157 klx)
    return dev_.write_reg(0x00, uvs ? 0x0A : 0x02);           // enable, UVS / ALS mode
}

bool Ltr390::step(bool& is_uvs, float& value) {
    if (!present_) {
        return false;
    }
    std::uint8_t st = 0;
    bool produced = false;
    if (dev_.read_regs(0x07, &st, 1) && (st & 0x08)) {
        std::uint8_t b[3];
        if (dev_.read_regs(uvs_mode_ ? 0x10 : 0x0D, b, 3)) {
            const std::uint32_t counts = (b[0] | (b[1] << 8) | (b[2] << 16)) & 0xFFFFFU;
            is_uvs = uvs_mode_;
            value = uvs_mode_ ? environment::MetCalculator::uv_index_from_ltr390(counts, 18.0F, 0.25F)
                              : environment::MetCalculator::lux_from_ltr390_als(counts, 1.0F, 100.0F);
            produced = true;
        }
    }
    configure(!uvs_mode_);
    return produced;
}

// -------------------------------------------------------------------------------------- TSL2591
namespace tsl {
constexpr std::uint8_t kCmd = 0xA0;
constexpr std::uint8_t kEnable = kCmd | 0x00;
constexpr std::uint8_t kControl = kCmd | 0x01;
constexpr std::uint8_t kId = kCmd | 0x12;
constexpr std::uint8_t kStatus = kCmd | 0x13;
constexpr std::uint8_t kC0Low = kCmd | 0x14;
constexpr float kAtimeMs = 100.0F;
constexpr std::uint16_t kSaturation = 37888;  // 100 ms integration
}  // namespace tsl

bool Tsl2591::begin(I2cBus& bus, std::uint8_t addr) {
    if (!bus.probe(addr)) {
        return false;
    }
    dev_ = bus.add(addr);
    std::uint8_t id = 0;
    if (!dev_.read_regs(tsl::kId, &id, 1) || id != 0x50) {
        return false;
    }
    dev_.write_reg(tsl::kEnable, 0x03);  // PON + AEN
    present_ = set_gain(gain_);
    return present_;
}

bool Tsl2591::set_gain(environment::Tsl2591Gain g) {
    gain_ = g;
    return dev_.write_reg(tsl::kControl, static_cast<std::uint8_t>(g));  // ATIME = 0 (100 ms)
}

bool Tsl2591::read(float& lux, float& nir_ratio) {
    if (!present_) {
        return false;
    }
    std::uint8_t st = 0;
    if (!dev_.read_regs(tsl::kStatus, &st, 1) || (st & 0x01) == 0) {
        return false;
    }
    std::uint8_t b[4];
    if (!dev_.read_regs(tsl::kC0Low, b, 4)) {
        return false;
    }
    const std::uint16_t ch0 = static_cast<std::uint16_t>(b[0] | (b[1] << 8));
    const std::uint16_t ch1 = static_cast<std::uint16_t>(b[2] | (b[3] << 8));
    const environment::Tsl2591Gain used = gain_;
    const auto cmd = sched_.tick_500ms(ch0 >= tsl::kSaturation ? 65535 : ch0);
    if (cmd.tsl2591_gain_changed) {
        set_gain(cmd.tsl2591_next_gain);
    }
    if (ch0 >= tsl::kSaturation || ch1 >= tsl::kSaturation) {
        return false;  // saturated: the LTR-390 ALS covers bright sunlight
    }
    const float again = environment::DomeOpticalScheduler::gain_multiplier(used);
    const float cpl = (tsl::kAtimeMs * again) / 408.0F;
    if (ch0 == 0) {
        lux = 0.0F;
        nir_ratio = 0.0F;
        return true;
    }
    const float c0 = static_cast<float>(ch0);
    const float c1 = static_cast<float>(ch1);
    lux = std::max(0.0F, (c0 - c1) * (1.0F - c1 / c0) / cpl);
    nir_ratio = c1 / c0;
    return true;
}

// ------------------------------------------------------------------------------------ GUVB-C31SM
namespace guvb {
constexpr std::uint8_t kChipId = 0x00;
constexpr std::uint8_t kMode = 0x01;
constexpr std::uint8_t kResUv = 0x04;
constexpr std::uint8_t kRangeUvb = 0x07;
constexpr std::uint8_t kSoftReset = 0x0B;
constexpr std::uint8_t kUvbLsb = 0x17;
constexpr std::uint8_t kNvmCtl = 0x30;
constexpr std::uint8_t kNvmMsb = 0x31;
constexpr std::uint8_t kNvmLsb = 0x32;
constexpr std::uint8_t kModeUvbNormal = 0x20;  // OPER[1:0] = 10 (UVB), PMODE[1:0] = 00 (normal)
constexpr std::uint8_t kRes100ms = 0x03;       // power-on default
constexpr std::uint8_t kDefaultRange = 3;      // x8, power-on default (reference for the factory calibration)
constexpr std::uint16_t kOverflow = 65000;
constexpr std::uint16_t kTooLow = 1500;
}  // namespace guvb

bool GuvbC31sm::reg_read(std::uint8_t reg, std::uint8_t* out, std::size_t len) {
    // Datasheet read sequence: register address write terminated by STOP, then a separate read.
    return dev_.write(&reg, 1) && dev_.read(out, len);
}

bool GuvbC31sm::begin(I2cBus& bus, std::uint8_t addr) {
    if (!bus.probe(addr)) {
        return false;
    }
    dev_ = bus.add(addr);
    std::uint8_t id = 0;
    if (!reg_read(guvb::kChipId, &id, 1) || id != 0x62) {
        return false;
    }
    dev_.write_reg(guvb::kMode, guvb::kModeUvbNormal);  // soft reset is only allowed in normal mode
    dev_.write_reg(guvb::kSoftReset, 0xA5);
    delay_ms(5);
    dev_.write_reg(guvb::kMode, guvb::kModeUvbNormal);
    dev_.write_reg(guvb::kResUv, guvb::kRes100ms);
    range_ = guvb::kDefaultRange;
    dev_.write_reg(guvb::kRangeUvb, range_);
    // Factory calibration in NVM: 0x0A = dark offset (8 bit, in NVM_MSB), 0x0C = B_Scale (16 bit)
    std::uint8_t msb = 0, lsb = 0;
    dev_.write_reg(guvb::kNvmCtl, 0x0A);
    reg_read(guvb::kNvmMsb, &msb, 1);
    offset_ = msb;
    dev_.write_reg(guvb::kNvmCtl, 0x0C);
    reg_read(guvb::kNvmMsb, &msb, 1);
    reg_read(guvb::kNvmLsb, &lsb, 1);
    b_scale_ = static_cast<std::uint16_t>((msb << 8) | lsb);
    present_ = true;
    skip_next_ = true;
    return true;
}

bool GuvbC31sm::read(float counts_per_uvi, float& counts, float& uvi, bool& uvi_ok) {
    uvi_ok = false;
    if (!present_) {
        return false;
    }
    std::uint8_t b[2];
    if (!reg_read(guvb::kUvbLsb, b, 2)) {
        return false;
    }
    if (skip_next_) {  // first conversion after a range change mixes both ranges
        skip_next_ = false;
        return false;
    }
    const std::uint16_t raw = static_cast<std::uint16_t>(b[0] | (b[1] << 8));
    if (raw >= guvb::kOverflow && range_ < 7) {
        dev_.write_reg(guvb::kRangeUvb, ++range_);
        skip_next_ = true;
        return false;  // saturated: retry on the less sensitive range
    }
    // Higher range code = half the counts per step ("x128 is half of x64"): normalise to x8.
    const float norm = static_cast<float>(raw) * static_cast<float>(1U << range_) / 8.0F;
    counts = std::max(0.0F, norm - static_cast<float>(offset_));
    const float scale = counts_per_uvi > 0.0F ? counts_per_uvi
                        : (b_scale_ != 0 && b_scale_ != 0xFFFF ? static_cast<float>(b_scale_) : 0.0F);
    if (scale > 0.0F) {
        uvi = counts / scale;
        uvi_ok = true;
    }
    if (raw < guvb::kTooLow && range_ > 0) {
        dev_.write_reg(guvb::kRangeUvb, --range_);  // more sensitivity for the next reading
        skip_next_ = true;
    }
    return true;
}

}  // namespace firmware
}  // namespace station
