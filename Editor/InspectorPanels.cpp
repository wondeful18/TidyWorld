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
        if (!mScene.storage && ImGui::Checkbox("Compare spheres", &materials->spheres) && materials->spheres) {
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
    if (mScene.capstone && mSelected == kTourPanel && !mSculptEnabled) {
        const auto grid = mScene.terrain->Grid();
        ImGui::Text("%d x %d chunks | %.0f m wide", grid.Side(), grid.Side(), grid.HalfExtent() * 2.0f);
        if (ImGui::Button(mScene.IsWalking() ? "Stop walking" : "Walk from start")) {
            mScene.SetWalking(!mScene.IsWalking());
            if (mScene.IsWalking()) ImGui::SetWindowFocus("Scene Viewport");
        }
        ImGui::BeginDisabled(mScene.IsWalking() || grid == TerrainGrid{});
        for (std::size_t i = 0; i < kTourStops.size(); ++i) {
            if (i % 2 == 1) ImGui::SameLine();
            const auto& stop = kTourStops[i];
            if (ImGui::Button(stop.label)) mScene.camera = stop.camera;
        }
        ImGui::EndDisabled();
        if (ImGui::Button("Noon")) *mScene.dayCycle = {12.0f, false};
        ImGui::SameLine();
        if (ImGui::Button("Night")) *mScene.dayCycle = {21.0f, false};
        if (grid == TerrainGrid::Exploration()) {
            ImGui::BeginDisabled(mScene.IsWalking());
            if (mScene.road && mScene.road->points.size() > 1 && mScene.road->points.front().x < -100.0f &&
                ImGui::Button("West trailhead")) {
                const auto start = mScene.road->points.front();
                const auto forward = mScene.road->points[1] - start;
                mScene.spawn = WalkSpawn{{start.x, start.z}, glm::degrees(std::atan2(forward.x, forward.z))};
                mScene.camera = {{start.x, 5.0f, start.z}, 230.0f, 22.0f, 36.0f};
            }
            if (ImGui::Button("World overview")) mScene.camera = {{-32.0f, 0.0f, 0.0f}, 236.0f, 32.0f, 220.0f};
            ImGui::EndDisabled();
            ImGui::Checkbox("Four shadow cascades", &mScene.precision.cascades);
            ImGui::SeparatorText("World coordinate precision");
            ImGui::Checkbox("World origin +100 km", &mScene.precision.offsetWorld);
            ImGui::BeginDisabled(!mScene.precision.offsetWorld);
            ImGui::Checkbox("Camera-relative rendering", &mScene.precision.cameraRelative);
            ImGui::SliderFloat("Camera X nudge", &mScene.precision.cameraNudge, -0.03f, 0.03f, "%.4f m");
            ImGui::EndDisabled();
        }
        if (mSceneDraws) ImGui::Text("Scene draw calls: %u", *mSceneDraws);
        return;
    }
    if (auto& terrain = mScene.terrainPreview; terrain && mSelected == kTerrainPanel && !mSculptEnabled) {
        ImGui::Checkbox("Distance LOD", &terrain->lod);
        ImGui::Checkbox("Camera frustum culling", &terrain->cull);
        ImGui::BeginDisabled(mScene.IsWalking());
        constexpr std::array names{"Slope", "Bridge", "River", "Cape"};
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (i != 0) ImGui::SameLine();
            if (ImGui::Button(names[i])) mScene.camera = WorldScene::kTerrainRoute[i];
        }
        ImGui::EndDisabled();
        if (mScene.streamTerrain && mStreamStats) {
            ImGui::SeparatorText("Streaming");
            const auto& stats = *mStreamStats;
            ImGui::Text("Focus (%d, %d)", stats.queue.focus.x, stats.queue.focus.y);
            ImGui::Text("Resident %u / %u (peak %u)", stats.resident, stats.queue.wanted, stats.peakResident);
            ImGui::Text("Queue %u | Work %u | Ready %u", stats.queue.queued, stats.queue.working, stats.queue.ready);
            if (mScene.storage) {
                ImGui::Text("CPU cache %u / %zu | hits %u", stats.cpuResident, ChunkCache::kCapacity, stats.cacheHits);
                ImGui::Text("Disk Read %.3f ms | Decompress %.3f ms", stats.io.diskMs, stats.io.decompressMs);
            }
            ImGui::Text("Mesh Build %.3f ms | GPU Upload %.3f ms", stats.meshMs, stats.uploadMs);
            ImGui::Text("Resident buffers %.2f MiB", stats.residentBytes / 1048576.0);
            ImGui::Text("Upload %u/%u | Peak %.1f KiB", stats.uploaded, mScene.terrain->Grid() == TerrainGrid::Exploration() ? 6 : ChunkStreamer::kUploadsPerFrame,
                        stats.peakUploadedBytes / 1024.0);
            ImGui::Text("Evicted since start: %u", stats.evicted);
        }
        if (mTerrainStats) {
            const auto& stats = *mTerrainStats;
            ImGui::Text("Visible %u | Culled %u", stats.visible, stats.culled);
            ImGui::Text("LOD 0 / 1 / 2 / 3: %u / %u / %u / %u", stats.lods[0], stats.lods[1],
                        stats.lods[2], stats.lods[3]);
            ImGui::Text("Color triangles: %u", stats.colorTriangles);
            ImGui::Text("Shadow triangles: %u", stats.shadowTriangles);
        }
        return;
    }
    if (auto& day = mScene.dayCycle; day && mSelected == kDayCyclePanel && !mSculptEnabled) {
        const int minute = static_cast<int>(WrapDayHour(day->hour) * 60.0f);
        ImGui::Text("Time %02d:%02d", minute / 60, minute % 60);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderFloat("##time-hours", &day->hour, 0.0f, 24.0f, "%.2f h", ImGuiSliderFlags_AlwaysClamp)) {
            day->playing = false;
        }
        const auto jumpTo = [&](const char* name, float hour) {
            if (ImGui::Button(name)) {
                day->hour = hour;
                day->playing = false;
            }
        };
        jumpTo("Sunrise", 6.75f);
        ImGui::SameLine();
        jumpTo("Noon", 12.0f);
        ImGui::SameLine();
        jumpTo("Night", 21.0f);
        ImGui::Checkbox("Play", &day->playing);
        if (ImGui::Button("Valley view")) mScene.camera = WorldScene::kDayCamera;
        ImGui::SameLine();
        if (ImGui::Button("Porch view")) mScene.camera = WorldScene::kPorchCamera;
        const auto light = EvaluateDaylight(day->hour);
        ImGui::Text("Sun altitude %.1f deg", light.sun.elevation);
        ImGui::Text("Night blend %.0f%%", light.nightMix * 100.0f);
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
        const bool wide = mScene.terrain->Grid() == TerrainGrid::Exploration();
        ImGui::Checkbox("Distance fog", &air->fogEnabled);
        ImGui::BeginDisabled(!air->fogEnabled);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.50f);
        ImGui::SliderFloat("Fog start (m)", &air->fogStart, 0.0f, wide ? 350.0f : 60.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.50f);
        ImGui::SliderFloat("Density (1/m)", &air->fogDensity, 0.0f, 0.12f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::EndDisabled();
        ImGui::SeparatorText("Sun shadow");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.50f);
        ImGui::SliderFloat("Distance (m)", &air->shadowDistance, AtmosphereSettings::kMinShadowDistance,
                           wide ? 350.0f : AtmosphereSettings::kMaxShadowDistance, "%.1f", ImGuiSliderFlags_AlwaysClamp);
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
        const float limitX = mScene.terrain ? mScene.terrain->Grid().WalkLimit() : WalkSpawn::kLimitX;
        const float limitZ = mScene.terrain ? mScene.terrain->Grid().WalkLimit() : WalkSpawn::kLimitZ;
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
        if (mScene.dayCycle) ImGui::TextUnformatted("Sun direction and power follow Day / night.");
        ImGui::BeginDisabled(mScene.dayCycle.has_value());
        ImGui::SliderFloat("Azimuth", &sun.azimuth, -180.0f, 180.0f, "%.0f deg", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Elevation", &sun.elevation, 0.0f, 90.0f, "%.0f deg", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Intensity", &sun.intensity, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::EndDisabled();
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

    if (mScene.storage) {
        ImGui::TextWrapped("Open the authoring JSON to edit and repack this object.");
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

}  // namespace tinyworld
