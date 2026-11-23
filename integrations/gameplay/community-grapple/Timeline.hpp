#pragma once
#include <cmath>
#include <cstdint>

namespace brt::grapple {
struct Sample {
    unsigned phase;
    bool cooling;
    bool anchor;
    int fraction;
};

inline int Fraction(float value) {
    return std::isfinite(value) && value >= 0 && value <= 1 ? static_cast<int>(std::lround(value * 1000)) : -1;
}

enum class Delivery { None, WaitForOwner, NotifyOwner };

// 每个角色的每件钩索各自持有一份；没有服务端原生使用和冷却结束就没有通知。
struct CooldownDelivery {
    bool used = false, cooling = false, pending = false, reportedMissing = false;

    template<class CanRoute>
    Delivery Observe(const Sample& sample, CanRoute&& canRoute) {
        if (sample.phase != 0) {
            *this = {};
            used = sample.phase <= 3;
        } else if (sample.cooling) {
            // 中途绑定看见冷却不算亲眼见到这次使用。
            if (used) { cooling = true; used = false; }
            // 已结束的旧冷却不能被送到另一段新冷却。
            pending = reportedMissing = false;
        } else {
            if (cooling) { pending = true; cooling = false; }
            if (pending) {
                if (canRoute()) { pending = false; return Delivery::NotifyOwner; }
                if (!reportedMissing) { reportedMissing = true; return Delivery::WaitForOwner; }
            }
        }
        return Delivery::None;
    }
};

struct Timeline {
    bool hasSample = false;
    Sample previous{};
    int watchStart = -1;
    unsigned checks = 0;

    template<class Write>
    void Observe(int milliseconds, const Sample& sample, Write&& write) {
        if (!hasSample) {
            hasSample = true;
            previous = sample;
            write("baseline", sample);
            return;
        }
        // Ready 和冷却是两份独立状态。计时只安排两次诊断取样。
        if ((previous.phase == 2 || previous.phase == 3) && sample.phase == 0) {
            watchStart = milliseconds;
            checks = 0;
        }
        if (sample.phase == 1 || sample.phase == 2) watchStart = -1;
        if (sample.phase != previous.phase || sample.cooling != previous.cooling || sample.anchor != previous.anchor)
            write("change", sample);
        if (watchStart >= 0 && checks < 2 && milliseconds - watchStart >= int(checks + 1) * 8000) {
            ++checks;
            write("followup", sample);
        }
        previous = sample;
    }
};
}
