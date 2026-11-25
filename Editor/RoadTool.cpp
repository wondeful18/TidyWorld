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
// 世界坐标与视口拖动共享同一份控制点；输出时按当前点和边距重算遮罩。
//————————————————————————————————————————————————————————————————————————————————
void WorldEditor::DrawRoadInspector() {
    if (mScene.storage) {
        ImGui::TextWrapped("Open the authoring JSON to edit and repack this road.");
        return;
    }
    auto& road = *mScene.road;
    mRoadPoint = std::min(mRoadPoint, road.points.size() - 1);
    ImGui::SeparatorText("Spline road");
    int selected = static_cast<int>(mRoadPoint + 1);
    if (ImGui::SliderInt("Control point", &selected, 1, static_cast<int>(road.points.size()), "%d",
                         ImGuiSliderFlags_AlwaysClamp)) {
        mRoadPoint = static_cast<std::size_t>(selected - 1);
    }
    auto& point = road.points[mRoadPoint];
    const float pointLimit = mScene.terrain->Grid().HalfExtent() - 0.5f;
    ImGui::DragFloat("World X (m)", &point.x, 0.05f, -pointLimit, pointLimit,
                     "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloat("Y offset (m)", &point.y, 0.05f, -RoadSpline::kHeightOffsetLimit, RoadSpline::kHeightOffsetLimit,
                     "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloat("World Z (m)", &point.z, 0.05f, -pointLimit, pointLimit,
                     "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Width (m)", &road.width, RoadSpline::kMinimumWidth, RoadSpline::kMaximumWidth,
                       "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Avoidance margin (m)", &road.margin, 0.0f, RoadSpline::kMaximumMargin,
                       "%.2f", ImGuiSliderFlags_AlwaysClamp);
    int surface = static_cast<int>(road.surface);
    if (ImGui::Combo("Surface", &surface, "Gravel\0Dirt\0Paved\0")) {
        road.surface = static_cast<RoadSurface>(surface);
    }
    int junction = static_cast<int>(road.junction);
    if (ImGui::Combo("Junction", &junction, "None\0T junction\0Crossroad\0")) {
        road.junction = static_cast<RoadJunction>(junction);
    }
    if (road.junction != RoadJunction::None && road.points.size() >= 3) {
        int anchor = static_cast<int>(std::clamp<std::size_t>(road.junctionPoint, 1, road.points.size() - 2) + 1);
        if (ImGui::SliderInt("Junction point", &anchor, 2, static_cast<int>(road.points.size()) - 1)) {
            road.junctionPoint = static_cast<std::size_t>(anchor - 1);
        }
        ImGui::SliderFloat("Branch length (m)", &road.branchLength, 2.0f, 8.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    }
    ImGui::BeginDisabled(road.points.size() <= RoadSpline::kMinPoints);
    if (ImGui::Button("Remove point")) {
        const auto erased = mRoadPoint;
        road.points.erase(road.points.begin() + static_cast<std::ptrdiff_t>(mRoadPoint));
        mRoadPoint = std::min(mRoadPoint, road.points.size() - 1);
        if (road.junctionPoint > erased) --road.junctionPoint;
        road.junctionPoint = std::min(road.junctionPoint, road.points.size() - 1);
    }
    ImGui::EndDisabled();
    if (ImGui::Button("Export avoidance PGM")) {
        try {
            const std::string_view text(mScenePath.data());
            std::filesystem::path path(std::u8string(text.begin(), text.end()));
            path = path.filename().empty() ? std::filesystem::path("road-avoidance.pgm")
                                           : path.replace_extension(".avoidance.pgm");
            ExportRoadMask(path, road, mScene.terrain->Grid());
            const auto name = path.filename().u8string();
            mRoadStatus = "Exported " + std::string(name.begin(), name.end());
        } catch (const std::exception& error) {
            mRoadStatus = error.what();
        }
    }
    if (!mRoadStatus.empty()) {
        ImGui::TextWrapped("%s", mRoadStatus.c_str());
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 点击投影标记选择节点。局部 Z 沿路线，Y 始终竖直。
//————————————————————————————————————————————————————————————————————————————————
void WorldEditor::DrawRoadEditor(PixelSize size) {
    auto& road = *mScene.road;
    const auto& field = *mScene.terrain;
    const auto low = ImGui::GetItemRectMin();
    const auto high = ImGui::GetItemRectMax();
    const auto& camera = mScene.camera;
    const auto vp = camera.Projection(static_cast<float>(size.width) / size.height) * camera.View();
    const auto mouse = ImGui::GetIO().MousePos;
    mRoadPoint = std::min(mRoadPoint, road.points.size() - 1);
    const auto toWorld = [&](glm::vec3 p) {
        return p + glm::vec3{0.0f, field.HeightAt({p.x, p.z}) + 0.12f, 0.0f};
    };
    const auto toScreen = [&](glm::vec3 p, ImVec2& marker) { return ViewportPoint(toWorld(p), vp, low, high, marker); };
    auto* list = ImGui::GetWindowDrawList();
    const float pointLimit = field.Grid().HalfExtent() - 0.5f;
    for (const auto& path : {road.Centerline(), road.Branch(pointLimit)}) {
        ImVec2 last{};
        bool lastVisible = false;
        for (const auto point : path) {
            ImVec2 current{};
            const bool visible = toScreen(point, current);
            if (visible && lastVisible) {
                list->AddLine(last, current, IM_COL32(255, 220, 106, 255), 2.0f);
            }
            last = current;
            lastVisible = visible;
        }
    }
    for (std::size_t i = 0; i < road.points.size(); ++i) {
        ImVec2 marker{};
        if (toScreen(road.points[i], marker)) {
            const ImU32 color = i == mRoadPoint ? IM_COL32(255, 205, 70, 255) : IM_COL32(223, 245, 247, 255);
            list->AddCircleFilled(marker, 5.5f, color);
            list->AddCircle(marker, 6.0f, IM_COL32(39, 57, 60, 255), 0, 2.0f);
        }
    }
    if (mOrbiting) return;

    const auto& io = ImGui::GetIO();
    const bool clicked = ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    if (clicked && io.KeyShift && !ImGuizmo::IsUsingAny()) {
        glm::vec3 hit;
        if (road.points.size() < RoadSpline::kMaxPoints && PickTerrain(field, vp, low, high, mouse, hit)) {
            const auto xz = glm::clamp(glm::vec2{hit.x, hit.z}, -pointLimit, pointLimit);
            ++mRoadPoint;
            if (road.junctionPoint >= mRoadPoint) ++road.junctionPoint;
            road.points.insert(road.points.begin() + static_cast<std::ptrdiff_t>(mRoadPoint), {xz.x, 0.0f, xz.y});
            road.junctionPoint = std::min(road.junctionPoint, road.points.size() - 1);
        }
        return;
    }

    auto& selected = road.points[mRoadPoint];
    if (!ImGuizmo::IsUsingAny()) {
        const auto tangent = road.points[std::min(mRoadPoint + 1, road.points.size() - 1)] -
                             road.points[mRoadPoint == 0 ? 0 : mRoadPoint - 1];
        mRoadHeading = std::hypot(tangent.x, tangent.z) > 0.0001f
            ? glm::degrees(std::atan2(tangent.x, tangent.z)) : 0.0f;
    }
    const auto origin = toWorld(selected);
    auto model = Transform{origin, {0.0f, mRoadHeading, 0.0f}}.Matrix();
    if (ManipulateLocal(camera, size, ImGuizmo::TRANSLATE, kRoadPanel, model)) {
        const auto delta = glm::vec3(model[3]) - origin;
        const glm::vec3 limit{pointLimit, RoadSpline::kHeightOffsetLimit, pointLimit};
        // 横向拖动只改 XZ。地面在新位置的高度由地形重新求得。
        selected = glm::clamp(selected + delta, -limit, limit);
    }
    if (clicked && ImGuizmo::GetHoveredHandleType() == ImGuizmo::MT_NONE && !ImGuizmo::IsUsingAny()) {
        float closest = 13.0f * 13.0f;
        for (std::size_t i = 0; i < road.points.size(); ++i) {
            ImVec2 marker{};
            if (toScreen(road.points[i], marker)) {
                const float dx = marker.x - mouse.x;
                const float dy = marker.y - mouse.y;
                const float distance = dx * dx + dy * dy;
                if (distance < closest) {
                    closest = distance;
                    mRoadPoint = i;
                }
            }
        }
    }
}

}  // namespace tinyworld
