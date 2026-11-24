#pragma once

namespace tinyworld {

enum class WaterView { Lit, Depth, NoReflection, Off };  // 同一机位使用的水面比较模式

//————————————————————————————————————————————————————————————————————————————————
// 第十四章水面的显示状态和运行时间。河道与平湖共用一个水平面。
//————————————————————————————————————————————————————————————————————————————————
struct WaterSettings {
    static constexpr float kLevel = -0.38f;  // 湖、河口与海共用的世界 Y，单位为米
    static constexpr float kCoastStart = 14.0f;  // 河口与岸坡材质的过渡起点，世界 X
    static constexpr float kCoastEnd = 25.0f;  // 海浪权重达到 1 的世界 X
    static constexpr float kCoastFar = 150.0f;  // 海面章节的相机最低远裁剪距离，米
    static constexpr float kLoopSeconds = 32.0f;  // 所有波纹一起回到初始相位的周期
    WaterView view{WaterView::Lit};   // 正常、深度、关闭反射或隐藏水面
    bool coast{};                    // 第十五章起让水带通向海面
    float seconds{};                 // 波纹相位使用的场景运行秒数
};

}  // namespace tinyworld
