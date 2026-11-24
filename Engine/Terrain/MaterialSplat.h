#pragma once

#include "Engine/Terrain/HeightField.h"

#include <glm/vec2.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 一个绘制笔刷写入 R、G、B、A 中指定的一层。层号 4 用来擦除。
//————————————————————————————————————————————————————————————————————————————————
struct MaterialBrush {
    int layer{};             // 草、碎石、岩石、沙土或擦除
    bool square{};           // 方形使用两轴最大距离，圆形使用欧氏距离
    float radius{1.8f};      // 笔刷作用半径，米
    float strength{4.0f};    // 每秒接近目标权重的速率
};

//————————————————————————————————————————————————————————————————————————————————
// 按 Z 行保存线性 RGBA 权重，空余权重分配给自动地表规则。
//————————————————————————————————————————————————————————————————————————————————
class MaterialSplat {
public:
    static constexpr int kSide = static_cast<int>(2.0f * HeightField::kHalfExtent / 0.25f);
    using Pixel = std::array<std::uint8_t, 4>;
    using Pixels = std::vector<Pixel>;

    //————————————————————————————————————————————————————————————————————————————————
    // 笔刷同时重分配四个通道，每个像素的总和保持不超过 255。
    //————————————————————————————————————————————————————————————————————————————————
    void Paint(glm::vec2 center, const MaterialBrush& brush, float seconds);

    //————————————————————————————————————————————————————————————————————————————————
    // 用校验后的完整像素表替换当前权重。
    //————————————————————————————————————————————————————————————————————————————————
    void Replace(const Pixels& pixels);

    //————————————————————————————————————————————————————————————————————————————————
    // 读取待上传或保存的数据与修订号。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] const Pixels& Values() const { return mPixels; }
    [[nodiscard]] std::uint64_t Revision() const { return mRevision; }

private:
    Pixels mPixels = Pixels(kSide * kSide);  // 线性 RGBA，四个通道总和不超过 255
    std::uint64_t mRevision{};  // 绘制或重开时替换的修订号
};

}  // namespace tinyworld
