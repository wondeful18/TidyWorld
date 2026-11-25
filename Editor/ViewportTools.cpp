#include "Editor/WorldEditor.h"

#include "Engine/Scene/Capstone.h"
#include "Engine/Scene/DayCycle.h"
#include "Engine/Terrain/ChunkStreamer.h"

#include "Engine/Renderer/VegetationRenderer.h"
#include "Engine/Scene/SceneFile.h"
#include "Engine/Terrain/TerrainMesh.h"

#include <imgui.h>
#include <ImGuizmo.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string_view>
#include "Editor/ViewportTools.h"

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 在场景图片内用同一套投影提交局部手柄。选中项的 ID 区分不同的操作目标。
//————————————————————————————————————————————————————————————————————————————————
bool ManipulateLocal(const OrbitCamera& camera, PixelSize size, ImGuizmo::OPERATION operation,
                     std::size_t id, glm::mat4& model) {
    const auto low = ImGui::GetItemRectMin();
    const auto high = ImGui::GetItemRectMax();
    const auto view = camera.View();
    const auto projection = camera.Projection(static_cast<float>(size.width) / size.height, false);
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(low.x, low.y, high.x - low.x, high.y - low.y);
    ImGuizmo::PushID(static_cast<int>(id));
    const bool changed = ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), operation,
                                             ImGuizmo::LOCAL, glm::value_ptr(model));
    ImGuizmo::PopID();
    return changed;
}

//————————————————————————————————————————————————————————————————————————————————
// 从 ImGui 图片坐标构造世界射线，WebGPU 的裁剪深度从 0 到 1。
//————————————————————————————————————————————————————————————————————————————————
bool PickTerrain(const HeightField& field, const glm::mat4& viewProjection, ImVec2 low, ImVec2 high,
                 ImVec2 mouse, glm::vec3& hit) {
    const glm::vec2 ndc{2.0f * (mouse.x - low.x) / (high.x - low.x) - 1.0f,
                        1.0f - 2.0f * (mouse.y - low.y) / (high.y - low.y)};
    const auto inv = glm::inverse(viewProjection);
    const auto nearPoint = inv * glm::vec4(ndc, 0.0f, 1.0f);
    const auto farPoint = inv * glm::vec4(ndc, 1.0f, 1.0f);
    const auto origin = glm::vec3(nearPoint) / nearPoint.w;
    const auto direction = glm::normalize(glm::vec3(farPoint) / farPoint.w - origin);
    return field.Raycast(origin, direction, hit);
}

//————————————————————————————————————————————————————————————————————————————————
// 把地形上的世界点投到视口图片上。相机后方的点不画。
//————————————————————————————————————————————————————————————————————————————————
bool ViewportPoint(glm::vec3 world, const glm::mat4& viewProjection, ImVec2 low, ImVec2 high, ImVec2& point) {
    const auto clip = viewProjection * glm::vec4(world, 1.0f);
    if (clip.w <= 0.0f) {
        return false;
    }
    point = {low.x + (0.5f + 0.5f * clip.x / clip.w) * (high.x - low.x),
             low.y + (0.5f - 0.5f * clip.y / clip.w) * (high.y - low.y)};
    return true;
}

//————————————————————————————————————————————————————————————————————————————————
// 轮廓依照笔刷的距离函数，在地形上投出圆或方形。
//————————————————————————————————————————————————————————————————————————————————
void BrushOutline(const HeightField& field, glm::vec2 center, float radius, bool square,
                  const glm::mat4& vp, ImVec2 low, ImVec2 high) {
    constexpr int segments = 48;
    auto* list = ImGui::GetWindowDrawList();
    ImVec2 last{};
    bool lastVisible = false;
    for (int i = 0; i <= segments; ++i) {
        const float angle = glm::two_pi<float>() * i / segments;
        auto delta = glm::vec2(std::cos(angle), std::sin(angle));
        if (square) {
            delta /= std::max(std::abs(delta.x), std::abs(delta.y));
        }
        const auto point = center + radius * delta;
        ImVec2 current{};
        const bool visible = ViewportPoint({point.x, field.HeightAt(point) + 0.07f, point.y}, vp, low, high, current);
        if (visible && lastVisible) {
            list->AddLine(last, current, IM_COL32(255, 221, 128, 255), 2.0f);
        }
        lastVisible = visible;
        last = current;
    }
}
}  // namespace tinyworld
