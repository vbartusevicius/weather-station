#include "HistoryStore.hpp"

#include <algorithm>
#include <cstring>

namespace station {
namespace history {

namespace {
constexpr std::uint16_t kRecMagic = 0x4D48;   // "HM"
constexpr std::uint8_t kRecVersion = 1;
constexpr std::uint8_t kTypeHour = 1;
constexpr std::uint8_t kTypeDay = 2;
constexpr std::uint32_t kSbMagic = 0x53484D41;   // "AMHS"
constexpr std::uint16_t kSbVersion = 1;
constexpr std::size_t kSbBytes = 18;

constexpr std::size_t kSlotB = HistoryStore::kSlotBytes;
void put_slot(std::uint8_t* p, const Slot& s) {
    std::memcpy(p, &s.a, 4);
    std::memcpy(p + 4, &s.b, 4);
    std::memcpy(p + 8, &s.c, 4);
    std::memcpy(p + 12, &s.n, 4);
}
Slot get_slot(const std::uint8_t* p) {
    Slot s{};
    std::memcpy(&s.a, p, 4);
    std::memcpy(&s.b, p + 4, 4);
    std::memcpy(&s.c, p + 8, 4);
    std::memcpy(&s.n, p + 12, 4);
    return s;
}
constexpr std::size_t kSlotsOff = 16;
constexpr std::size_t kRoseOff = kSlotsOff + kMetricCount * kSlotB;
constexpr std::size_t kCrcOff = HistoryStore::kRecordBytes - 4;

void superblock(std::uint8_t (&sb)[kSbBytes]) {
    const std::uint16_t ver = kSbVersion, rec = static_cast<std::uint16_t>(HistoryStore::kRecordBytes),
                        metrics = static_cast<std::uint16_t>(kMetricCount),
                        hs = static_cast<std::uint16_t>(HistoryStore::kHourSectors),
                        ds = static_cast<std::uint16_t>(HistoryStore::kDaySectors);
    std::memcpy(sb, &kSbMagic, 4);
    std::memcpy(sb + 4, &ver, 2);
    std::memcpy(sb + 6, &rec, 2);
    std::memcpy(sb + 8, &metrics, 2);
    std::memcpy(sb + 10, &hs, 2);
    std::memcpy(sb + 12, &ds, 2);
    const std::uint32_t c = crc32(sb, 14);
    std::memcpy(sb + 14, &c, 4);
}
}  // namespace

std::uint32_t crc32(const std::uint8_t* data, std::size_t len, std::uint32_t crc) {
    crc = ~crc;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

// ------------------------------------------------------------------------------------------------- MemFlash
bool MemFlash::read(std::size_t offset, void* buf, std::size_t len) const {
    if (offset + len > mem_.size()) return false;
    std::memcpy(buf, mem_.data() + offset, len);
    return true;
}
bool MemFlash::write(std::size_t offset, const void* buf, std::size_t len) {
    if (offset + len > mem_.size()) return false;
    const auto* src = static_cast<const std::uint8_t*>(buf);
    for (std::size_t i = 0; i < len; ++i) mem_[offset + i] &= src[i];   // NOR flash can only clear bits
    return true;
}
bool MemFlash::erase_sector(std::size_t offset) {
    if (offset % kSector != 0 || offset + kSector > mem_.size()) return false;
    std::fill(mem_.begin() + static_cast<std::ptrdiff_t>(offset), mem_.begin() + static_cast<std::ptrdiff_t>(offset + kSector), 0xFF);
    ++erases_;
    return true;
}

// ------------------------------------------------------------------------------------------------- store
std::size_t HistoryStore::offset(const Ring& r, std::size_t slot) const {
    return (r.first_sector + slot / kPerSector) * FlashDevice::kSector + (slot % kPerSector) * kRecordBytes;
}

bool HistoryStore::slot_blank(const Ring& r, std::size_t slot) const {
    std::uint8_t buf[kRecordBytes];
    if (!dev_->read(offset(r, slot), buf, sizeof buf)) return false;
    return std::all_of(buf, buf + sizeof buf, [](std::uint8_t b) { return b == 0xFF; });
}

void HistoryStore::format() {
    for (std::size_t s = 0; s < kSectorsNeeded; ++s) dev_->erase_sector(s * FlashDevice::kSector);
    std::uint8_t sb[kSbBytes];
    superblock(sb);
    dev_->write(0, sb, sizeof sb);
    formatted_ = true;
}

bool HistoryStore::begin(FlashDevice* dev) {
    dev_ = nullptr;
    formatted_ = false;
    if (dev == nullptr || dev->size() < kSectorsNeeded * FlashDevice::kSector) return false;
    dev_ = dev;
    std::uint8_t want[kSbBytes], have[kSbBytes];
    superblock(want);
    if (!dev_->read(0, have, sizeof have) || std::memcmp(want, have, sizeof want) != 0) {
        format();   // first start, the old LittleFS data or another layout: start a fresh log
    }
    hours_ = Ring{};
    hours_.first_sector = 1;
    hours_.sectors = kHourSectors;
    hours_.type = kTypeHour;
    days_ = Ring{};
    days_.first_sector = 1 + kHourSectors;
    days_.sectors = kDaySectors;
    days_.type = kTypeDay;
    scan(hours_);
    scan(days_);
    return true;
}

void HistoryStore::scan(Ring& r) {
    std::uint8_t buf[kRecordBytes];
    bool any = false;
    std::size_t last_slot = 0;
    for (std::size_t slot = 0; slot < r.slots(); ++slot) {
        if (!dev_->read(offset(r, slot), buf, sizeof buf)) continue;
        std::uint16_t magic = 0;
        std::memcpy(&magic, buf, 2);
        if (magic != kRecMagic || buf[2] != r.type || buf[3] != kRecVersion) continue;
        std::uint32_t crc = 0;
        std::memcpy(&crc, buf + kCrcOff, 4);
        if (crc != crc32(buf, kCrcOff)) continue;   // torn write
        Entry e{};
        std::memcpy(&e.seq, buf + 4, 4);
        std::memcpy(&e.idx, buf + 8, 8);
        e.slot = static_cast<std::uint16_t>(slot);
        auto it = std::lower_bound(r.index.begin(), r.index.end(), e.idx, [](const Entry& a, std::int64_t v) { return a.idx < v; });
        if (it != r.index.end() && it->idx == e.idx) {
            if (it->seq < e.seq) *it = e;   // the same interval written again (clock change): newest wins
        } else {
            r.index.insert(it, e);
        }
        if (!any || e.seq > r.seq) {
            r.seq = e.seq;
            last_slot = slot;
        }
        any = true;
    }
    r.next = any ? (last_slot + 1) % r.slots() : 0;
    // skip a torn record after the newest one: only write into blank slots (or a sector start, which is erased)
    for (std::size_t guard = 0; guard < kPerSector && r.next % kPerSector != 0 && !slot_blank(r, r.next); ++guard) {
        r.next = (r.next + 1) % r.slots();
    }
}

const HistoryStore::Entry* HistoryStore::find(const Ring& r, std::int64_t idx) const {
    auto it = std::lower_bound(r.index.begin(), r.index.end(), idx, [](const Entry& a, std::int64_t v) { return a.idx < v; });
    return (it != r.index.end() && it->idx == idx) ? &*it : nullptr;
}

bool HistoryStore::append(Tier t, const ClosedInterval& rec) {
    if (dev_ == nullptr || t == Tier::Minute) return false;
    Ring& r = ring(t);
    std::size_t slot = r.next;
    if (slot % kPerSector != 0 && !slot_blank(r, slot)) {
        slot = (slot / kPerSector + 1) * kPerSector % r.slots();   // never program over written bytes
    }
    if (slot % kPerSector == 0) {   // entering a sector: erase it and forget the records it held
        const std::size_t sector = slot / kPerSector;
        if (!dev_->erase_sector((r.first_sector + sector) * FlashDevice::kSector)) return false;
        r.index.erase(std::remove_if(r.index.begin(), r.index.end(), [sector](const Entry& e) { return e.slot / kPerSector == sector; }),
                      r.index.end());
    }
    std::uint8_t buf[kRecordBytes];
    std::memset(buf, 0, sizeof buf);
    const std::uint32_t seq = r.seq + 1;
    std::memcpy(buf, &kRecMagic, 2);
    buf[2] = r.type;
    buf[3] = kRecVersion;
    std::memcpy(buf + 4, &seq, 4);
    std::memcpy(buf + 8, &rec.idx, 8);
    for (std::size_t m = 0; m < kMetricCount; ++m) put_slot(buf + kSlotsOff + m * kSlotB, rec.slots[m]);
    std::memcpy(buf + kRoseOff, &rec.rose, sizeof(WindRose::Bucket));
    const std::uint32_t crc = crc32(buf, kCrcOff);
    std::memcpy(buf + kCrcOff, &crc, 4);
    if (!dev_->write(offset(r, slot), buf, sizeof buf)) return false;
    r.seq = seq;
    r.next = (slot + 1) % r.slots();
    Entry e{rec.idx, seq, static_cast<std::uint16_t>(slot)};
    auto it = std::lower_bound(r.index.begin(), r.index.end(), e.idx, [](const Entry& a, std::int64_t v) { return a.idx < v; });
    if (it != r.index.end() && it->idx == e.idx) {
        *it = e;
    } else {
        r.index.insert(it, e);
    }
    return true;
}

bool HistoryStore::read(Tier t, std::int64_t idx, ClosedInterval& out) const {
    if (dev_ == nullptr || t == Tier::Minute) return false;
    const Ring& r = ring(t);
    const Entry* e = find(r, idx);
    std::uint8_t buf[kRecordBytes];
    if (e == nullptr || !dev_->read(offset(r, e->slot), buf, sizeof buf)) return false;
    out.idx = idx;
    for (std::size_t m = 0; m < kMetricCount; ++m) out.slots[m] = get_slot(buf + kSlotsOff + m * kSlotB);
    std::memcpy(&out.rose, buf + kRoseOff, sizeof(WindRose::Bucket));
    return true;
}

bool HistoryStore::read_slot(Tier t, std::int64_t idx, Metric m, Slot& out) const {
    if (dev_ == nullptr || t == Tier::Minute) return false;
    const Ring& r = ring(t);
    const Entry* e = find(r, idx);
    std::uint8_t buf[kSlotB];
    if (e == nullptr || !dev_->read(offset(r, e->slot) + kSlotsOff + static_cast<std::size_t>(m) * kSlotB, buf, sizeof buf)) return false;
    out = get_slot(buf);
    return true;
}

bool HistoryStore::read_rose(Tier t, std::int64_t idx, WindRose::Bucket& out) const {
    if (dev_ == nullptr || t == Tier::Minute) return false;
    const Ring& r = ring(t);
    const Entry* e = find(r, idx);
    return e != nullptr && dev_->read(offset(r, e->slot) + kRoseOff, &out, sizeof out);
}

std::size_t HistoryStore::count(Tier t) const {
    return t == Tier::Minute ? 0 : ring(t).index.size();
}

}  // namespace history
}  // namespace station
