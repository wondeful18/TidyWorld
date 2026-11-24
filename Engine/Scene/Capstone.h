#pragma once

#include "Engine/Scene/WorldScene.h"

#include <array>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 课件机位与编辑器入口共用这些观察点，白天和夜晚用相同的投影。
//————————————————————————————————————————————————————————————————————————————————
struct TourStop {
    const char* id;
    const char* label;
    OrbitCamera camera;
};

inline constexpr std::array kTourStops{
    TourStop{"forest", "Forest", {{-7.5f, 2.5f, 28.0f}, -24.0f, 21.0f, 13.0f}},
    TourStop{"village", "Village", WorldScene::kDayCamera},
    TourStop{"bridge", "Footbridge", {{0.0f, 0.45f, 0.0f}, 117.0f, 27.0f, 12.5f}},
    TourStop{"estuary", "Estuary", {{17.0f, 0.5f, -4.0f}, 82.0f, 24.0f, 15.0f}},
    TourStop{"cape", "Cape", {{29.0f, 1.45f, -32.0f}, -42.0f, 32.0f, 14.0f}},
};

struct TourWalkResult {
    glm::vec3 end;
    float distance{};
    float minimumWaterClearance{};
    unsigned chunksVisited{};
    unsigned steps{};
    bool crossedBridge{};
};

//————————————————————————————————————————————————————————————————————————————————
// 用正常步行控制器沿已保存的主路前进，验证跨桥、跨块及抵达岬角。
// 完成时场景留在角色位置，便于调用方检查最终姿态或回读。
//————————————————————————————————————————————————————————————————————————————————
[[nodiscard]] TourWalkResult WalkCapstone(WorldScene& scene);

}  // namespace tinyworld
