#pragma once

#include "Engine/Platform/Window.h"
#include "Engine/Scene/OrbitCamera.h"
#include "Engine/Terrain/HeightField.h"

#include <imgui.h>
#include <ImGuizmo.h>

namespace tinyworld {

inline constexpr std::array kSculptLabels{"Raise", "Lower", "Smooth", "Flatten"};

//————————————————————————————————————————————————————————————————————————————————
// 在场景图片内提交局部变换手柄。
//————————————————————————————————————————————————————————————————————————————————
bool ManipulateLocal(const OrbitCamera& camera, PixelSize size, ImGuizmo::OPERATION operation, std::size_t id, glm::mat4& model);
//————————————————————————————————————————————————————————————————————————————————
// 将图片内的鼠标位置转成世界射线并查询地形。
//————————————————————————————————————————————————————————————————————————————————
bool PickTerrain(const HeightField& field, const glm::mat4& viewProjection, ImVec2 low, ImVec2 high, ImVec2 mouse, glm::vec3& hit);
//————————————————————————————————————————————————————————————————————————————————
// 将相机前方的世界点投影到视口图片。
//————————————————————————————————————————————————————————————————————————————————
bool ViewportPoint(glm::vec3 world, const glm::mat4& viewProjection, ImVec2 low, ImVec2 high, ImVec2& point);
//————————————————————————————————————————————————————————————————————————————————
// 沿地形绘制圆形或方形笔刷的轮廓。
//————————————————————————————————————————————————————————————————————————————————
void BrushOutline(const HeightField& field, glm::vec2 center, float radius, bool square, const glm::mat4& vp, ImVec2 low, ImVec2 high);

}  // namespace tinyworld
