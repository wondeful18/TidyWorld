#include "Editor/WorldEditor.h"

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

namespace tinyworld {
namespace {

constexpr std::array kSculptLabels{"Raise", "Lower", "Smooth", "Flatten"};

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

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 场景路径以 UTF-8 交给 ImGui。省略路径时使用工作目录中的默认文件名。
//————————————————————————————————————————————————————————————————————————————————
WorldEditor::WorldEditor(WorldScene& scene, const std::filesystem::path& scenePath, bool sculpt,
                         MaterialPreviews previews, const VegetationDrawStats* vegetationStats)
    : mScene(scene), mPreviews(previews), mVegetationStats(vegetationStats), mSculptEnabled(sculpt && scene.canSculpt) {
    if (mScene.shadows) {
        mSelected.reset();
    }
    if (mScene.spawn) {
        mSelected = mScene.lightmapEnabled ? kLightmapPanel : mScene.atmosphere ? kAtmospherePanel : mScene.water ? kWaterPanel : mScene.canUseGltfProps ? kPropsPanel
            : mScene.vegetation ? kVegetationPanel : mScene.canPlaceProps ? kPropsPanel : mScene.road ? kRoadPanel
            : mScene.materials ? kMaterialPanel : WorldScene::kObjectCount;
        const auto text = scenePath.empty()
            ? std::u8string{mScene.water && mScene.water->coast ? u8"coast-ch15.json"
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
// 按钮与快捷键共用撤销历史。文本编辑和步行时让出快捷键。
//————————————————————————————————————————————————————————————————————————————————
void WorldEditor::DrawToolbar() {
    if (!mScene.canSculpt) {
        return;
    }
    if (!mSculptEnabled || !ImGui::IsMouseDown(ImGuiMouseButton_Left) || mScene.IsWalking()) {
        FinishStroke();
    }
    ImGui::BeginDisabled(mScene.IsWalking());
    const auto& io = ImGui::GetIO();
    const bool shortcut = ImGui::IsWindowFocused() && io.KeyCtrl && !io.WantTextInput &&
                          !ImGui::IsAnyItemActive() && !mScene.IsWalking();
    if (ImGui::Button("Inspect")) {
        SelectForEditing(mSelected);
    }
    for (std::size_t i = 0; i < kSculptLabels.size(); ++i) {
        ImGui::SameLine();
        const auto mode = static_cast<BrushMode>(i);
        const bool active = mSculptEnabled && mBrush.mode == mode;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
        if (ImGui::Button(kSculptLabels[i])) {
            mSculptEnabled = true;
            mMaterialPaint = false;
            mPlacement.reset();
            mBrush.mode = mode;
            if (mScene.materials) mScene.materials->spheres = false;
        }
        if (active) ImGui::PopStyleColor();
    }
    if (mScene.paint) {
        ImGui::SameLine();
        if (ImGui::Button("Paint layers")) {
            SelectForEditing(kMaterialPanel);
            mMaterialPaint = true;
            mScene.materials->spheres = false;
            if (mScene.materials->view == MaterialView::VertexColor) mScene.materials->view = MaterialView::Lit;
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(mUndo.empty() || mPainting);
    const bool undo = ImGui::Button("Undo");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(mRedo.empty() || mPainting);
    const bool redo = ImGui::Button("Redo");
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (!mPainting && (undo || (shortcut && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)))) {
        TravelHistory(mUndo, mRedo);
    } else if (!mPainting && (redo || (shortcut && (ImGui::IsKeyPressed(ImGuiKey_Y, false) ||
                                                      (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)))))) {
        TravelHistory(mRedo, mUndo);
    }
    ImGui::Separator();
}

//————————————————————————————————————————————————————————————————————————————————
// 记录一笔开始前的高度。新笔划使原重做分支失效。
//————————————————————————————————————————————————————————————————————————————————
void WorldEditor::FinishStroke() {
    mPainting = false;
    if (!mStrokeStart) {
        return;
    }
    if (*mStrokeStart != mScene.terrain->Values()) {
        if (mUndo.size() == 24) {
            mUndo.pop_front();
        }
        mUndo.push_back(std::move(*mStrokeStart));
        mRedo.clear();
    }
    mStrokeStart.reset();
}

//————————————————————————————————————————————————————————————————————————————————
// 当前快照进入相反方向的历史，两次操作后可回到相同高度。
//————————————————————————————————————————————————————————————————————————————————
void WorldEditor::TravelHistory(std::deque<HeightField::Samples>& from, std::deque<HeightField::Samples>& to) {
    if (!from.empty()) {
        to.push_back(mScene.terrain->Values());
        mScene.RestoreTerrain(from.back());
        from.pop_back();
    }
}

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
    ImGui::InputTextWithHint("##scene-path", "Scene JSON path", mScenePath.data(), mScenePath.size());
    const bool save = ImGui::Button("Save scene");
    ImGui::SameLine();
    const bool open = ImGui::Button("Open scene");
    ImGui::EndDisabled();
    if (save || open) {
        try {
            const std::string_view text(mScenePath.data());
            const std::filesystem::path path(std::u8string(text.begin(), text.end()));
            if (save) {
                SaveScene(path, mScene);
                mSceneStatus = "Scene saved.";
            } else {
                LoadScene(path, mScene);
                mStrokeStart.reset();
                mUndo.clear();
                mRedo.clear();
                mPainting = false;
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

//————————————————————————————————————————————————————————————————————————————————
// 所有滑块直接修改 CPU 场景，下一次编码立即使用所见数值。
//————————————————————————————————————————————————————————————————————————————————
void WorldEditor::DrawInspector() {
    if (auto& materials = mScene.materials; materials && mSelected == kMaterialPanel && !mSculptEnabled) {
        ImGui::SeparatorText("Terrain materials");
        int view = static_cast<int>(materials->view);
        if (ImGui::Combo("Display", &view, "PBR surface\0Blend weights\0Roughness\0Vertex colors\0")) {
            materials->view = static_cast<MaterialView>(view);
        }
        ImGui::BeginDisabled(mScene.IsWalking());
        if (ImGui::Checkbox("Compare spheres", &materials->spheres) && materials->spheres) {
            mMaterialPaint = false;
        }
        if (mScene.paint && ImGui::Checkbox("Paint surface", &mMaterialPaint) && mMaterialPaint) {
            materials->spheres = false;
            if (materials->view == MaterialView::VertexColor) materials->view = MaterialView::Lit;
        }
        if (mScene.paint && !materials->spheres) {
            DrawMaterialPalette();
        }
        if (mMaterialPaint) {
            ImGui::Checkbox("Square brush", &mMaterialBrush.square);
            ImGui::SliderFloat("Radius (m)", &mMaterialBrush.radius, 0.5f, 6.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("Strength", &mMaterialBrush.strength, 1.0f, 12.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        }
        ImGui::EndDisabled();
        if (materials->spheres) {
            ImGui::TextWrapped("Left to right: dielectric (0.24 / 0.62), metal (0.24 / 0.62).");
        } else if (materials->view == MaterialView::Weights) {
            ImGui::TextWrapped("Grass (green), gravel (red), rock (blue), sand (ochre).");
        }
        return;
    }
    if (auto& lightmap = mScene.lightmapEnabled; lightmap && mSelected == kLightmapPanel && !mSculptEnabled) {
        ImGui::Checkbox("Porch lightmap", &*lightmap);
        if (ImGui::Button("Porch view")) mScene.camera = WorldScene::kPorchCamera;
        for (const auto& prop : mScene.props) {
            if (!prop.lightmap.empty()) ImGui::TextWrapped("%s", prop.lightmap.c_str());
        }
        return;
    }
    if (auto& air = mScene.atmosphere; air && mSelected == kAtmospherePanel && !mSculptEnabled) {
        ImGui::Checkbox("Distance fog", &air->fogEnabled);
        ImGui::BeginDisabled(!air->fogEnabled);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.50f);
        ImGui::SliderFloat("Fog start (m)", &air->fogStart, 0.0f, 60.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.50f);
        ImGui::SliderFloat("Density (1/m)", &air->fogDensity, 0.0f, 0.12f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::EndDisabled();
        ImGui::SeparatorText("Sun shadow");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.50f);
        ImGui::SliderFloat("Distance (m)", &air->shadowDistance, AtmosphereSettings::kMinShadowDistance,
                           AtmosphereSettings::kMaxShadowDistance, "%.1f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::Text("Fade begins at %.1f m", air->shadowDistance * AtmosphereSettings::kShadowFadeRatio);
        return;
    }
    if (mScene.water && mSelected == kWaterPanel && !mSculptEnabled) {
        ImGui::SeparatorText(mScene.water->coast ? "Estuary and coast" : "Lake and stream");
        int mode = static_cast<int>(mScene.water->view);
        if (ImGui::Combo("View", &mode, "Lit\0Depth (0–2 m)\0No reflection\0Off\0")) {
            mScene.water->view = static_cast<WaterView>(mode);
        }
        if (mScene.water->coast && !mScene.IsWalking() && ImGui::Button("Shore view")) {
            mScene.camera = WorldScene::kShoreCamera;
        }
        return;
    }
    if (mScene.vegetation && mSelected == kVegetationPanel && !mSculptEnabled) {
        DrawVegetationInspector();
        return;
    }
    if (mScene.spawn && (mScene.IsDriving() || mScene.IsWalking())) {
        ImGui::TextWrapped("Stop the preview to edit the scene.");
        return;
    }
    if (mSculptEnabled) {
        ImGui::SeparatorText(kSculptLabels[static_cast<std::size_t>(mBrush.mode)]);
        ImGui::SliderFloat("Radius (m)", &mBrush.radius, 0.5f, 7.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Strength", &mBrush.strength, 0.1f, 6.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        if (mBrush.mode == BrushMode::Flatten) {
            ImGui::SliderFloat("Target Y (m)", &mBrush.targetHeight, -HeightField::kHeightLimit,
                              HeightField::kHeightLimit, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::TextWrapped("Shift-click terrain to sample its height.");
        }
        return;
    }
    if (mScene.road && mSelected == kRoadPanel) {
        DrawRoadInspector();
        return;
    }
    if (mScene.canPlaceProps && mSelected == kPropsPanel) {
        DrawPropsPalette();
        return;
    }
    if (mScene.spawn && mSelected == WorldScene::kObjectCount) {
        auto& spawn = *mScene.spawn;
        ImGui::SeparatorText("Walk spawn");
        const float limitX = mScene.terrain ? HeightField::kWalkLimit : WalkSpawn::kLimitX;
        const float limitZ = mScene.terrain ? HeightField::kWalkLimit : WalkSpawn::kLimitZ;
        ImGui::SliderFloat(mScene.terrain ? "World X (m)" : "Ground X (m)", &spawn.groundXZ.x, -limitX, limitX,
                           "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat(mScene.terrain ? "World Z (m)" : "Ground Z (m)", &spawn.groundXZ.y, -limitZ, limitZ,
                           "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Heading (deg)", &spawn.yaw, -180.0f, 180.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
        return;
    }
    if (!mSelected) {
        auto& sun = mScene.sun;
        ImGui::SeparatorText("Directional sunlight");
        ImGui::SliderFloat("Azimuth", &sun.azimuth, -180.0f, 180.0f, "%.0f deg", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Elevation", &sun.elevation, 0.0f, 90.0f, "%.0f deg", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Intensity", &sun.intensity, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        if (auto& shadows = mScene.shadows) {
            ImGui::SeparatorText("Shadow map");
            ImGui::Checkbox("Enable shadows", &shadows->enabled);
            ImGui::BeginDisabled(!shadows->enabled);
            ImGui::Checkbox("3x3 PCF", &shadows->pcf);
            ImGui::SliderFloat("Depth bias", &shadows->depthBias, 0.0f, ShadowSettings::kMaxDepthBias,
                               "%.5f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::EndDisabled();
        }
        return;
    }

    const bool placed = *mSelected >= kFirstProp;
    auto& transform = placed ? mScene.props[*mSelected - kFirstProp].transform : mScene.objects[*mSelected].transform;
    ImGui::SeparatorText(placed ? kPropAssets[mScene.props[*mSelected - kFirstProp].asset].name
                                : mScene.objects[*mSelected].name);
    ImGui::TextUnformatted("Position (m)");
    ImGui::SetNextItemWidth(-1.0f);
    const float positionLimit = placed ? 100.0f : 30.0f;
    ImGui::DragFloat3("##position", glm::value_ptr(transform.position), 0.05f, -positionLimit, positionLimit,
                      "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::TextUnformatted("Rotation (deg)");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::DragFloat3("##rotation", glm::value_ptr(transform.rotationDegrees), 0.5f, -180.0f, 180.0f,
                      "%.1f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::TextUnformatted("Scale");
    ImGui::SetNextItemWidth(-1.0f);
    // 保留正尺度，使模型矩阵可逆，法线的逆转置才有意义。
    ImGui::DragFloat3("##scale", glm::value_ptr(transform.scale), 0.01f, 0.05f, 5.0f,
                      "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SeparatorText("Gizmo");
    if (ImGui::RadioButton("Move", mGizmoMode == GizmoMode::Move)) {
        mGizmoMode = GizmoMode::Move;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Rotate", mGizmoMode == GizmoMode::Rotate)) {
        mGizmoMode = GizmoMode::Rotate;
    }
    if (mScene.canPlaceProps) {
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale", mGizmoMode == GizmoMode::Scale)) {
            mGizmoMode = GizmoMode::Scale;
        }
    }
    if (placed && ImGui::Button("Remove object")) {
        mScene.props.erase(mScene.props.begin() + static_cast<std::ptrdiff_t>(*mSelected - kFirstProp));
        SelectForEditing(kPropsPanel);
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 切换提交策略以对照开销。第 12 章同时允许修改下一批实例的种子和密度。
//————————————————————————————————————————————————————————————————————————————————
void WorldEditor::DrawVegetationInspector() {
    auto& settings = *mScene.vegetation;
    const bool rules = settings.mode == VegetationMode::TerrainRules;
    ImGui::SeparatorText(rules ? "Terrain ecology" : "Meadow and treeline");
    if (ImGui::Checkbox("Show vegetation", &settings.visible) && settings.visible && mScene.materials) {
        mScene.materials->spheres = false;
    }
    ImGui::Checkbox("Instanced drawing", &settings.instanced);
    if (rules) {
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.64f);
        ImGui::InputScalar("Seed", ImGuiDataType_U32, &settings.seed, nullptr, nullptr, "%u",
                           ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SliderFloat("Density", &settings.density, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    }
    if (mVegetationStats) {
        const auto& stats = *mVegetationStats;
        ImGui::SeparatorText("Instances");
        ImGui::Text("Grass %u  Trees %u", stats.instances[0], stats.instances[1]);
        if (rules) ImGui::Text("Rocks %u", stats.instances[2]);
        ImGui::SeparatorText("Vegetation draw calls");
        ImGui::Text("Color %u   Shadow %u", stats.colorDraws, stats.shadowDraws);
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 以两行两列显示真实底色贴图，点选任一格立即启用对应权重的笔刷。
//————————————————————————————————————————————————————————————————————————————————
void WorldEditor::DrawMaterialPalette() {
    constexpr std::array labels{"Grass", "Gravel", "Rock", "Sand"};
    const auto& style = ImGui::GetStyle();
    const float cell = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;
    const float imageSide = std::clamp(cell - 2.0f * style.FramePadding.x, 18.0f, 64.0f);
    const auto choose = [&](int layer) {
        mMaterialBrush.layer = layer;
        mMaterialPaint = true;
        if (mScene.materials->view == MaterialView::VertexColor) {
            mScene.materials->view = MaterialView::Lit;
        }
    };
    for (std::size_t i = 0; i < labels.size(); ++i) {
        if (i % 2 == 1) ImGui::SameLine();
        ImGui::BeginGroup();
        const bool selected = mMaterialPaint && mMaterialBrush.layer == static_cast<int>(i);
        bool clicked = false;
        if (const auto view = mPreviews[i]) {
            const auto texture = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(view));
            clicked = ImGui::ImageButton(labels[i], ImTextureRef{texture}, {imageSide, imageSide});
        } else {
            clicked = ImGui::Button(labels[i], {imageSide, imageSide});
        }
        if (selected) {
            auto* list = ImGui::GetWindowDrawList();
            list->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(101, 237, 212, 255),
                          style.FrameRounding, 0, 2.5f);
        }
        if (clicked) choose(static_cast<int>(i));
        ImGui::TextUnformatted(labels[i]);
        ImGui::EndGroup();
    }
    if (ImGui::RadioButton("Erase weights", mMaterialPaint && mMaterialBrush.layer == 4)) {
        choose(4);
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 选中资产后等待地形落点。
//————————————————————————————————————————————————————————————————————————————————
void WorldEditor::DrawPropsPalette() {
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
    const bool editingRoad = mScene.road && mSelected == kRoadPanel && !mSculptEnabled && !spheres;
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
        const auto key = HeightField::ChunkAt({feet.x, feet.z});
        const bool coarse = mScene.previewLod && IsCoarseTerrainChunk(key, mScene.ActiveCamera().Position());
        std::snprintf(terrainHint, sizeof(terrainHint), "%s\n%s chunk (%d, %d)   feet Y %.2f m%s",
                      hint, walking ? "Robot" : "Spawn", key.x, key.y, static_cast<double>(feet.y),
                      mScene.previewLod ? (coarse ? "   LOD 1" : "   LOD 0") : "");
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
        const float maxDistance = mScene.terrain ? 65.0f : 33.0f;
        camera.distance = std::clamp(camera.distance * std::exp(-0.095f * io.MouseWheel), 5.0f, maxDistance);
    }

    if (!mSelected || mOrbiting || mSculptEnabled || mPlacement) return;
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

//————————————————————————————————————————————————————————————————————————————————
// 命中点作为笔刷圆心。圆环按地表高度投到视口，供观察作用半径。
//————————————————————————————————————————————————————————————————————————————————
void WorldEditor::DrawTerrainBrush(PixelSize size, float deltaSeconds) {
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || mOrbiting) {
        FinishStroke();
    }
    if (!ImGui::IsItemHovered() || mOrbiting) {
        return;
    }
    const auto low = ImGui::GetItemRectMin();
    const auto high = ImGui::GetItemRectMax();
    const auto& camera = mScene.camera;
    const auto vp = camera.Projection(static_cast<float>(size.width) / size.height) * camera.View();
    const auto& field = *mScene.terrain;
    const auto& io = ImGui::GetIO();
    glm::vec3 hit;
    if (!PickTerrain(field, vp, low, high, io.MousePos, hit)) {
        return;
    }
    const glm::vec2 center{hit.x, hit.z};
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (mBrush.mode == BrushMode::Flatten && io.KeyShift) {
            mBrush.targetHeight = hit.y;
        } else {
            mPainting = true;
            mStrokeStart = field.Values();
        }
    }
    if (mPainting) {
        mScene.SculptTerrain(center, mBrush, std::clamp(deltaSeconds, 0.0f, 0.05f));
    }

    BrushOutline(field, center, mBrush.radius, false, vp, low, high);
}

//————————————————————————————————————————————————————————————————————————————————
// 高度只用来拾取和显示轮廓，笔刷写入的是世界 XZ 对应的 RGBA 像素。
//————————————————————————————————————————————————————————————————————————————————
void WorldEditor::DrawMaterialBrush(PixelSize size, float deltaSeconds) {
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || mOrbiting) {
        mPaintDragging = false;
    }
    if (!ImGui::IsItemHovered() || mOrbiting) {
        return;
    }
    const auto low = ImGui::GetItemRectMin();
    const auto high = ImGui::GetItemRectMax();
    const auto& camera = mScene.camera;
    const auto vp = camera.Projection(static_cast<float>(size.width) / size.height) * camera.View();
    const auto& field = *mScene.terrain;
    glm::vec3 hit;
    if (!PickTerrain(field, vp, low, high, ImGui::GetIO().MousePos, hit)) {
        return;
    }
    const glm::vec2 center{hit.x, hit.z};
    mPaintDragging |= ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    if (mPaintDragging) {
        mScene.paint->Paint(center, mMaterialBrush, std::clamp(deltaSeconds, 0.0f, 0.05f));
    }
    BrushOutline(field, center, mMaterialBrush.radius, mMaterialBrush.square, vp, low, high);
}

//————————————————————————————————————————————————————————————————————————————————
// 世界坐标与视口拖动共享同一份控制点；输出时按当前点和边距重算遮罩。
//————————————————————————————————————————————————————————————————————————————————
void WorldEditor::DrawRoadInspector() {
    auto& road = *mScene.road;
    mRoadPoint = std::min(mRoadPoint, road.points.size() - 1);
    ImGui::SeparatorText("Spline road");
    int selected = static_cast<int>(mRoadPoint + 1);
    if (ImGui::SliderInt("Control point", &selected, 1, static_cast<int>(road.points.size()), "%d",
                         ImGuiSliderFlags_AlwaysClamp)) {
        mRoadPoint = static_cast<std::size_t>(selected - 1);
    }
    auto& point = road.points[mRoadPoint];
    ImGui::DragFloat("World X (m)", &point.x, 0.05f, -RoadSpline::kPointLimit, RoadSpline::kPointLimit,
                     "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloat("Y offset (m)", &point.y, 0.05f, -RoadSpline::kHeightOffsetLimit, RoadSpline::kHeightOffsetLimit,
                     "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloat("World Z (m)", &point.z, 0.05f, -RoadSpline::kPointLimit, RoadSpline::kPointLimit,
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
            ExportRoadMask(path, road);
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
    for (const auto& path : {road.Centerline(), road.Branch()}) {
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
            const auto xz = glm::clamp(glm::vec2{hit.x, hit.z}, -RoadSpline::kPointLimit, RoadSpline::kPointLimit);
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
        const glm::vec3 limit{RoadSpline::kPointLimit, RoadSpline::kHeightOffsetLimit, RoadSpline::kPointLimit};
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
