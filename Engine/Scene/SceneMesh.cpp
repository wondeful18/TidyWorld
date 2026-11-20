#include "Engine/Scene/SceneMesh.h"

#include <bit>
#include <fstream>
#include <stdexcept>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 解码小端序的 TWM2 顶点包。校验长度后再分配顶点数组。
//————————————————————————————————————————————————————————————————————————————————
std::vector<SceneVertex> LoadSceneMesh(const std::filesystem::path& path) {
    static_assert(std::endian::native == std::endian::little);
    std::ifstream input(path, std::ios::binary);
    std::array<char, 4> magic{};
    std::uint32_t count = 0;
    if (!input.read(magic.data(), magic.size()) || magic != kMeshMagic ||
        !input.read(reinterpret_cast<char*>(&count), sizeof(count)) ||
        count == 0 || count > kMaxMeshVertices || count % 3 != 0 ||
        std::filesystem::file_size(path) != magic.size() + sizeof(count) + count * sizeof(SceneVertex)) {
        throw std::runtime_error("Invalid chapter 02 mesh: " + path.string());
    }
    std::vector<SceneVertex> vertices(count);
    if (!input.read(reinterpret_cast<char*>(vertices.data()), count * sizeof(SceneVertex))) {
        throw std::runtime_error("Cannot read chapter 02 vertices: " + path.string());
    }
    return vertices;
}

}  // namespace tinyworld
