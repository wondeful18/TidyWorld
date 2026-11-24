#pragma once

#include <glm/vec4.hpp>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 给物体、植被与水面传同一份线性雾色和距离。与 fog.wgsl 使用相同字节布局。
//————————————————————————————————————————————————————————————————————————————————
struct FrameAtmosphere {
    glm::vec4 colorDensity{0.0f};  // 线性雾色 RGB，W 为密度，0 代表关闭
    glm::vec4 distances{0.0f};     // 雾开始位置、阴影开始/结束淡出位置、填充，单位为米
};

static_assert(sizeof(FrameAtmosphere) == 32);

}  // namespace tinyworld
