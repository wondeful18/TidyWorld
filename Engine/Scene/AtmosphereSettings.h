#pragma once

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 第十六章的距离雾与单张阴影图的接收距离，均使用世界米数。
//————————————————————————————————————————————————————————————————————————————————
struct AtmosphereSettings {
    static constexpr float kMinShadowDistance = 8.0f;    // 近距离演示可设的最短阴影距离
    static constexpr float kMaxShadowDistance = 85.0f;   // 远距离演示可设的最长阴影距离
    static constexpr float kShadowFadeRatio = 0.8f;      // 在最后 20% 的距离衰减阴影
    bool fogEnabled{true};                              // 是否使用距离雾
    float fogStart{24.0f};                               // 从相机到雾起始位置，单位为米
    float fogDensity{0.030f};                            // 透过率的指数衰减系数，单位为 1/米
    float shadowDistance{52.0f};                         // 阴影完全淡出的接收距离，单位为米
};

}  // namespace tinyworld
