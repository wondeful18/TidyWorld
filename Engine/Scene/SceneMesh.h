#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 第二章的紧密顶点布局，供离线打包与 WebGPU 顶点缓冲共用。
//————————————————————————————————————————————————————————————————————————————————
struct SceneVertex {
    std::array<float, 3> position;  // 模型空间的位置，单位为米
    std::array<float, 3> normal;    // 模型空间的单位法线
    std::array<float, 3> color;     // 线性的材质底色 RGB
};

static_assert(sizeof(SceneVertex) == 9 * sizeof(float));
inline constexpr std::array kMeshMagic{'T', 'W', 'M', '2'};
inline constexpr std::uint32_t kMaxMeshVertices = 1'000'000;

//————————————————————————————————————————————————————————————————————————————————
// 读入构建时生成的三角形列表，并校验头、长度与顶点数量。
//————————————————————————————————————————————————————————————————————————————————
[[nodiscard]] std::vector<SceneVertex> LoadSceneMesh(const std::filesystem::path& path);

}  // namespace tinyworld
