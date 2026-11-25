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
// 选中资产后等待地形落点。
//————————————————————————————————————————————————————————————————————————————————
void WorldEditor::DrawPropsPalette() {
    if (mScene.storage) {
        ImGui::TextWrapped("Open the authoring JSON to edit and repack placed objects.");
        return;
    }
    ImGui::SeparatorText("Place objects");
    const auto& style = ImGui::GetStyle();
    const float width = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;
    ImGui::BeginDisabled(mScene.props.size() >= PlacedProp::kLimit);
    const auto count = mScene.lightmapEnabled ? kPropAssets.size() :
                       mScene.canUseGltfProps ? kPorchAsset : kBuiltinPropCount;
    for (std::size_t i = 0; i < count; ++i) {
        if (i % 2 == 1) ImGui::SameLine();
        const bool armed = mPlacement == i;
        if (armed) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
        if (ImGui::Button(kPropAssets[i].name, {width, 0.0f})) {
            mPlacement = i;
            if (mScene.materials) mScene.materials->spheres = false;
        }
        if (armed) ImGui::PopStyleColor();
    }
    ImGui::EndDisabled();
    if (mPlacement) {
        ImGui::TextWrapped("Click terrain to place the %s. Esc cancels.", kPropAssets[*mPlacement].name);
    } else if (mScene.props.size() >= PlacedProp::kLimit) {
        ImGui::TextWrapped("Object limit reached. Remove an object to place another.");
    } else {
        ImGui::TextWrapped("Choose an object, then click the terrain.");
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 沿用笔刷的射线拾取放置物体，单击后转入新实例的变换编辑。
//————————————————————————————————————————————————————————————————————————————————
void WorldEditor::DrawPropPlacement(PixelSize size) {
    if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        mPlacement.reset();
        return;
    }
    if (mOrbiting || !ImGui::IsItemHovered()) return;
    const auto low = ImGui::GetItemRectMin();
    const auto high = ImGui::GetItemRectMax();
    const auto& camera = mScene.camera;
    const auto vp = camera.Projection(static_cast<float>(size.width) / size.height) * camera.View();
    const auto& field = *mScene.terrain;
    glm::vec3 hit;
    if (!PickTerrain(field, vp, low, high, ImGui::GetIO().MousePos, hit)) return;
    BrushOutline(field, {hit.x, hit.z}, 0.55f, false, vp, low, high);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mScene.props.size() < PlacedProp::kLimit) {
        const auto* lightmap = kPropAssets[*mPlacement].lightmap;
        mScene.props.push_back({*mPlacement, {hit}, lightmap ? lightmap : ""});
        SelectForEditing(kFirstProp + mScene.props.size() - 1);
        mGizmoMode = GizmoMode::Move;
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 右键和滚轮驱动相机，左键仍留给变换手柄。
// 手柄使用与场景一致的视图与纵横比，只换用它所需的裁剪深度区间。
//————————————————————————————————————————————————————————————————————————————————
void WorldEditor::DrawEditControls(PixelSize size) {
    const bool hovered = ImGui::IsItemHovered();
    auto& camera = mScene.camera;
    const auto& io = ImGui::GetIO();
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !ImGuizmo::IsUsingAny()) {
        mOrbiting = true;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        mOrbiting = false;
    }
    if (mOrbiting) {
        camera.azimuth = std::remainder(camera.azimuth - 0.28f * io.MouseDelta.x, 360.0f);
        const float minimumElevation = mScene.environment ? -35.0f : 10.0f;
        camera.elevation = std::clamp(camera.elevation + 0.25f * io.MouseDelta.y, minimumElevation, 80.0f);
    }
    if (hovered && io.MouseWheel != 0.0f && !ImGuizmo::IsUsingAny()) {
        const float maxDistance = mScene.terrain && mScene.terrain->Grid() == TerrainGrid::Exploration()
            ? 350.0f : mScene.terrain ? 65.0f : 33.0f;
        camera.distance = std::clamp(camera.distance * std::exp(-0.095f * io.MouseWheel), 5.0f, maxDistance);
    }

    if (!mSelected || mScene.storage || mOrbiting || mSculptEnabled || mPlacement) return;
    const auto selection = *mSelected;
    if (selection >= mScene.objects.size() && selection < kFirstProp) return;
    auto& transform = selection >= kFirstProp ? mScene.props[selection - kFirstProp].transform
                                              : mScene.objects[selection].transform;
    auto model = transform.Matrix();
    constexpr std::array operations{ImGuizmo::TRANSLATE, ImGuizmo::ROTATE, ImGuizmo::SCALE};
    if (ManipulateLocal(camera, size, operations[static_cast<std::size_t>(mGizmoMode)], selection, model)) {
        switch (mGizmoMode) {
        case GizmoMode::Move:
            transform.position = glm::vec3(model[3]);
            break;
        case GizmoMode::Rotate: {
            const glm::mat3 rotation{glm::normalize(glm::vec3(model[0])),
                                     glm::normalize(glm::vec3(model[1])),
                                     glm::normalize(glm::vec3(model[2]))};
            transform.rotationDegrees = glm::degrees(glm::eulerAngles(glm::quat_cast(rotation)));
            break;
        }
        case GizmoMode::Scale:
            for (int i = 0; i < 3; ++i) {
                transform.scale[i] = std::clamp(glm::length(glm::vec3(model[i])), 0.05f, 5.0f);
            }
            break;
        }
    }
}

}  // namespace tinyworld
