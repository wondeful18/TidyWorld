#include "Engine/Scene/Capstone.h"
#include "Engine/Scene/SceneFile.h"
#include "Engine/Terrain/ChunkStreamer.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
using namespace tinyworld;

void Require(bool passed, const char* message) {
    if (!passed) throw std::runtime_error(message);
}

//————————————————————————————————————————————————————————————————————————————————
// 与应用入口一致地开启旧存档和新地图所需的场景能力。
//————————————————————————————————————————————————————————————————————————————————
WorldScene MakeScene(bool capstone) {
    WorldScene scene;
    scene.terrain.emplace();
    scene.canSculpt = true;
    scene.road.emplace();
    scene.vegetation.emplace().mode = VegetationMode::TerrainRules;
    scene.canPlaceProps = scene.canUseGltfProps = true;
    scene.dayCycle.emplace();
    scene.lightmapEnabled = true;
    scene.capstone = capstone;
    return scene;
}

//————————————————————————————————————————————————————————————————————————————————
// 用同一个工作线程切换两种布局，检查旧数组槽无法覆盖新索引。
//————————————————————————————————————————————————————————————————————————————————
void CheckChunkHandoff(const WorldScene& small, const WorldScene& tour) {
    using namespace std::chrono_literals;
    ChunkStreamer stream;
    const auto wait = [&] {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        do {
            if (auto result = stream.TakeReady()) return std::move(*result);
            stream.WaitForResult(10ms);
        } while (std::chrono::steady_clock::now() < deadline);
        throw std::runtime_error("Timed out waiting for the capstone tile.");
    };
    const glm::ivec2 nearby{-1, 1};
    const glm::ivec2 cape{3, -4};
    stream.Update(nearby, *small.terrain, small.road, *small.vegetation);
    stream.Update(cape, *tour.terrain, tour.road, *tour.vegetation);
    const auto tile = wait();
    Require(tour.terrain->Grid().Key(tile.index) == cape && !tile.road.empty(),
            "The new map did not receive the cape road tile by its key.");
    stream.Update(nearby, *small.terrain, small.road, *small.vegetation);
    Require(small.terrain->Grid().Key(wait().index) == nearby &&
            !stream.Wants(small.terrain->Grid().ChunkCount()),
            "Switching back retained an obsolete array slot.");
}

//————————————————————————————————————————————————————————————————————————————————
// 对比旧地形所有原始样点，验收新版场景保存、重开、时间和整条步行路线。
//————————————————————————————————————————————————————————————————————————————————
void CheckTour(const std::filesystem::path& root, const std::filesystem::path& scratch) {
    const auto path = root / "scenes/ch21/tour.json";
    auto small = MakeScene(false);
    LoadScene(root / "scenes/ch17/porch.json", small);
    auto tour = MakeScene(true);
    LoadScene(path, tour);
    const auto grid = tour.terrain->Grid();
    const auto old = small.terrain->Grid();
    Require(grid.ChunkCount() == 100 && grid.SampleSide() == 161 && grid.HalfExtent() == 40.0f, "Bad tour layout.");
    for (int z = old.FirstSample(); z <= old.LastSample(); ++z) {
        for (int x = old.FirstSample(); x <= old.LastSample(); ++x) {
            Require(small.terrain->Sample(x, z) == tour.terrain->Sample(x, z), "An original valley sample changed.");
        }
    }
    Require(grid.KeyAt({40.0f, -40.0f}) == glm::ivec2(4, -5) &&
            grid.KeyAt({-0.05f, 0.0f}) == glm::ivec2(-1, 0), "Bad expanded edge key.");
    const auto mask = MakeRoadMask(*tour.road, grid);
    Require(mask.Side() == 320 && mask.Blocks({29.4f, -31.0f}, 0.1f), "The outer road was not covered.");
    CheckChunkHandoff(small, tour);
    bool refused = false;
    try { LoadScene(path, small); } catch (const std::runtime_error&) { refused = true; }
    Require(refused && small.terrain->Grid() == old, "Chapter 20 must keep its scene when it refuses version 9.");

    tour.dayCycle->hour = 21.0f;
    const auto initial = WalkCapstone(tour);
    tour.SetWalking(false);
    const auto reopenedPath = scratch / "reopened.json";
    SaveScene(reopenedPath, tour);
    LoadScene(reopenedPath, tour);
    Require(tour.dayCycle->hour == 21.0f && tour.terrain->Grid() == grid, "Reopen lost the time or grid.");
    const auto reopened = WalkCapstone(tour);
    Require(initial.end == reopened.end && initial.distance == reopened.distance &&
            initial.steps == reopened.steps && initial.chunksVisited == reopened.chunksVisited &&
            reopened.crossedBridge, "The full walk changed after reopening the saved scene.");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) return 1;
    try {
        const auto scratch = std::filesystem::path(argv[2]) / "capstone-checks";
        std::filesystem::create_directories(scratch);
        CheckTour(argv[1], scratch);
        std::cout << "Expanded grid, old valley, streaming handoff and full route replay passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
