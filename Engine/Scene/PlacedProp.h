#pragma once

#include "Engine/Scene/SceneMesh.h"
#include "Engine/Scene/Transform.h"

#include <array>
#include <cstddef>

namespace tinyworld {

struct PropAsset {
    const char* id;
    const char* name;
};

inline constexpr std::array kPropAssets{
    PropAsset{"rock", "Rock"}, PropAsset{"tree", "Tree"},
    PropAsset{"cottage", "Cottage"}, PropAsset{"cabin", "Cabin"},
};

struct PlacedProp {
    static constexpr std::size_t kLimit = 128;
    std::size_t asset{};  // 原型索引，存档使用原型的字符串 ID
    Transform transform;
};

[[nodiscard]] std::vector<SceneVertex> MakeRockMesh();
[[nodiscard]] std::vector<SceneVertex> MakeCabinMesh();

}  // namespace tinyworld
