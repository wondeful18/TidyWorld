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
// 通过同一份预设函数恢复默认布局或构造两个视觉对照。
//————————————————————————————————————————————————————————————————————————————————
void WorldEditor::DrawHierarchy() {
    const auto objectCount = mScene.spawn ? WorldScene::kObjectCount : WorldScene::kCarIndex;
    if (!mScene.canSculpt || ImGui::TreeNodeEx("Valley objects", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (std::size_t index = mScene.terrain ? 1 : 0; index < objectCount; ++index) {
            if (ImGui::Selectable(mScene.objects[index].name, mSelected == index)) {
                SelectForEditing(index);
            }
        }
        if (mScene.canSculpt) ImGui::TreePop();
    }
    if (ImGui::Selectable("Sun", !mSelected)) {
        SelectForEditing(std::nullopt);
    }
    if (mScene.spawn && ImGui::Selectable("Spawn", mSelected == WorldScene::kObjectCount)) {
        SelectForEditing(WorldScene::kObjectCount);
    }
    if (mScene.materials && ImGui::Selectable("Materials", mSelected == kMaterialPanel)) {
        SelectForEditing(kMaterialPanel);
    }
    if (mScene.road && ImGui::Selectable("Road", mSelected == kRoadPanel)) {
        SelectForEditing(kRoadPanel);
    }
    if (mScene.vegetation && ImGui::Selectable("Vegetation", mSelected == kVegetationPanel)) {
        SelectForEditing(kVegetationPanel);
    }
    if (mScene.water && ImGui::Selectable("Water", mSelected == kWaterPanel)) {
        SelectForEditing(kWaterPanel);
    }
    if (mScene.capstone && ImGui::Selectable("Forest to Cape", mSelected == kTourPanel)) {
        SelectForEditing(kTourPanel);
    }
    if (mScene.terrainPreview && ImGui::Selectable("Terrain performance", mSelected == kTerrainPanel)) {
        SelectForEditing(kTerrainPanel);
    }
    if (mScene.dayCycle && ImGui::Selectable("Day / night", mSelected == kDayCyclePanel)) {
        SelectForEditing(kDayCyclePanel);
    }
    if (mScene.lightmapEnabled && ImGui::Selectable("Baked lighting", mSelected == kLightmapPanel)) {
        SelectForEditing(kLightmapPanel);
    }
    if (mScene.atmosphere && ImGui::Selectable("Atmosphere", mSelected == kAtmospherePanel)) {
        SelectForEditing(kAtmospherePanel);
    }
    if (mScene.canPlaceProps) {
        if (ImGui::Selectable("Place objects", mSelected == kPropsPanel)) {
            SelectForEditing(kPropsPanel);
        }
        for (std::size_t i = 0; i < mScene.props.size(); ++i) {
            const auto& prop = mScene.props[i];
            const auto name = std::string(kPropAssets[prop.asset].name) + " " + std::to_string(i + 1);
            if (ImGui::Selectable(name.c_str(), mSelected == kFirstProp + i)) {
                SelectForEditing(kFirstProp + i);
            }
        }
    }
    ImGui::SeparatorText("Scene demos");
    if (ImGui::Button(mScene.canSculpt ? "Reset layout" : "Reset")) {
        mScene.ApplyPreset(WorldPreset::Default);
    }
    ImGui::SameLine();
    if (ImGui::Button("Overlap")) {
        mScene.ApplyPreset(WorldPreset::Overlap);
    }
    ImGui::SameLine();
    if (ImGui::Button("Evening")) {
        mScene.ApplyPreset(WorldPreset::Evening);
    }
    const bool driving = mScene.IsDriving();
    if (!mScene.terrain && ImGui::Button(driving ? "Stop driving" : "Drive car")) {
        mScene.SetDriving(!driving);
        if (!driving) {
            ImGui::SetWindowFocus("Scene Viewport");
        }
    }
    if (mScene.spawn) {
        if (mScene.terrain) {
            ImGui::SeparatorText("Terrain");
            ImGui::Checkbox("Chunk borders", &mScene.showChunkBorders);
            if (mScene.canPreviewLod) {
                ImGui::Checkbox("LOD preview", &mScene.previewLod);
            }
            if (mScene.canSculpt) {
                if (ImGui::Checkbox("Sculpt terrain", &mSculptEnabled) && mSculptEnabled && mScene.materials) {
                    mScene.materials->spheres = false;
                    mMaterialPaint = false;
                    mPlacement.reset();
                }
            }
        } else {
            ImGui::SameLine();
        }
        const bool walking = mScene.IsWalking();
        if (ImGui::Button(walking ? "Stop walking" : "Walk from spawn")) {
            mScene.SetWalking(!walking);
            if (!walking) {
                mPlacement.reset();
                ImGui::SetWindowFocus("Scene Viewport");
            }
        }
        if (!mScene.terrain || mScene.canSculpt) {
            DrawSceneFileControls();
        }
    }
    if (auto& environment = mScene.environment) {
        ImGui::SeparatorText("Environment");
        if (ImGui::RadioButton("Cubemap skybox", environment->sky == SkyMode::Cubemap)) {
            environment->sky = SkyMode::Cubemap;
        }
        if (ImGui::RadioButton("Panorama sphere", environment->sky == SkyMode::Panorama)) {
            environment->sky = SkyMode::Panorama;
        }
        ImGui::Checkbox("Car reflection", &environment->carReflection);
    }
}

void WorldEditor::SelectForEditing(std::optional<std::size_t> selection) {
    mSelected = selection;
    mSculptEnabled = false;
    mMaterialPaint = false;
    mPlacement.reset();
    if (mScene.materials && selection && *selection >= kRoadPanel) {
        mScene.materials->spheres = false;
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 保存和重开共用输入路径。失败消息留在面板，调用者可以修正路径或存档后重试。
//————————————————————————————————————————————————————————————————————————————————
void WorldEditor::DrawSceneFileControls() {
    ImGui::SeparatorText("Scene file");
    ImGui::BeginDisabled(mScene.IsDriving() || mScene.IsWalking());
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##scene-path", "Scene JSON or world directory", mScenePath.data(), mScenePath.size());
    ImGui::BeginDisabled(mScene.storage.has_value());
    const bool save = ImGui::Button("Save JSON");
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool open = ImGui::Button("Open scene");
    bool pack = false;
    if (mScene.streamTerrain && !mScene.storage) {
        pack = ImGui::Button("Save world package");
        if (mScene.capstone) {
            ImGui::SameLine();
            ImGui::Checkbox("1024 m", &mPackExploration);
        }
    }
    ImGui::EndDisabled();
    if (save || open || pack) {
        try {
            const std::string_view text(mScenePath.data());
            const std::filesystem::path path(std::u8string(text.begin(), text.end()));
            if (pack) {
                SaveWorldPackage(path, mScene, mPackExploration);
                mSceneStatus = "World package updated.";
            } else if (save) {
                SaveScene(path, mScene);
                mSceneStatus = "Scene saved.";
            } else {
                LoadScene(path, mScene);
                mStrokeStart.reset();
                mUndo.clear();
                mRedo.clear();
                mPainting = false;
                if (mScene.storage) mSculptEnabled = false;
                mOrbiting = false;
                mRoadPoint = 0;
                mPlacement.reset();
                if (mSelected && *mSelected >= kFirstProp) mSelected = kPropsPanel;
                mSceneStatus = "Scene opened.";
            }
        } catch (const std::exception& error) {
            mSceneStatus = error.what();
        }
    }
    if (!mSceneStatus.empty()) {
        ImGui::TextWrapped("%s", mSceneStatus.c_str());
    }
}

}  // namespace tinyworld
