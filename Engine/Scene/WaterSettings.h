#pragma once

namespace tinyworld {

enum class WaterView { Lit, Depth, NoReflection, Off };  // 同一机位使用的水面比较模式

//————————————————————————————————————————————————————————————————————————————————
// 第十四章水面的显示状态和运行时间。河道与平湖共用一个水平面。
//————————————————————————————————————————————————————————————————————————————————
struct WaterSettings {
    static constexpr float kLoopSeconds = 32.0f;  // 所有波纹一起回到初始相位的周期
    WaterView view{WaterView::Lit};   // 正常、深度、关闭反射或隐藏水面
    float seconds{};                 // 波纹相位使用的场景运行秒数
};

}  // namespace tinyworld
