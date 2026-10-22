#pragma once
// Crown (STM32G431, I2C2 peripheral) <-> main (ESP32-C6) register protocol. Shared by both firmwares.
//
// Transport: plain I2C register access at address kI2cAddress, 100-400 kHz over the 260 mm crown cable.
//   write: [reg] [payload...]            read: write [reg], repeated start, read N bytes
// Every block that is read as a whole ends with a CRC-8 (poly 0x31, init 0xFF, Sensirion/SMBus style) so a
// corrupted read over the cable is detected and dropped instead of becoming a bogus wind gust.
// Multi-byte fields are little-endian. The crown raises CROWN_IRQ (active high) while kStatus has unread data.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crown {

constexpr std::uint8_t kI2cAddress = 0x2A;
constexpr std::uint8_t kWhoAmIValue = 0xC7;
constexpr std::uint8_t kProtocolVersion = 2;   // v2: kStatus carries a CRC

// STM32G431 ROM bootloader on I2C2 (PC4 / PA8), entered with BOOT0 high during reset (AN2606, AN4221).
// AN2606 lists the 7-bit address per device family; the station scans both candidates before flashing.
constexpr std::uint8_t kBootloaderAddrA = 0x52;
constexpr std::uint8_t kBootloaderAddrB = 0x56;

enum Reg : std::uint8_t {
    kWhoAmI = 0x00,      // 1 B  = kWhoAmIValue
    kVersion = 0x01,     // 4 B  = protocol, fw major, fw minor, fw patch
    kStatus = 0x02,      // 3 B  = StatusBits (LE) + CRC-8 (read-only)
    kWind = 0x10,        // sizeof(WindBlock)
    kRainCount = 0x30,   // 1 B  = rain events waiting in the FIFO
    kRainPop = 0x31,     // sizeof(RainEvent): oldest rain event (removes it)
    kStrikeCount = 0x50, // 1 B
    kStrikePop = 0x51,   // sizeof(StrikeEvent)
    kDiag = 0x60,        // sizeof(DiagBlock)
    kConfig = 0x70,      // sizeof(ConfigBlock), read / write
    kCommand = 0x7E,     // 1 B write: Command
};

enum StatusBits : std::uint16_t {
    kStWindNew = 1U << 0,      // a new WindBlock since the last kWind read
    kStRainPending = 1U << 1,
    kStStrikePending = 1U << 2,
    kStAs3935Ok = 1U << 3,
    kStAs3935Tuned = 1U << 4,
    kStTuning = 1U << 5,       // AS3935 antenna tuning in progress
    kStDriveLow = 1U << 6,     // +12V_HEAT drive rail measured below 9 V
    kStWatchdogReset = 1U << 7,
};

enum Command : std::uint8_t {
    kCmdNone = 0x00,
    kCmdTuneAntenna = 0x02,    // re-run the AS3935 500 kHz tank tuning (zero-wind offsets live on the station)
    kCmdClearFifos = 0x03,
    kCmdReset = 0x5A,
    kCmdEnterBootloader = 0xB0,  // jump to the ROM bootloader without the BOOT0 line
};

enum PathFlags : std::uint8_t {
    kPathOk = 0,
    kPathNoEcho = 1U << 0,     // envelope never left the noise floor
    kPathClipped = 1U << 1,    // ADC saturated (gain stepped down for the next burst)
    kPathLowCorr = 1U << 2,    // reciprocal waveforms correlate < 0.8: turbulence / rain splash / obstruction
    kPathEarly = 1U << 3,      // arrival before the geometric minimum (direct-path leak or ringing)
};

