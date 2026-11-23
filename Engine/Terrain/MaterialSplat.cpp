#include "Engine/Terrain/MaterialSplat.h"

#include "Engine/Terrain/HeightField.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace tinyworld {
namespace {

//————————————————————————————————————————————————————————————————————————————————
// 新笔划与文件重开共享修订号来源，GPU 能识别替换后的权重表。
//————————————————————————————————————————————————————————————————————————————————
std::uint64_t NextPaintRevision() {
    static std::atomic<std::uint64_t> counter{};
    return ++counter;
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 只遍历笔刷包围盒，并用圆形或方形的距离函数求边缘衰减。
//————————————————————————————————————————————————————————————————————————————————
void MaterialSplat::Paint(glm::vec2 center, const MaterialBrush& brush, float seconds) {
    if (brush.radius <= 0.0f || brush.strength <= 0.0f || seconds <= 0.0f) {
        return;
    }
    constexpr float extent = HeightField::kHalfExtent;
    constexpr float spacing = 2.0f * extent / kSide;
    const auto cell = [](float value) { return std::clamp(static_cast<int>(std::floor(value)), 0, kSide - 1); };
    bool changed = false;
    for (int z = cell((center.y + extent - brush.radius) / spacing);
         z <= cell((center.y + extent + brush.radius) / spacing); ++z) {
        for (int x = cell((center.x + extent - brush.radius) / spacing);
             x <= cell((center.x + extent + brush.radius) / spacing); ++x) {
            const glm::vec2 delta = (glm::vec2{x, z} + 0.5f) * spacing - extent - center;
            const float distance = brush.square ? std::max(std::abs(delta.x), std::abs(delta.y)) : glm::length(delta);
            const float t = std::max(0.0f, 1.0f - distance / brush.radius);
            const float alpha = 1.0f - std::exp(-brush.strength * seconds * t * t * (3.0f - 2.0f * t));
            auto& pixel = mPixels[z * kSide + x];
            Pixel next{};
            int sum = 0;
            for (int layer = 0; layer < 4; ++layer) {
                const float target = layer == brush.layer ? 255.0f : 0.0f;
                next[layer] = static_cast<std::uint8_t>(std::lround(std::lerp(static_cast<float>(pixel[layer]), target, alpha)));
                sum += next[layer];
            }
            if (sum > 255) {
                auto& highest = *std::max_element(next.begin(), next.end());
                highest -= static_cast<std::uint8_t>(sum - 255);
            }
            changed |= next != pixel;
            pixel = next;
        }
    }
    if (changed) {
        mRevision = NextPaintRevision();
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 先验证所有像素，再提交，避免错误存档覆盖当前画面。
//————————————————————————————————————————————————————————————————————————————————
void MaterialSplat::Replace(const Pixels& pixels) {
    if (std::any_of(pixels.begin(), pixels.end(), [](const Pixel& p) {
        return std::accumulate(p.begin(), p.end(), 0) > 255;
    })) {
        throw std::runtime_error("Terrain paint channels exceed their available weight.");
    }
    mPixels = pixels;
    mRevision = NextPaintRevision();
}

}  // namespace tinyworld
