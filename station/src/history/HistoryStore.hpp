#pragma once
// Long-term history on raw flash (the "storage" data partition, no file system). RAM keeps only the last 60
// minutes, a 24-h cache and the running intervals (TieredHistory); every closed hour and day is appended here.
//
// Layout (4 KB sectors): sector 0 = superblock (layout / format version; anything else -> the partition is erased
// and re-initialised), then two independent rings of fixed-size records:
//   hourly ring  kHourSectors sectors -> >= 14 days of hours,   daily ring  kDaySectors sectors -> >= 6 months of days.
// A record = header (magic, type, seq, interval index) + one Slot per metric + the interval's wind-rose bucket +
// CRC-32. Records never span sectors. Writing into a new sector erases it first (dropping its oldest records), so a
// ring keeps (sectors - 1) x records-per-sector records. A power cut can only lose the record being written: torn
// or foreign bytes fail the CRC and are skipped, and the next write goes to the next blank slot.
// Wear: one 4 KB erase per 6 records: each hourly sector is erased every 2 weeks, each daily sector every 6 months
// (~26 and ~2 erases a year against ~100 000 rated).

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Climate.hpp"
#include "TieredHistory.hpp"

namespace station {
namespace history {

class FlashDevice {
public:
    static constexpr std::size_t kSector = 4096;
    virtual ~FlashDevice() = default;
    virtual std::size_t size() const = 0;
    virtual bool read(std::size_t offset, void* buf, std::size_t len) const = 0;
    virtual bool write(std::size_t offset, const void* buf, std::size_t len) = 0;   // NOR: only clears bits
    virtual bool erase_sector(std::size_t offset) = 0;                              // sets the sector to 0xFF
};

// RAM-backed flash with NOR semantics (host tests, simulator).
class MemFlash : public FlashDevice {
public:
    explicit MemFlash(std::size_t bytes) : mem_(bytes, 0xFF) {}
    std::size_t size() const override { return mem_.size(); }
    bool read(std::size_t offset, void* buf, std::size_t len) const override;
    bool write(std::size_t offset, const void* buf, std::size_t len) override;
    bool erase_sector(std::size_t offset) override;
    std::uint32_t erases() const { return erases_; }
    std::vector<std::uint8_t>& raw() { return mem_; }

private:
    std::vector<std::uint8_t> mem_;
    std::uint32_t erases_{0};
};

// One closed hour or day: every metric's slot + the wind-rose bucket of that interval.
struct ClosedInterval {
    std::int64_t idx{0};   // hour / day index as in TieredHistory (local time)
    std::array<Slot, kMetricCount> slots{};
    WindRose::Bucket rose{};
};

class HistoryStore {
public:
    static constexpr std::size_t kSlotBytes = 16;   // a, b, c (float) + n (uint32)
    static constexpr std::size_t kRecordBytes = 16 + kMetricCount * kSlotBytes + sizeof(WindRose::Bucket) + 4;
    static constexpr std::size_t kPerSector = FlashDevice::kSector / kRecordBytes;
    static constexpr std::size_t kHourRecords = 14 * 24;   // capacity guaranteed by the ring
    static constexpr std::size_t kDayRecords = 183;
    static constexpr std::size_t kHourSectors = (kHourRecords + kPerSector - 1) / kPerSector + 1;
    static constexpr std::size_t kDaySectors = (kDayRecords + kPerSector - 1) / kPerSector + 1;
    static constexpr std::size_t kSectorsNeeded = 1 + kHourSectors + kDaySectors;

    // Mounts `dev` (re-initialises it when the superblock does not match this layout). False if it is too small.
    bool begin(FlashDevice* dev);
    bool ready() const { return dev_ != nullptr; }
    bool formatted() const { return formatted_; }   // true when begin() had to erase (new / foreign / old layout)

    bool append(Tier t, const ClosedInterval& rec);   // Tier::Hour or Tier::Day
    bool read(Tier t, std::int64_t idx, ClosedInterval& out) const;
    bool read_slot(Tier t, std::int64_t idx, Metric m, Slot& out) const;
    bool read_rose(Tier t, std::int64_t idx, WindRose::Bucket& out) const;
    std::size_t count(Tier t) const;
    static std::size_t capacity(Tier t) { return t == Tier::Day ? kDayRecords : (t == Tier::Hour ? kHourRecords : 0); }

private:
    struct Entry {
        std::int64_t idx;
        std::uint32_t seq;
        std::uint16_t slot;   // record slot within the ring
    };
    struct Ring {
        std::size_t first_sector{0};
        std::size_t sectors{0};
        std::uint8_t type{0};
        std::vector<Entry> index;   // sorted by idx
        std::uint32_t seq{0};
        std::size_t next{0};        // next record slot to write
        std::size_t slots() const { return sectors * kPerSector; }
    };
    Ring& ring(Tier t) { return t == Tier::Day ? days_ : hours_; }
    const Ring& ring(Tier t) const { return t == Tier::Day ? days_ : hours_; }
    std::size_t offset(const Ring& r, std::size_t slot) const;
    void scan(Ring& r);
    bool slot_blank(const Ring& r, std::size_t slot) const;
    const Entry* find(const Ring& r, std::int64_t idx) const;
    void format();

    FlashDevice* dev_{nullptr};
    bool formatted_{false};
    Ring hours_{}, days_{};
};

std::uint32_t crc32(const std::uint8_t* data, std::size_t len, std::uint32_t crc = 0);

}  // namespace history
}  // namespace station
