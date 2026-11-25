#include "Engine/Renderer/RenderCoordinates.h"
#include "Engine/Scene/SceneFile.h"
#include "Engine/Scene/WorldScene.h"
#include "Engine/Terrain/ChunkStreamer.h"
#include "Engine/Terrain/TerrainMesh.h"

#include <glm/geometric.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace tinyworld;
using namespace std::chrono_literals;

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

WorldScene SceneForPackages() {
    WorldScene scene;
    scene.terrain.emplace();
    scene.canSculpt = scene.streamTerrain = scene.capstone = true;
    scene.canPlaceProps = scene.canUseGltfProps = true;
    scene.materials.emplace();
    scene.paint.emplace();
    scene.road.emplace();
    scene.vegetation.emplace().mode = VegetationMode::TerrainRules;
    scene.dayCycle.emplace();
    scene.lightmapEnabled = true;
    return scene;
}

//————————————————————————————————————————————————————————————————————————————————
// 检查两个文件的边缘法线，验证四档索引都保留每个边界顶点。
//————————————————————————————————————————————————————————————————————————————————
void CheckEdges(const ChunkTile& a, const ChunkTile& b, glm::ivec2 key, TerrainGrid grid) {
    const auto left = MakeTerrainVertices(a.heights, key, false);
    const auto right = MakeTerrainVertices(b.heights, key + glm::ivec2{1, 0}, false);
    const auto cells = grid.Cells();
    const auto stride = cells + 1;
    for (int z = 0; z <= cells; ++z) {
        Require(left[z * stride + cells].position == right[z * stride].position, "Chunk positions break at seam.");
        Require(left[z * stride + cells].normal == right[z * stride].normal, "Chunk halo does not match edge normals.");
    }
    std::size_t previous = 0;
    for (int step : {1, 2, 4, 8}) {
        const auto indices = MakeTerrainIndices(cells, step);
        Require(indices.size() % 3 == 0 && (previous == 0 || indices.size() < previous), "LOD counts did not decrease.");
        previous = indices.size();
        std::vector<bool> used(left.size());
        for (const auto index : indices) {
            Require(index < used.size(), "An index exceeds the shared vertex buffer.");
            used[index] = true;
        }
        for (int z = 0; z <= cells; ++z) {
            Require(used[z * stride] && used[z * stride + cells] && used[z] && used[cells * stride + z],
                    "LOD did not reference every edge sample.");
        }
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 冷启动只读一个块。角色等待下一块时保持脚底，块到达后同一指令可以跨过去。
//————————————————————————————————————————————————————————————————————————————————
void CheckWalking(WorldScene& scene) {
    auto& store = *scene.storage;
    const auto grid = scene.terrain->Grid();
    const glm::ivec2 left{-1, 1}, right{0, 1};
    ChunkStreamer stream;
    scene.spawn = WalkSpawn{{-0.005f, grid.ChunkSize() * 1.5f}, 90.0f};
    scene.SetWalking(true);
    scene.UpdateWalk({1.0f, 0.0f}, 1.0 / 120.0);
    Require(scene.WalkerPose().position.x == scene.spawn->groundXZ.x, "Unloaded spawn moved.");
    const auto request = store.cache->TakePriority();
    Require(request && *request == left && scene.terrain->Values().empty(), "Spawn did not request its disk tile.");
    stream.Update(left, *scene.terrain, scene.road, *scene.vegetation, store.directory, store.cache);
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    std::optional<ChunkPayload> payload;
    do {
        stream.WaitForResult(10ms);
        payload = stream.TakeReady();
    } while (!payload && std::chrono::steady_clock::now() < deadline);
    Require(payload && payload->tile && !payload->cacheHit && payload->io.diskMs > 0.0, "Worker did not read disk tile.");
    Require(grid.Key(payload->index) == left, "Disk worker loaded the wrong focus.");
    // 使用独立缓存控制两个块何时到达，避免线程调度改变本测试的边界条件。
    auto controlled = std::make_shared<ChunkCache>(grid);
    controlled->Store(left, payload->tile);
    scene.storage->cache = controlled;
    scene.terrain.emplace(grid, controlled);
    scene.UpdateWalk({1.0f, 0.0f}, 1.0 / 120.0);
    Require(scene.WalkerPose().position.x < 0.0f && controlled->TakePriority() == right, "Walker crossed a missing tile.");
    ChunkReadTimes times;
    controlled->Store(right, std::make_shared<const ChunkTile>(ReadChunk(ChunkPath(store.directory, right), grid, right, times)));
    scene.UpdateWalk({1.0f, 0.0f}, 1.0 / 120.0);
    Require(scene.WalkerPose().position.x > 0.0f && std::isfinite(scene.WalkerPose().position.y), "Walker did not resume.");
}

//————————————————————————————————————————————————————————————————————————————————
// 对同一逻辑位置做远点平移，相对渲染的微位移仍可稳定进入 float 模型矩阵。
//————————————————————————————————————————————————————————————————————————————————
void CheckPrecision() {
    OrbitCamera camera{{0.0f, 0.0f, 0.0f}, 0.0f, 10.0f, 8.0f};
    const auto projection = camera.Projection(16.0f / 9.0f, true);
    glm::mat4 object{1.0f};
    object[3].x = 0.002f;
    const RenderCoordinates absolute(camera, projection, PrecisionPreview{true, false});
    const RenderCoordinates relative(camera, projection, PrecisionPreview{true, true});
    Require(absolute.Model(object)[3].x == 100000.0f, "Absolute float unexpectedly retained a 2 mm translation.");
    Require(std::abs(relative.Model(object)[3].x - 0.002f) < 1.0e-7f, "Relative translation lost the 2 mm movement.");
}

void CheckPackages(const std::filesystem::path& root, const std::filesystem::path& scratch) {
    auto authoring = SceneForPackages();
    LoadScene(root / "scenes/ch17/porch.json", authoring);
    SaveWorldPackage(scratch, authoring, false, false);
    auto before = SceneForPackages();
    LoadScene(scratch, before);
    const glm::ivec2 editedKey{0, 0}, untouchedKey{-3, -3};
    const auto path = before.storage->cache->File(scratch, untouchedKey);
    const auto time = std::filesystem::last_write_time(path);
    const auto manifestTime = std::filesystem::last_write_time(scratch / "manifest.json");
    SaveWorldPackage(scratch, authoring, false, false);
    Require(std::filesystem::last_write_time(scratch / "manifest.json") == manifestTime, "No-op save replaced the manifest.");
    Require(authoring.terrain->ApplyBrush({0.15f, 0.15f}, TerrainBrush{}, 0.2f), "Test edit did not change terrain.");
    SaveWorldPackage(scratch, authoring, false, false);
    auto after = SceneForPackages();
    LoadScene(scratch, after);
    const auto oldEdited = before.storage->cache->File(scratch, editedKey);
    const auto newEdited = after.storage->cache->File(scratch, editedKey);
    Require(oldEdited != newEdited && after.storage->cache->File(scratch, untouchedKey) == path &&
            std::filesystem::last_write_time(path) == time, "An unchanged tile was replaced or the dirty tile was lost.");
    ChunkReadTimes times;
    const auto grid = after.terrain->Grid();
    const auto oldTile = ReadChunk(oldEdited, grid, editedKey, times);
    const auto newTile = ReadChunk(newEdited, grid, editedKey, times);
    const glm::ivec2 west{-1, 0};
    const auto updatedWest = ReadChunk(after.storage->cache->File(scratch, west), grid, west, times);
    Require(oldTile.heights.Sample(0, 0) < newTile.heights.Sample(0, 0), "Dirty height did not persist in the new snapshot.");
    CheckEdges(updatedWest, newTile, west, grid);

    auto large = SceneForPackages();
    LoadScene(root / "worlds/exploration", large);
    const auto wide = large.terrain->Grid();
    Require(wide.ChunkCount() == 1024 && wide.ChunkSize() == 32.0f && large.terrain->Values().empty(),
            "Exploration loaded with unexpected dimensions or a global height field.");
    const glm::ivec2 left{-1, 1}, right{0, 1};
    const auto a = ReadChunk(large.storage->cache->File(large.storage->directory, left), wide, left, times);
    const auto b = ReadChunk(large.storage->cache->File(large.storage->directory, right), wide, right, times);
    Require(times.decompressMs > 0.0, "Expected LZ4 block compression.");
    CheckEdges(a, b, left, wide);
    CheckWalking(large);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) return 1;
    try {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto scratch = std::filesystem::path(argv[2]) / ("world-package-checks-" + std::to_string(stamp));
        CheckPackages(argv[1], scratch);
        CheckPrecision();
        std::cout << "TWCK packing, unchanged files, halo normals, indexed LODs, asynchronous walking and precision passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
