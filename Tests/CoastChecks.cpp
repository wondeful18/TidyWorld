#include "Engine/Scene/SceneFile.h"
#include "Engine/Scene/WorldScene.h"

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

using namespace tinyworld;

//————————————————————————————————————————————————————————————————————————————————
// 验收在 Debug 和 Release 中都应报告同一条错误。
//————————————————————————————————————————————————————————————————————————————————
void Require(bool passed, const char* message) {
    if (!passed) throw std::runtime_error(message);
}

//————————————————————————————————————————————————————————————————————————————————
// 按样条中心线给步行控制器转向和前进指令，从村口走过桥到达观景点。
//————————————————————————————————————————————————————————————————————————————————
void CheckCoastalWalk(const std::filesystem::path& path) {
    WorldScene scene;
    scene.terrain.emplace();
    scene.canSculpt = true;
    scene.canPlaceProps = true;
    scene.canUseGltfProps = true;
    scene.road.emplace();
    scene.vegetation.emplace().mode = VegetationMode::TerrainRules;
    LoadScene(path, scene);

    const auto line = scene.road->Centerline();
    const auto spawn = scene.spawn.value().groundXZ;
    const auto closest = std::min_element(line.begin(), line.end(), [&](glm::vec3 a, glm::vec3 b) {
        return glm::distance(spawn, glm::vec2(a.x, a.z)) < glm::distance(spawn, glm::vec2(b.x, b.z));
    });
    Require(closest != line.end() && glm::distance(spawn, glm::vec2(closest->x, closest->z)) < 0.5f,
            "The spawn must be near the coastal route.");

    scene.SetWalking(true);
    bool touchedDeck = false;
    for (auto point = closest; point != line.end(); ++point) {
        const glm::vec2 goal{point->x, point->z};
        for (int step = 0;; ++step) {
            const auto pose = scene.WalkerPose();
            const glm::vec2 delta = goal - glm::vec2(pose.position.x, pose.position.z);
            if (glm::length(delta) < 0.13f) break;
            Require(step < 240, "The walker could not follow the coastal route.");
            const float heading = glm::degrees(std::atan2(delta.x, delta.y));
            const float turn = std::remainder(heading - pose.rotationDegrees.y, 360.0f);
            scene.UpdateWalk({std::abs(turn) < 12.0f ? 1.0f : 0.0f, std::clamp(turn, -1.0f, 1.0f)}, 1.0 / 120.0);

            const auto feet = scene.WalkerPose().position;
            Require(feet.y > WaterSettings::kLevel, "The route took the walker into the water.");
            Require(std::abs(feet.y - pose.position.y) < 0.14f, "The coastal path has a discontinuity.");
            if (std::abs(feet.x) < 0.5f && std::abs(feet.z) < 0.5f) {
                Require(std::abs(feet.y - 0.19f) < 0.001f, "The walker went below the footbridge.");
                touchedDeck = true;
            }
        }
    }
    const auto end = scene.WalkerPose().position;
    Require(touchedDeck && end.x > 20.8f && std::abs(end.z + 10.4f) < 0.25f,
            "The walker did not reach the coastal overlook via the bridge.");

    // 湖盆至海边的中心线应连续在同一个水位下。
    for (float x = 13.5f; x <= 24.0f; x += 0.25f) {
        const float z = -2.6f + 0.31f * (x - 14.0f) + 0.48f * std::sin((x - 13.0f) * 0.45f);
        Require(scene.terrain->HeightAt({x, z}) < WaterSettings::kLevel, "A dry strip blocks the estuary.");
    }
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 使用发布目录中的场景验收河口和观景路线。
//————————————————————————————————————————————————————————————————————————————————
int main(int argc, char** argv) {
    if (argc != 2) return 1;
    try {
        CheckCoastalWalk(std::filesystem::path(argv[1]) / "scenes/ch15/coast.json");
        std::cout << "The estuary is connected, and the walk reaches the overlook via the bridge.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
