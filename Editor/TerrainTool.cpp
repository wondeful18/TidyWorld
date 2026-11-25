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
        mScene.paint->Paint(center, mMaterialBrush, std::clamp(deltaSeconds, 0.0f, 0.05f), field.Grid().HalfExtent());
    }
    BrushOutline(field, center, mMaterialBrush.radius, mMaterialBrush.square, vp, low, high);
}

}  // namespace tinyworld
