#include "Engine/Scene/OrbitCamera.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#include <cmath>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 绕 Y 轴确定方位，并用仰角确定相机高度。
//————————————————————————————————————————————————————————————————————————————————
glm::mat4 OrbitCamera::View() const {
    const float yaw = glm::radians(azimuth);
    const float pitch = glm::radians(elevation);
    const glm::vec3 offset{std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                           std::cos(yaw) * std::cos(pitch)};
    return glm::lookAtRH(target + distance * offset, target, glm::vec3{0.0f, 1.0f, 0.0f});
}

//————————————————————————————————————————————————————————————————————————————————
// 为场景和 ImGuizmo 提供等价的像素投影，二者只采用不同的裁剪深度约定。
//————————————————————————————————————————————————————————————————————————————————
glm::mat4 OrbitCamera::Projection(float aspect, bool zeroToOne) const {
    const float fov = glm::radians(43.0f);
    return zeroToOne ? glm::perspectiveRH_ZO(fov, aspect, 0.1f, 80.0f)
                     : glm::perspectiveRH_NO(fov, aspect, 0.1f, 80.0f);
}

}  // namespace tinyworld
