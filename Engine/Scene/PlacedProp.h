#pragma once

#include "Engine/Scene/SceneMesh.h"
#include "Engine/Scene/Transform.h"

#include <array>
#include <cstddef>
#include <string>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 场景中共用的资产原型。磁盘只记录稳定的路径或内置 ID。
//————————————————————————————————————————————————————————————————————————————————
struct PropAsset {
    const char* id;     // 内置 ID 或相对运行目录的 glTF 路径
    const char* name;   // 编辑器中的原型名称
    const char* lightmap{};  // 与该原型 UV2 匹配的相对路径
};

inline constexpr std::size_t kBuiltinPropCount = 4;
inline constexpr std::size_t kBridgeAsset = 6;
inline constexpr std::size_t kPorchAsset = 7;
inline constexpr float kBridgeDeckHalfWidth = 1.05f;    // 桥面局部 X 半宽，单位为米
inline constexpr float kBridgeDeckHalfLength = 3.7f;    // 桥面局部 Z 半长，单位为米

inline constexpr std::array kPropAssets{
    PropAsset{"rock", "Rock"}, PropAsset{"tree", "Tree"},
    PropAsset{"cottage", "Cottage"}, PropAsset{"cabin", "Cabin"},
    PropAsset{"models/ch13/house.glb", "Village house"},
    PropAsset{"models/ch13/fence.glb", "Fence"},
    PropAsset{"models/ch13/bridge.glb", "Footbridge"},
    PropAsset{"models/ch17/porch.glb", "Baked porch", "lightmaps/ch17/porch.twlm"},
};

//————————————————————————————————————————————————————————————————————————————————
// 以变换复用一个场景资产，多个实例共享同一份顶点与材质。
//————————————————————————————————————————————————————————————————————————————————
struct PlacedProp {
    static constexpr std::size_t kLimit = 128;  // 场景手摆对象上限
    std::size_t asset{};  // 原型索引，存档使用原型的字符串 ID
    Transform transform;  // 当前实例所在的世界变换
    std::string lightmap; // 空引用表示使用实时光照
};

[[nodiscard]] std::vector<SceneVertex> MakeRockMesh();
[[nodiscard]] std::vector<SceneVertex> MakeCabinMesh();

}  // namespace tinyworld
