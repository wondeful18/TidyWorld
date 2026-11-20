#include "Editor/WorldEditor.h"

#include <imgui.h>
#include <ImGuizmo.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 通过同一份预设函数恢复默认布局或构造两个视觉对照。
//————————————————————————————————————————————————————————————————————————————————
void WorldEditor::DrawHierarchy() {
    for (std::size_t index = 0; index < mScene.objects.size(); ++index) {
        if (ImGui::Selectable(mScene.objects[index].name, mSelected == index)) {
            mSelected = index;
        }
    }
    if (ImGui::Selectable("Sun", !mSelected)) {
        mSelected.reset();
    }
    ImGui::SeparatorText("Scene demos");
    if (ImGui::Button("Reset")) {
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
}

//————————————————————————————————————————————————————————————————————————————————
// 所有滑块直接修改 CPU 场景，下一次编码立即使用所见数值。
//————————————————————————————————————————————————————————————————————————————————
void WorldEditor::DrawInspector() {
    if (!mSelected) {
        auto& sun = mScene.sun;
        ImGui::SeparatorText("Directional sunlight");
        ImGui::SliderFloat("Azimuth", &sun.azimuth, -180.0f, 180.0f, "%.0f deg", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Elevation", &sun.elevation, 0.0f, 90.0f, "%.0f deg", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Intensity", &sun.intensity, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
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
// 右键和滚轮驱动相机，左键仍留给变换手柄。
// 手柄使用与场景一致的视图与纵横比，只换用它所需的裁剪深度区间。
//————————————————————————————————————————————————————————————————————————————————
void WorldEditor::DrawViewport(PixelSize size) {
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
        camera.elevation = std::clamp(camera.elevation + 0.25f * io.MouseDelta.y, 10.0f, 80.0f);
    }
    if (hovered && io.MouseWheel != 0.0f) {
        camera.distance = std::clamp(camera.distance * std::exp(-0.095f * io.MouseWheel), 5.0f, 33.0f);
    }

    auto* list = ImGui::GetWindowDrawList();
    list->PushClipRect(low, high, true);
    if (mSelected && !mOrbiting) {
        auto& transform = mScene.objects[*mSelected].transform;
        glm::mat4 model = transform.Matrix();
        const glm::mat4 view = camera.View();
        const glm::mat4 project = camera.Projection(static_cast<float>(size.width) / size.height, false);
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist(list);
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
    constexpr auto hint = "Right drag: orbit   Wheel: zoom";
    const ImVec2 label = ImGui::CalcTextSize(hint);
    list->AddRectFilled({low.x + 10.0f, low.y + 10.0f}, {low.x + label.x + 26.0f, low.y + label.y + 22.0f},
                        IM_COL32(25, 41, 47, 218), 5.0f);
    list->AddText({low.x + 18.0f, low.y + 16.0f}, IM_COL32(240, 249, 248, 255), hint);
    list->PopClipRect();
}

}  // namespace tinyworld
