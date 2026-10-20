#include "GasIndex.hpp"

namespace station {
namespace airquality {

namespace {
constexpr std::uint32_t kVocLearningS = static_cast<std::uint32_t>(3600.0F * 1.45F);  // INIT_DURATION_VARIANCE_VOC
constexpr std::uint32_t kNoxLearningS = static_cast<std::uint32_t>(3600.0F * 5.70F);  // INIT_DURATION_VARIANCE_NOX
constexpr std::uint32_t kMinStateAgeS = 3U * 3600U;
}  // namespace

GasIndexPair::GasIndexPair() {
    reset();
}

void GasIndexPair::reset() {
    GasIndexAlgorithm_init(&voc_, GasIndexAlgorithm_ALGORITHM_TYPE_VOC);
    GasIndexAlgorithm_init(&nox_, GasIndexAlgorithm_ALGORITHM_TYPE_NOX);
    voc_index_ = 0;
    nox_index_ = 0;
    seconds_ = 0;
    restored_ = false;
}

void GasIndexPair::process(std::uint16_t sraw_voc, bool nox_ok, std::uint16_t sraw_nox) {
    GasIndexAlgorithm_process(&voc_, static_cast<std::int32_t>(sraw_voc), &voc_index_);
    if (nox_ok) {
        GasIndexAlgorithm_process(&nox_, static_cast<std::int32_t>(sraw_nox), &nox_index_);
    }
    ++seconds_;
}

bool GasIndexPair::voc(float& out) const {
    if (voc_index_ <= 0) {
        return false;
    }
    out = static_cast<float>(voc_index_);
    return true;
}

bool GasIndexPair::nox(float& out) const {
    if (nox_index_ <= 0) {
        return false;
    }
    out = static_cast<float>(nox_index_);
    return true;
}

bool GasIndexPair::learning() const {
    const bool voc_learning = !restored_ && seconds_ < kVocLearningS;
    return voc_learning || seconds_ < kNoxLearningS;
}

bool GasIndexPair::voc_state(float& state0, float& state1) const {
    if (seconds_ < kMinStateAgeS && !restored_) {
        return false;
    }
    GasIndexAlgorithm_get_states(&voc_, &state0, &state1);
    return true;
}

void GasIndexPair::restore_voc_state(float state0, float state1) {
    GasIndexAlgorithm_reset(&voc_);
    GasIndexAlgorithm_set_states(&voc_, state0, state1);
    restored_ = true;
}

}  // namespace airquality
}  // namespace station
