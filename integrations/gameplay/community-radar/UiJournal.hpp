#pragma once
#include <array>
#include <filesystem>
#include <fstream>

namespace BrtUiJournal {
enum class Stage {
    Room, NoSignal, NoHud, NoManager, Scan, Visible, RangeRejected, StyleRejected,
    ProjectionRejected, NativeDot, NodeRejected, DrewDot, EmptyShoulders, HasShoulder,
    OwnerPending, OwnerDifferent, HasOwner, SelectedLeft, SelectedRight, Presenting,
    HideEmpty, HidePrimitives, Closed, ReticleFound, ReticleHidden, ReticleRestored,
    SupplyLocal, SupplyNative, SupplyDrawn, Count
};
inline constexpr const char* labels[] = {
    "room","radar-no-signal","radar-no-hud","radar-no-manager","radar-pass","radar-visible-mark",
    "radar-range-rejected","radar-style-rejected","radar-projection-rejected","radar-native-dot",
    "radar-node-rejected","radar-drew-dot","pod-empty-shoulders","pod-has-shoulder",
    "pod-owner-pending","pod-owner-different","pod-has-owner","pod-left-selected","pod-right-selected",
    "pod-presenting","pod-hide-without-components","pod-hide-with-components","pod-closed",
    "reticle-found","reticle-hidden","reticle-restored","radar-supply-local","radar-supply-native","radar-supply-drawn"
};
static_assert(sizeof(labels)/sizeof(*labels) == static_cast<unsigned>(Stage::Count));
inline const void* active{};
inline std::array<unsigned, static_cast<unsigned>(Stage::Count)> counts{};
inline std::ofstream file;
inline bool attempted{};

inline void Note(const void* pawn, Stage stage) noexcept {
    if (!pawn || pawn != active || !file.is_open()) return;
    const auto i = static_cast<unsigned>(stage);
    if (counts[i] >= 16) return;
    const auto count = ++counts[i];
    if (count != 1 && count != 16) return;
    file << "[BRT-LOCAL-UI] stage=" << labels[i] << " count=" << count << '\n';
    file.flush();
}
template<class SavedDirectory>
inline void Bind(const void* pawn, SavedDirectory savedDirectory) noexcept {
    active = pawn;
    if (!pawn) return;
    if (!attempted) {
        attempted = true;
        try {
            auto saved = savedDirectory();
            if (!saved.empty()) {
                const auto dir = std::filesystem::path(saved) / L"Logs";
                std::error_code error;
                std::filesystem::create_directories(dir,error);
                if (!error) file.open(dir / L"BRT-local-ui.log",std::ios::trunc);
            }
        } catch (...) {}
    }
    Note(pawn,Stage::Room);
}
inline void Unbind(const void* pawn) { if (active == pawn) active = nullptr; }
}
