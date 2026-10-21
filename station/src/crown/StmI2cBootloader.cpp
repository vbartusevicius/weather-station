#include "StmI2cBootloader.hpp"

#include <algorithm>

#include "crown_protocol.hpp"

namespace station {
namespace crown_link {

bool StmI2cBootloader::wait_ack(std::uint32_t timeout_ms) {
    for (std::uint32_t waited = 0; waited <= timeout_ms; waited += 5) {
        std::uint8_t r = 0;
        if (t_.read(addr_, &r, 1)) {
            if (r == kAck) return true;
            if (r == kNack) return false;
            // kBusy (no-stretch commands in progress) or bus noise: poll again
        }
        t_.delay_ms(5);
    }
    return false;
}

bool StmI2cBootloader::command(std::uint8_t cmd) {
    const std::uint8_t frame[2] = {cmd, static_cast<std::uint8_t>(~cmd)};
    return t_.write(addr_, frame, 2) && wait_ack(50);
}

bool StmI2cBootloader::send_with_checksum(const std::uint8_t* data, std::size_t len) {
    std::uint8_t buf[kBlock + 2];
    std::uint8_t x = 0;
    for (std::size_t i = 0; i < len; ++i) {
        buf[i] = data[i];
        x ^= data[i];
    }
    buf[len] = (len == 1) ? static_cast<std::uint8_t>(~data[0]) : x;
    return t_.write(addr_, buf, len + 1);
}

bool StmI2cBootloader::enter() {
    t_.set_boot0(true);
    t_.reset_pulse();
    t_.delay_ms(50);
    for (std::uint8_t a : {crown::kBootloaderAddrA, crown::kBootloaderAddrB}) {
        addr_ = a;
        if (command(kCmdGet)) {
            std::uint8_t info[32] = {};
            if (t_.read(addr_, info, 1) && info[0] < sizeof(info) - 1 && t_.read(addr_, info + 1, info[0] + 1u) &&
                wait_ack(50)) {
                return true;
            }
        }
    }
    addr_ = 0;
    return false;
}

bool StmI2cBootloader::get_id(std::uint16_t& pid) {
    if (!command(kCmdGetId)) return false;
    std::uint8_t b[3] = {};
    if (!t_.read(addr_, b, 3) || b[0] != 1) return false;   // N = 1 -> 2 bytes follow
    pid = static_cast<std::uint16_t>((b[1] << 8) | b[2]);
    return wait_ack(50);
}

bool StmI2cBootloader::mass_erase() {
    if (!command(kCmdEraseNs)) return false;
    const std::uint8_t global[2] = {0xFF, 0xFF};             // 0xFFFF = mass erase
    return send_with_checksum(global, 2) && wait_ack(30000);  // G431 128 KB: ~20 ms per page, polled with BUSY
}

bool StmI2cBootloader::write_block(std::uint32_t addr, const std::uint8_t* data, std::size_t len) {
    if (len == 0 || len > kBlock || (len % 4) != 0) return false;
    if (!command(kCmdWriteNs)) return false;
    const std::uint8_t a[4] = {static_cast<std::uint8_t>(addr >> 24), static_cast<std::uint8_t>(addr >> 16),
                               static_cast<std::uint8_t>(addr >> 8), static_cast<std::uint8_t>(addr)};
    if (!send_with_checksum(a, 4) || !wait_ack(50)) return false;
    std::uint8_t buf[kBlock + 1];
    buf[0] = static_cast<std::uint8_t>(len - 1);
    std::copy(data, data + len, buf + 1);
    return send_with_checksum(buf, len + 1) && wait_ack(200);
}

bool StmI2cBootloader::read_block(std::uint32_t addr, std::uint8_t* data, std::size_t len) {
    if (len == 0 || len > 256) return false;
    if (!command(kCmdRead)) return false;
    const std::uint8_t a[4] = {static_cast<std::uint8_t>(addr >> 24), static_cast<std::uint8_t>(addr >> 16),
                               static_cast<std::uint8_t>(addr >> 8), static_cast<std::uint8_t>(addr)};
    if (!send_with_checksum(a, 4) || !wait_ack(50)) return false;
    const std::uint8_t n = static_cast<std::uint8_t>(len - 1);
    if (!send_with_checksum(&n, 1) || !wait_ack(50)) return false;
    return t_.read(addr_, data, len);
}

void StmI2cBootloader::leave() {
    t_.set_boot0(false);
    t_.reset_pulse();
}

StmI2cBootloader::Result StmI2cBootloader::flash(const std::uint8_t* image, std::size_t len,
                                                 void (*progress)(std::size_t, std::size_t)) {
    if (len == 0 || len > 128U * 1024U) return Result::TooLarge;
    if (!enter()) {
        leave();
        return Result::NoBootloader;
    }
    std::uint16_t pid = 0;
    if (!get_id(pid) || pid != kG431ProductId) {
        leave();
        return Result::WrongChip;
    }
    if (!mass_erase()) {
        leave();
        return Result::EraseFailed;
    }
    std::uint8_t block[kBlock];
    std::uint8_t back[kBlock];
    for (std::size_t off = 0; off < len; off += kBlock) {
        const std::size_t n = std::min(kBlock, len - off);
        const std::size_t padded = (n + 3) & ~static_cast<std::size_t>(3);
        std::fill(block, block + kBlock, 0xFF);
        std::copy(image + off, image + off + n, block);
        if (!write_block(kFlashBase + static_cast<std::uint32_t>(off), block, padded)) {
            leave();
            return Result::WriteFailed;
        }
        if (!read_block(kFlashBase + static_cast<std::uint32_t>(off), back, padded) ||
            !std::equal(block, block + padded, back)) {
            leave();
            return Result::VerifyFailed;
        }
        if (progress) progress(off + n, len);
    }
    leave();
    return Result::Ok;
}

}  // namespace crown_link
}  // namespace station
