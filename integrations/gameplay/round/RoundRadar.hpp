#pragma once

#include "RoundSelection.hpp"
#include <algorithm>
#include <vector>

namespace brt { namespace round {

// 回合、生命和视图版本共同标识本机队伍/HUD；换队、换视角和重建 HUD 时更换 view。
struct RadarView {
    Life life;
    std::uint64_t view = 0;
    friend bool operator==(RadarView a, RadarView b) { return a.life == b.life && a.view == b.view; }
    friend bool operator!=(RadarView a, RadarView b) { return !(a == b); }
};

// 每个本地小地图一个实例。节点和目标使用宿主的弱引用。源码调用者在一次原生绘制完成后提交一帧。
template<class Target, class Node>
class MarkedRadar {
    struct Supplemental {
        Target target;
        Node node;
        std::uint64_t touched;
    };
    RadarView view_{};
    std::uint64_t lastFrame_ = 0;
    std::vector<Supplemental> nodes_;
public:
    MarkedRadar() = default;
    MarkedRadar(const MarkedRadar&) = delete;
    MarkedRadar& operator=(const MarkedRadar&) = delete;

    template<class Surface>
    void Detach(Surface& surface) {
        for (auto& saved : nodes_) surface.Remove(saved.node);
        nodes_.clear();
        view_ = {};
        lastFrame_ = 0;
    }
    template<class Surface>
    void Attach(RadarView view, Surface& surface) {
        if (view_ == view) return;
        Detach(surface);
        if (view.life.Valid() && view.view) view_ = view;
    }
    template<class Capture, class Surface>
    bool Finish(RadarView view, std::uint64_t frame, Capture&& visitCaptured, Surface& surface) {
        if (!view.life.Valid() || !view.view || view != view_ || frame <= lastFrame_) return false;
        lastFrame_ = frame;
        visitCaptured([&](const Target& key, const auto& info) {
            // 宿主仅提交本次捕获通过原有显隐/范围判断的目标。已有原生节点优先。
            if (!surface.IsMarkedEnemy(key, info) || surface.StockDrew(key)) return;
            auto found = std::find_if(nodes_.begin(), nodes_.end(),
                [&](const Supplemental& saved) { return saved.target == key; });
            if (found != nodes_.end() && found->touched == frame) return;
            if (found == nodes_.end()) {
                const Node node = surface.Add(info);
                if (!surface.Alive(node)) return;
                nodes_.push_back({key, node, frame});
                surface.Update(node, info);
                return;
            }
            if (!surface.Alive(found->node)) {
                // UI 树可能在这帧重建，已经脱离画布的节点可在当前捕获中重新创建。
                surface.Remove(found->node);
                found->node = surface.Add(info);
                if (!surface.Alive(found->node)) return;
            }
            found->touched = frame;
            surface.Update(found->node, info);
        });
        nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(), [&](const Supplemental& saved) {
            if (saved.touched == frame) return false;
            surface.Remove(saved.node);
            return true;
        }), nodes_.end());
        return true;
    }
};

} } // namespace brt::round
