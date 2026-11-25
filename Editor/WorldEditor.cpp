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
// 场景路径以 UTF-8 交给 ImGui。省略路径时使用工作目录中的默认文件名。
//————————————————————————————————————————————————————————————————————————————————
WorldEditor::WorldEditor(WorldScene& scene, const std::filesystem::path& scenePath, bool sculpt,
                         MaterialPreviews previews, const VegetationDrawStats* vegetationStats,
                         const TerrainDrawStats* terrainStats, const TerrainStreamStats* streamStats,
                         const unsigned* sceneDraws)
    : mScene(scene), mPreviews(previews), mVegetationStats(vegetationStats), mStreamStats(streamStats),
      mSceneDraws(sceneDraws), mTerrainStats(terrainStats), mSculptEnabled(sculpt && scene.canSculpt) {
    if (mScene.shadows) {
        mSelected.reset();
    }
    if (mScene.spawn) {
        mSelected = mScene.capstone ? kTourPanel : mScene.terrainPreview ? kTerrainPanel : mScene.dayCycle ? kDayCyclePanel : mScene.lightmapEnabled ? kLightmapPanel : mScene.atmosphere ? kAtmospherePanel : mScene.water ? kWaterPanel : mScene.canUseGltfProps ? kPropsPanel
            : mScene.vegetation ? kVegetationPanel : mScene.canPlaceProps ? kPropsPanel : mScene.road ? kRoadPanel
            : mScene.materials ? kMaterialPanel : WorldScene::kObjectCount;
        const auto text = mScene.storage ? mScene.storage->directory.u8string() : scenePath.empty()
            ? std::u8string{mScene.capstone ? u8"tour-ch21.json" : mScene.water && mScene.water->coast ? u8"coast-ch15.json"
                : mScene.water ? u8"valley-ch14.json" : mScene.canUseGltfProps ? u8"village-ch13.json"
                : mScene.vegetation ? (mScene.vegetation->mode == VegetationMode::TerrainRules
                                                ? u8"valley-ch12.json" : u8"valley-ch11.json")
                : mScene.canPlaceProps ? u8"valley-ch10.json" : mScene.road ? u8"valley-ch09.json"
                : mScene.canSculpt ? u8"valley-ch07.json" : u8"scene-ch05.json"}
            : scenePath.u8string();
        if (text.size() < mScenePath.size()) {
            std::copy(text.begin(), text.end(), mScenePath.begin());
        }
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 按当前模式更新相机并在场景图片内显示对应操作提示。
//————————————————————————————————————————————————————————————————————————————————
void WorldEditor::DrawViewport(PixelSize size, float deltaSeconds) {
    const ImVec2 low = ImGui::GetItemRectMin();
    const ImVec2 high = ImGui::GetItemRectMax();
    auto* list = ImGui::GetWindowDrawList();
    list->PushClipRect(low, high, true);
    const bool driving = mScene.IsDriving();
    const bool walking = mScene.IsWalking();
    const bool spheres = mScene.materials && mScene.materials->spheres;
    const bool editingRoad = !mScene.storage && mScene.road && mSelected == kRoadPanel && !mSculptEnabled && !spheres;
    const bool paintingMaterial = mScene.paint && mMaterialPaint && mSelected == kMaterialPanel && !spheres;
    if (driving || walking) {
        mOrbiting = false;
        mPainting = false;
        mPaintDragging = false;
        UpdateRoam(deltaSeconds);
    } else if (!spheres) {
        DrawEditControls(size);
        if (mSculptEnabled) {
            DrawTerrainBrush(size, deltaSeconds);
        } else if (paintingMaterial) {
            DrawMaterialBrush(size, deltaSeconds);
        } else if (editingRoad) {
            DrawRoadEditor(size);
        } else if (mPlacement) {
            DrawPropPlacement(size);
        }
    }
    const char* hint = spheres ? "Fixed material study   |   roughness 0.24 / 0.62 per pair"
        : walking ? "W/S: walk   A/D: turn   Arrows   R / Home: respawn"
        : driving ? "WASD / Arrows: drive   R / Home: reset"
        : editingRoad ? "Select a node and drag a local axis   Shift-click: insert"
        : paintingMaterial ? "Left drag: paint layer   Right drag: orbit"
        : mPlacement ? "Click terrain to place   Esc: cancel"
        : mSculptEnabled ? "Left drag: sculpt   Right drag: orbit   Wheel: zoom" : "Right drag: orbit   Wheel: zoom";
    char terrainHint[160]{};
    if (mScene.terrain && !spheres) {
        const auto feet = mScene.WalkerPose().position;
        const auto key = mScene.terrain->Grid().KeyAt({feet.x, feet.z});
        const bool waiting = mScene.storage && !mScene.terrain->TryHeight({feet.x, feet.z});
        const bool coarse = mScene.previewLod && IsCoarseTerrainChunk(key, mScene.ActiveCamera().Position());
        std::snprintf(terrainHint, sizeof(terrainHint), "%s\n%s chunk (%d, %d)   feet Y %.2f m%s",
                      hint, walking ? "Robot" : "Spawn", key.x, key.y, static_cast<double>(feet.y),
                      waiting ? "   Waiting for terrain" : mScene.previewLod ? (coarse ? "   LOD 1" : "   LOD 0") : "");
        hint = terrainHint;
    }
    const ImVec2 label = ImGui::CalcTextSize(hint);
    list->AddRectFilled({low.x + 10.0f, low.y + 10.0f}, {low.x + label.x + 26.0f, low.y + label.y + 22.0f},
                        IM_COL32(25, 41, 47, 218), 5.0f);
    list->AddText({low.x + 18.0f, low.y + 16.0f}, IM_COL32(240, 249, 248, 255), hint);
    list->PopClipRect();
}

//————————————————————————————————————————————————————————————————————————————————
// 焦点离开视口时释放游览输入。步行立即停下，小车自然减速。
//————————————————————————————————————————————————————————————————————————————————
void WorldEditor::UpdateRoam(float deltaSeconds) {
    CarInput input;
    const auto& io = ImGui::GetIO();
    if (ImGui::IsWindowFocused() && !ImGui::IsAnyItemActive() && !io.WantTextInput &&
        !io.KeyCtrl && !io.KeyAlt && !io.KeySuper) {
        // 方向键和 Home 在中文输入法开启时也可使用。
        const bool forward = ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_UpArrow);
        const bool reverse = ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow);
        const bool left = ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow);
        const bool right = ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow);
        input.throttle = static_cast<float>(forward - reverse);
        input.steering = static_cast<float>(left - right);
        input.reset = ImGui::IsKeyDown(ImGuiKey_R) || ImGui::IsKeyDown(ImGuiKey_Home);
    }
    if (mScene.IsWalking()) {
        mScene.UpdateWalk({input.throttle, input.steering, input.reset}, deltaSeconds);
    } else {
        mScene.UpdateDrive(input, deltaSeconds);
    }
}

}  // namespace tinyworld