#pragma pack(push, 1)
// One reciprocal quad: times of flight in picoseconds after the drive edge, already corrected for the per-path
// electronic delay measured at calibration. Wind is solved on the station (wind::UltrasonicWindSolver).
struct WindBlock {
    std::uint32_t seq;          // increments per quad
    std::uint32_t tof_ps[4];    // N->S, S->N, E->W, W->E
    std::uint8_t snr_db[4];     // envelope peak / pre-arrival RMS
    std::uint8_t corr_pct[2];   // reciprocal correlation (NS pair, EW pair), 0..100
    std::uint8_t gain_code;     // PGA gain index (0 = x2 .. 4 = x32)
    std::uint8_t flags[4];      // PathFlags per path
    std::uint8_t crc;
};

struct RainEvent {
    std::uint32_t age_ms;       // ms between the impact and the read (station converts to its own clock)
    std::uint16_t peak_mv;      // charge-amplifier deviation from its baseline at the peak (mV)
    std::uint16_t width_us;     // time above the trigger threshold
    std::uint8_t flags;         // bit0: clipped (hail / saturation)
    std::uint8_t crc;
};

struct StrikeEvent {
    std::uint32_t age_ms;
    std::uint8_t kind;          // 1 = lightning, 2 = disturber, 3 = noise level too high
    std::uint8_t distance_km;   // AS3935 storm-front estimate (1 = overhead, 63 = out of range)
    std::uint32_t energy;       // AS3935 21-bit energy
    std::uint8_t during_burst;  // 1 if the interrupt came while a transducer burst was being driven
    std::uint8_t crc;
};

struct DiagBlock {
    std::uint32_t uptime_s;
    std::uint16_t drive_mv;     // +12V_HEAT at the drivers (ADC, 1:6 divider not fitted in V1 -> 0)
    std::int16_t mcu_temp_c10;  // STM32 internal sensor, 0.1 C
    std::uint8_t tun_cap;       // AS3935 TUN_CAP chosen by the tuning routine (0..15)
    std::uint16_t lco_hz10;     // measured antenna resonance / 10 (Hz), target 50000
    std::uint16_t i2c_errors;
    std::uint16_t bursts_per_s;
    std::uint8_t crc;
};

struct ConfigBlock {
    std::uint8_t quads_per_s;      // 1..10 (default 4); 0 = bursts paused (lightning interference test)
    std::uint8_t burst_cycles;     // 4..16 (default 8)
    std::uint8_t as3935_outdoor;   // 1 = outdoor AFE gain
    std::uint8_t as3935_min_strikes;
    std::uint8_t rain_trigger_mv;  // charge-amp deviation that starts an impact (default 40 mV)
    std::uint8_t reserved[3];
    std::uint8_t crc;
};
#pragma pack(pop)

static_assert(sizeof(WindBlock) == 32, "WindBlock layout");
static_assert(sizeof(RainEvent) == 10, "RainEvent layout");
static_assert(sizeof(StrikeEvent) == 12, "StrikeEvent layout");

inline std::uint8_t crc8(const std::uint8_t* data, std::size_t len) {
    std::uint8_t crc = 0xFF;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = static_cast<std::uint8_t>((crc & 0x80U) ? ((crc << 1) ^ 0x31U) : (crc << 1));
        }
    }
    return crc;
}

// Seal / check a packed block whose last byte is `crc`.
template <typename T>
inline void seal(T& block) {
    auto* p = reinterpret_cast<std::uint8_t*>(&block);
    p[sizeof(T) - 1] = crc8(p, sizeof(T) - 1);
}

template <typename T>
inline bool valid(const T& block) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(&block);
    return p[sizeof(T) - 1] == crc8(p, sizeof(T) - 1);
}

template <typename T>
inline bool decode(const std::uint8_t* raw, std::size_t len, T& out) {
    if (len != sizeof(T)) {
        return false;
    }
    std::memcpy(&out, raw, sizeof(T));
    return valid(out);
}

// Path order in WindBlock::tof_ps (matches wind::TofQuad).
enum Path : std::uint8_t { kNS = 0, kSN = 1, kEW = 2, kWE = 3 };

}  // namespace crown
