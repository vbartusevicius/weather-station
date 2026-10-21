#pragma once
// STM32 ROM bootloader client over I2C (ST AN4221, "no-stretch" commands), used by the ESP32 to (re)flash the crown
// STM32G431. Host-tested against a simulated bootloader; the firmware supplies the ESP-IDF transport.
//
// Sequence (StmI2cBootloader::flash): hold CROWN_BOOT0 high, pulse CROWN_NRST -> ROM bootloader on I2C2 ->
// find its address (kBootloaderAddrA / B) with GET -> mass erase -> write the image in 128-byte blocks from
// 0x08000000 -> read back and compare -> release BOOT0 -> reset into the new application.

#include <cstddef>
#include <cstdint>

namespace station {
namespace crown_link {

class BootTransport {
public:
    virtual ~BootTransport() = default;
    virtual bool write(std::uint8_t addr, const std::uint8_t* data, std::size_t len) = 0;
    virtual bool read(std::uint8_t addr, std::uint8_t* data, std::size_t len) = 0;
    virtual void delay_ms(std::uint32_t ms) = 0;
    virtual void set_boot0(bool high) = 0;
    virtual void reset_pulse() = 0;   // drive NRST low >= 1 ms, release, wait for the ROM to start
};

class StmI2cBootloader {
public:
    static constexpr std::uint8_t kAck = 0x79;
    static constexpr std::uint8_t kNack = 0x1F;
    static constexpr std::uint8_t kBusy = 0x76;
    static constexpr std::uint8_t kCmdGet = 0x00;
    static constexpr std::uint8_t kCmdGetId = 0x02;
    static constexpr std::uint8_t kCmdRead = 0x11;
    static constexpr std::uint8_t kCmdGo = 0x21;
    static constexpr std::uint8_t kCmdWriteNs = 0x32;
    static constexpr std::uint8_t kCmdEraseNs = 0x45;
    static constexpr std::uint32_t kFlashBase = 0x08000000U;
    static constexpr std::size_t kBlock = 128;
    static constexpr std::uint16_t kG431ProductId = 0x468;

    explicit StmI2cBootloader(BootTransport& t) : t_(t) {}

    enum class Result : std::uint8_t { Ok, NoBootloader, WrongChip, EraseFailed, WriteFailed, VerifyFailed, TooLarge };

    // `image` = raw .bin for 0x08000000. `progress(done, total)` may be null.
    Result flash(const std::uint8_t* image, std::size_t len, void (*progress)(std::size_t, std::size_t) = nullptr);

    // Lower-level steps (public for tests)
    bool enter();                       // BOOT0 high + reset, find the address
    bool get_id(std::uint16_t& pid);
    bool mass_erase();
    bool write_block(std::uint32_t addr, const std::uint8_t* data, std::size_t len);
    bool read_block(std::uint32_t addr, std::uint8_t* data, std::size_t len);
    void leave();                       // BOOT0 low + reset into the application
    std::uint8_t address() const { return addr_; }

private:
    bool command(std::uint8_t cmd);
    bool wait_ack(std::uint32_t timeout_ms);
    bool send_with_checksum(const std::uint8_t* data, std::size_t len);

    BootTransport& t_;
    std::uint8_t addr_{0};
};

}  // namespace crown_link
}  // namespace station
