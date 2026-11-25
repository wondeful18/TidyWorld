#include "Engine/Terrain/WorldChunkPacking.h"

#include "Engine/Scene/WaterSettings.h"

#include <glm/common.hpp>

#include <algorithm>
#include <cmath>

namespace tinyworld {
namespace {

//————————————————————————————————————————————————————————————————————————————————
// 以原游线为中心，向外平滑接上程序地形。东侧保留河口与海面。
//————————————————————————————————————————————————————————————————————————————————
HeightField MakeExploration(const HeightField& source) {
    const auto grid = TerrainGrid::Exploration();
    HeightField result(grid);
    HeightField::Samples samples(grid.SampleSide() * grid.SampleSide());
    const float inner = source.Grid().HalfExtent();
    for (int iz = grid.FirstSample(); iz <= grid.LastSample(); ++iz) {
        for (int ix = grid.FirstSample(); ix <= grid.LastSample(); ++ix) {
            const auto p = glm::vec2{ix, iz} * grid.Spacing();
            const float hills = 4.5f + 4.0f * std::sin(p.x * 0.026f + p.y * 0.018f) +
                                2.6f * std::cos(p.y * 0.042f - p.x * 0.013f);
            const float ridge = glm::smoothstep(65.0f, 230.0f, -p.x) * 3.0f;
            const float coast = glm::smoothstep(26.0f, 120.0f, p.x + 13.0f * std::sin(p.y * 0.026f));
            const float outer = glm::mix(hills + ridge, WaterSettings::kLevel - 3.0f, coast);
            const float t = glm::smoothstep(inner - 5.0f, inner + 37.0f, std::max(std::abs(p.x), std::abs(p.y)));
            samples[grid.SampleIndex(ix, iz)] = std::clamp(glm::mix(source.HeightAt(p), outer, t),
                                                           -HeightField::kHeightLimit, HeightField::kHeightLimit);
        }
    }
    result.ReplaceSamples(samples);
    return result;
}

//————————————————————————————————————————————————————————————————————————————————
// 将作者的全局权重重采样成带一像素外圈的切片。相邻切片以相同世界位置采样。
//————————————————————————————————————————————————————————————————————————————————
MaterialSplat::Pixel SamplePaint(const WorldScene& scene, glm::vec2 xz) {
    if (!scene.paint) return {};
    const auto extent = scene.terrain->Grid().HalfExtent();
    if (std::abs(xz.x) > extent || std::abs(xz.y) > extent) return {};
    constexpr int side = MaterialSplat::kSide;
    const auto p = glm::clamp((xz / (2.0f * extent) + 0.5f) * float(side) - 0.5f,
                              glm::vec2{0.0f}, glm::vec2{side - 1.0f});
    const auto low = glm::ivec2(p);
    const auto high = glm::min(low + 1, glm::ivec2{side - 1});
    const auto fraction = p - glm::vec2(low);
    const auto& paint = scene.paint->Values();
    MaterialSplat::Pixel pixel{};
    for (std::size_t channel = 0; channel < pixel.size(); ++channel) {
        const auto a = std::lerp(float(paint[low.y * side + low.x][channel]),
                                 float(paint[low.y * side + high.x][channel]), fraction.x);
        const auto b = std::lerp(float(paint[high.y * side + low.x][channel]),
                                 float(paint[high.y * side + high.x][channel]), fraction.x);
        pixel[channel] = static_cast<std::uint8_t>(std::lerp(a, b, fraction.y));
    }
    return pixel;
}

//————————————————————————————————————————————————————————————————————————————————
// 从西部山脊接入原有林地。保留原路线和桥头控制点。
//————————————————————————————————————————————————————————————————————————————————
std::optional<RoadSpline> ExtendRoad(const WorldScene& scene, bool exploration) {
    auto road = scene.road;
    if (!exploration || !road || scene.terrain->Grid() != TerrainGrid(true) ||
        road->points.size() > RoadSpline::kMaxPoints - 6) return road;
    constexpr std::array ridge{
        glm::vec3{-248.0f, 0.0f, 210.0f}, glm::vec3{-198.0f, 0.0f, 170.0f},
        glm::vec3{-133.0f, 0.0f, 140.0f}, glm::vec3{-89.0f, 0.0f, 92.0f},
        glm::vec3{-46.0f, 0.0f, 61.0f}, glm::vec3{-23.0f, 0.0f, 45.0f},
    };
    road->points.insert(road->points.begin(), ridge.begin(), ridge.end());
    road->junctionPoint += ridge.size();
    return road;
}

}  // namespace

WorldChunkPacking::WorldChunkPacking(const WorldScene& scene, bool exploration)
    : mScene(scene), mHeights(exploration ? MakeExploration(*scene.terrain) : *scene.terrain),
      mRoad(ExtendRoad(scene, exploration)), mRoads(Grid().ChunkCount()), mProps(Grid().ChunkCount()) {
    const auto grid = Grid();
    if (mRoad) {
        const auto road = MakeRoadMesh(mHeights, *mRoad);
        for (std::size_t i = 0; i < road.size(); i += 3) {
            glm::vec2 center{0.0f};
            for (std::size_t j = i; j < i + 3; ++j) center += glm::vec2{road[j].position[0], road[j].position[2]};
            auto& tile = mRoads[grid.Index(grid.KeyAt(center / 3.0f))];
            tile.insert(tile.end(), road.begin() + i, road.begin() + i + 3);
        }
    }
    for (auto prop : scene.props) {
        const auto position = prop.transform.position;
        const glm::vec2 xz{position.x, position.z};
        if (exploration) prop.transform.position.y += mHeights.HeightAt(xz) - scene.terrain->HeightAt(xz);
        mProps[grid.Index(grid.KeyAt(xz))].push_back(std::move(prop));
    }
}

ChunkTile WorldChunkPacking::MakeTile(glm::ivec2 key) const {
    const auto grid = Grid();
    const auto origin = key * grid.Cells();
    const int side = grid.Cells() + 3;
    HeightField::Samples halo;
    halo.reserve(side * side);
    for (int z = -1; z <= grid.Cells() + 1; ++z) {
        for (int x = -1; x <= grid.Cells() + 1; ++x) halo.push_back(mHeights.Sample(origin.x + x, origin.y + z));
    }
    const auto index = grid.Index(key);
    ChunkTile tile{HeightField(grid, key, std::move(halo)), {}, mProps[index], mRoads[index]};
    const float pixelSize = grid.ChunkSize() / kSplatTileCells;
    tile.splat.reserve(kSplatTileSide * kSplatTileSide);
    for (int z = -1; z <= kSplatTileCells; ++z) {
        for (int x = -1; x <= kSplatTileCells; ++x) {
            const auto pixel = key * kSplatTileCells + glm::ivec2{x, z};
            tile.splat.push_back(SamplePaint(mScene, (glm::vec2(pixel) + 0.5f) * pixelSize));
        }
    }
    return tile;
}

}  // namespace tinyworld
