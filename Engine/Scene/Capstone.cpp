#include "Engine/Scene/Capstone.h"

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace tinyworld {
namespace {

glm::vec2 Xz(glm::vec3 point) {
    return {point.x, point.z};
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 按道路采样点发出和键盘共用的转向、前进指令。绝不改写角色位置。
//————————————————————————————————————————————————————————————————————————————————
TourWalkResult WalkCapstone(WorldScene& scene) {
    if (!scene.terrain || scene.terrain->Grid() != TerrainGrid(true) || !scene.road || !scene.spawn) {
        throw std::runtime_error("The tour requires an expanded scene with a road and a spawn.");
    }
    const auto route = scene.road->Centerline();
    if (route.empty() || glm::distance(Xz(route.front()), scene.spawn->groundXZ) > 0.4f) {
        throw std::runtime_error("The tour must start at the beginning of the route.");
    }
    const auto grid = scene.terrain->Grid();
    scene.SetWalking(false);
    scene.SetWalking(true);
    TourWalkResult result{scene.WalkerPose().position, 0.0f, std::numeric_limits<float>::infinity()};
    std::array<bool, TerrainGrid::kMaxChunkCount> reached{};
    reached[grid.Index(grid.KeyAt(Xz(result.end)))] = true;
    for (const auto point : route) {
        for (int step = 0;; ++step) {
            const auto before = scene.WalkerPose();
            const auto delta = Xz(point) - Xz(before.position);
            if (glm::length(delta) < 0.125f) break;
            if (step >= 720) throw std::runtime_error("The walker did not reach a route sample.");
            const float heading = glm::degrees(std::atan2(delta.x, delta.y));
            const float turn = std::remainder(heading - before.rotationDegrees.y, 360.0f);
            scene.UpdateWalk({std::abs(turn) < 12.0f ? 1.0f : 0.0f, std::clamp(turn, -1.0f, 1.0f)}, 1.0 / 120.0);

            result.end = scene.WalkerPose().position;
            result.distance += glm::distance(Xz(before.position), Xz(result.end));
            ++result.steps;
            reached[grid.Index(grid.KeyAt(Xz(result.end)))] = true;
            const float clearance = result.end.y - WaterSettings::kLevel;
            result.minimumWaterClearance = std::min(result.minimumWaterClearance, clearance);
            if (clearance <= 0.0f || std::abs(result.end.y - before.position.y) > 0.14f) {
                throw std::runtime_error("The tour entered water or met a step discontinuity.");
            }
            if (std::abs(result.end.x) < 0.5f && std::abs(result.end.z) < 0.5f) {
                if (std::abs(result.end.y - 0.19f) > 0.001f) {
                    throw std::runtime_error("The walker did not use the bridge deck.");
                }
                result.crossedBridge = true;
            }
        }
    }
    result.chunksVisited = static_cast<unsigned>(std::count(reached.begin(), reached.end(), true));
    if (!result.crossedBridge || result.chunksVisited < 12 || result.distance < 70.0f ||
        result.end.x <= HeightField::kHalfExtent || result.end.z >= -HeightField::kHalfExtent ||
        glm::distance(Xz(result.end), Xz(route.back())) >= 0.125f) {
        throw std::runtime_error("The route did not cover the full forest-to-cape tour.");
    }
    return result;
}

}  // namespace tinyworld
