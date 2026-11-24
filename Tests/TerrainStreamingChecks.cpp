#include "Engine/Terrain/ChunkStreamer.h"
#include "Engine/Terrain/TerrainMesh.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace tinyworld;
using namespace std::chrono_literals;

void Require(bool valid, const char* message) {
    if (!valid) throw std::runtime_error(message);
}

//————————————————————————————————————————————————————————————————————————————————
// 逐步等最近的 CPU 结果，限制每次等待并报告工作线程超时。
//————————————————————————————————————————————————————————————————————————————————
ChunkPayload Get(ChunkStreamer& stream) {
    const auto deadline = std::chrono::steady_clock::now() + 4s;
    do {
        if (auto result = stream.TakeReady()) return std::move(*result);
        stream.WaitForResult(20ms);
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("Timed out waiting for chunk.");
}

//————————————————————————————————————————————————————————————————————————————————
// 持续取出新中心的数据，检查请求排序、逐出、掉头和以新版本重建。
//————————————————————————————————————————————————————————————————————————————————
void CheckStreaming() {
    HeightField field;
    const RoadSpline road;
    const VegetationSettings plants{VegetationMode::TerrainRules};
    ChunkStreamer stream;
    const glm::ivec2 start{-2, 1};
    const glm::ivec2 far{2, -2};
    stream.Update(start, field, road, plants);
    const auto first = Get(stream);
    Require(TerrainKey(first.index) == start && first.fine.size() == 1536 && first.coarse.size() == 648,
            "The nearest key must be delivered with both meshes first.");
    const auto firstHeights = first.fine;
    stream.Update(far, field, road, plants);
    Require(!stream.Wants(first.index), "A distant key was not evicted.");
    stream.Update(start, field, road, plants);
    const auto returned = Get(stream);
    Require(returned.index == first.index && returned.plants == first.plants &&
            returned.fine.size() == firstHeights.size(), "Return did not reproduce the old tile.");
    for (std::size_t i = 0; i < returned.fine.size(); ++i) {
        Require(returned.fine[i].position == firstHeights[i].position, "Return yielded a different height.");
    }

    stream.Update(far, field, road, plants);
    const auto center = (glm::vec2(far) + 0.5f) * HeightField::kChunkSize;
    Require(field.ApplyBrush(center, {BrushMode::Flatten, 2.0f, 5.0f, 11.0f}, 1.0f), "Brush did not edit.");
    stream.Update(far, field, road, plants);
    const auto edited = Get(stream);
    Require(TerrainKey(edited.index) == far && edited.bounds.high.y >= 10.9f,
            "A new request accepted obsolete CPU heights.");

    ChunkStreamer centerStream;
    centerStream.Update({0, 0}, field, road, plants);
    const auto count = centerStream.Statistics().wanted;
    Require(count == ChunkStreamer::kMaxResident, "The centre should request the full window.");
    std::array<bool, HeightField::kChunkCount> received{};
    for (unsigned i = 0; i < count; ++i) {
        const auto tile = Get(centerStream);
        Require(centerStream.Wants(tile.index) && !received[tile.index], "An obsolete or duplicate tile arrived.");
        if (i == 0) Require(TerrainKey(tile.index) == glm::ivec2(0), "Centre was not visited first.");
        received[tile.index] = true;
        for (const auto& v : tile.road) {
            Require(v.position[0] >= tile.bounds.low.x - 0.001f && v.position[0] <= tile.bounds.high.x + 0.001f &&
                    v.position[2] >= tile.bounds.low.z - 0.001f && v.position[2] <= tile.bounds.high.z + 0.001f,
                    "A streamed road vertex left its terrain chunk.");
        }
    }
    const auto stable = centerStream.Statistics();
    Require(!stable.queued && !stable.working && !stable.ready && !centerStream.TakeReady(),
            "Settled work should have no pending or duplicate results.");
}
}

int main() {
    try {
        CheckStreaming();
        std::cout << "Chunk priority, stale results, terrain revision and reload checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
