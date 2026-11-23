#include "Editor/WorldEditor.h"

#include "Engine/Scene/SceneFile.h"
#include "Engine/Terrain/TerrainMesh.h"

#include <imgui.h>
#include <ImGuizmo.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <string_view>

namespace tinyworld {
namespace {

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
WorldEditor::WorldEditor(WorldScene& scene, const std::filesystem::path& scenePath, bool sculpt)
    : mScene(scene), mSculptEnabled(sculpt && scene.canSculpt) {
    if (mScene.shadows) {
        mSelected.reset();
    }
    if (mScene.spawn) {
        mSelected = mScene.road ? kRoadPanel : mScene.materials ? kMaterialPanel : WorldScene::kObjectCount;
        const auto text = scenePath.empty()
            ? std::u8string{mScene.road ? u8"valley-ch09.json"
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
        mSculptEnabled = false;
        mMaterialPaint = false;
    }
    constexpr std::array labels{"Raise", "Lower", "Smooth", "Flatten"};
    for (std::size_t i = 0; i < labels.size(); ++i) {
        ImGui::SameLine();
        if (ImGui::Selectable(labels[i], mSculptEnabled && mBrush.mode == static_cast<BrushMode>(i),
                              ImGuiSelectableFlags_None, {ImGui::CalcTextSize(labels[i]).x + 9.0f, 0.0f})) {
            mSculptEnabled = true;
            mMaterialPaint = false;
            mBrush.mode = static_cast<BrushMode>(i);
            if (mScene.materials) mScene.materials->spheres = false;
        }
    }
    if (mScene.paint) {
        ImGui::SameLine();
        if (ImGui::Button("Paint layers")) {
            mSelected = kMaterialPanel;
            mSculptEnabled = false;
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
                mSelected = index;
                mSculptEnabled = false;
                mMaterialPaint = false;
            }
        }
        if (mScene.canSculpt) ImGui::TreePop();
    }
    if (ImGui::Selectable("Sun", !mSelected)) {
        mSelected.reset();
    }
    if (mScene.spawn && ImGui::Selectable("Spawn", mSelected == WorldScene::kObjectCount)) {
        mSelected = WorldScene::kObjectCount;
    }
    if (mScene.materials && ImGui::Selectable("Materials", mSelected == kMaterialPanel)) {
        mSelected = kMaterialPanel;
        mSculptEnabled = false;
    }
    if (mScene.road && ImGui::Selectable("Road", mSelected == kRoadPanel)) {
        mSelected = kRoadPanel;
        mSculptEnabled = false;
        mMaterialPaint = false;
        mScene.materials->spheres = false;
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
                }
            }
        } else {
            ImGui::SameLine();
        }
        const bool walking = mScene.IsWalking();
        if (ImGui::Button(walking ? "Stop walking" : "Walk from spawn")) {
            mScene.SetWalking(!walking);
            if (!walking) {
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
                mDraggingRoadPoint = false;
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
        if (mMaterialPaint) {
            ImGui::Combo("Layer", &mMaterialBrush.layer, "R Grass\0G Gravel\0B Rock\0A Sand\0Erase\0");
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
    if (mScene.spawn && (mScene.IsDriving() || mScene.IsWalking())) {
        ImGui::TextWrapped("Stop the preview to edit the scene.");
        return;
    }
    if (mSculptEnabled) {
        ImGui::SeparatorText("Terrain brush");
        int mode = static_cast<int>(mBrush.mode);
        if (ImGui::Combo("Mode", &mode, "Raise\0Lower\0Smooth\0Flatten\0")) {
            mBrush.mode = static_cast<BrushMode>(mode);
        }
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

    auto& object = mScene.objects[*mSelected];
    auto& transform = object.transform;
    ImGui::SeparatorText(object.name);
    ImGui::TextUnformatted("Position (m)");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::DragFloat3("##position", glm::value_ptr(transform.position), 0.05f, -30.0f, 30.0f,
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
    if (ImGui::RadioButton("Move", !mRotate)) {
        mRotate = false;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Rotate", mRotate)) {
        mRotate = true;
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
        mDraggingRoadPoint = false;
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
        }
    }
    const char* hint = spheres ? "Fixed material study   |   roughness 0.24 / 0.62 per pair"
        : walking ? "W/S: walk   A/D: turn   Arrows   R / Home: respawn"
        : driving ? "WASD / Arrows: drive   R / Home: reset"
        : editingRoad ? "Drag road marker   Shift-click: insert after selection"
        : paintingMaterial ? "Left drag: paint layer   Right drag: orbit"
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
    const ImVec2 low = ImGui::GetItemRectMin();
    const ImVec2 high = ImGui::GetItemRectMax();
    const bool hovered = ImGui::IsItemHovered();
    auto& camera = mScene.camera;
    const auto& io = ImGui::GetIO();
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !ImGuizmo::IsUsing()) {
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
    if (hovered && io.MouseWheel != 0.0f) {
        const float maxDistance = mScene.terrain ? 65.0f : 33.0f;
        camera.distance = std::clamp(camera.distance * std::exp(-0.095f * io.MouseWheel), 5.0f, maxDistance);
    }

    if (mSelected && *mSelected < mScene.objects.size() && !mOrbiting && !mSculptEnabled) {
        auto& transform = mScene.objects[*mSelected].transform;
        glm::mat4 model = transform.Matrix();
        const glm::mat4 view = camera.View();
        const glm::mat4 project = camera.Projection(static_cast<float>(size.width) / size.height, false);
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
        ImGuizmo::SetRect(low.x, low.y, high.x - low.x, high.y - low.y);
        const auto operation = mRotate ? ImGuizmo::ROTATE : ImGuizmo::TRANSLATE;
        if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(project), operation,
                                ImGuizmo::LOCAL, glm::value_ptr(model))) {
            transform.position = glm::vec3(model[3]);
            if (mRotate) {
                // 旋转手柄只更新方向，独立保留 Inspector 中的三轴缩放。
                const glm::mat3 rotation{glm::normalize(glm::vec3(model[0])),
                                         glm::normalize(glm::vec3(model[1])),
                                         glm::normalize(glm::vec3(model[2]))};
                transform.rotationDegrees = glm::degrees(glm::eulerAngles(glm::quat_cast(rotation)));
            }
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
    ImGui::DragFloat2("World XZ (m)", glm::value_ptr(road.points[mRoadPoint]), 0.05f,
                      -RoadSpline::kPointLimit, RoadSpline::kPointLimit, "%.2f", ImGuiSliderFlags_AlwaysClamp);
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
// 点击屏幕上的圆点选择控制点，拖动时把地面命中的 XZ 写回道路。
//————————————————————————————————————————————————————————————————————————————————
void WorldEditor::DrawRoadEditor(PixelSize size) {
    auto& road = *mScene.road;
    const auto& field = *mScene.terrain;
    const auto low = ImGui::GetItemRectMin();
    const auto high = ImGui::GetItemRectMax();
    const auto& camera = mScene.camera;
    const auto vp = camera.Projection(static_cast<float>(size.width) / size.height) * camera.View();
    const auto mouse = ImGui::GetIO().MousePos;
    const auto toScreen = [&](glm::vec2 p, ImVec2& marker) {
        return ViewportPoint({p.x, field.HeightAt(p) + 0.12f, p.y}, vp, low, high, marker);
    };
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || mOrbiting) {
        mDraggingRoadPoint = false;
    }
    const bool canEdit = ImGui::IsItemHovered() && !mOrbiting;
    if (canEdit && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        mDraggingRoadPoint = false;
        if (ImGui::GetIO().KeyShift) {
            glm::vec3 hit;
            if (road.points.size() < RoadSpline::kMaxPoints && PickTerrain(field, vp, low, high, mouse, hit)) {
                const auto point = glm::clamp(glm::vec2{hit.x, hit.z}, -RoadSpline::kPointLimit, RoadSpline::kPointLimit);
                ++mRoadPoint;
                if (road.junctionPoint >= mRoadPoint) ++road.junctionPoint;
                road.points.insert(road.points.begin() + static_cast<std::ptrdiff_t>(mRoadPoint), point);
                road.junctionPoint = std::min(road.junctionPoint, road.points.size() - 1);
            }
        } else {
            float closest = 13.0f * 13.0f;
            for (std::size_t i = 0; i < road.points.size(); ++i) {
                ImVec2 point;
                if (toScreen(road.points[i], point)) {
                    const float dx = point.x - mouse.x;
                    const float dy = point.y - mouse.y;
                    const float distance = dx * dx + dy * dy;
                    if (distance < closest) {
                        closest = distance;
                        mRoadPoint = i;
                        mDraggingRoadPoint = true;
                    }
                }
            }
        }
    }
    if (canEdit && mDraggingRoadPoint && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        glm::vec3 hit;
        if (PickTerrain(field, vp, low, high, mouse, hit)) {
            road.points[mRoadPoint] = glm::clamp(glm::vec2{hit.x, hit.z}, -RoadSpline::kPointLimit, RoadSpline::kPointLimit);
        }
    }
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
}

}  // namespace tinyworld
