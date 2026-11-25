#include "Engine/Terrain/Vegetation.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace tinyworld {
namespace {

//————————————————————————————————————————————————————————————————————————————————
// 无状态哈希只依赖格点、物种与种子，换一个块遍历顺序也会得到相同的实例。
//————————————————————————————————————————————————————————————————————————————————
float Random01(glm::ivec2 grid, std::uint32_t seed, std::uint32_t stream) {
    std::uint32_t bits = seed ^ (static_cast<std::uint32_t>(grid.x) * 0x9e3779b1u)
                             ^ (static_cast<std::uint32_t>(grid.y) * 0x85ebca77u) ^ (stream * 0xc2b2ae3du);
    bits ^= bits >> 16;
    bits *= 0x7feb352du;
    bits ^= bits >> 15;
    bits *= 0x846ca68bu;
    bits ^= bits >> 16;
    return static_cast<float>(bits >> 8) * (1.0f / 16777216.0f);
}

//————————————————————————————————————————————————————————————————————————————————
// 相邻噪声格共享角上的随机数。平滑插值让疏密变化越过地形块边界。
//————————————————————————————————————————————————————————————————————————————————
float DensityField(glm::vec2 point, std::uint32_t seed) {
    const auto grid = glm::ivec2(glm::floor(point));
    const auto t = glm::smoothstep(glm::vec2(0.0f), glm::vec2(1.0f), glm::fract(point));
    const float top = std::lerp(Random01(grid, seed, 1u), Random01(grid + glm::ivec2{1, 0}, seed, 1u), t.x);
    const float bottom = std::lerp(Random01(grid + glm::ivec2{0, 1}, seed, 1u),
                                   Random01(grid + glm::ivec2{1, 1}, seed, 1u), t.x);
    return std::lerp(top, bottom, t.y);
}

//————————————————————————————————————————————————————————————————————————————————
// 固定样地在村口留出空地，草甸和几组树沿山坡围出林缘。
//————————————————————————————————————————————————————————————————————————————————
float MeadowDensity(VegetationKind kind, glm::vec2 xz) {
    if (kind == VegetationKind::Grass) {
        const auto offset = (xz - glm::vec2{0.5f, 5.5f}) / glm::vec2{17.0f, 18.0f};
        return 0.94f * (1.0f - glm::smoothstep(0.60f, 1.0f, glm::length(offset)));
    }
    if (kind == VegetationKind::Rock) return 0.0f;
    const auto left = (xz - glm::vec2{-9.5f, -2.0f}) / glm::vec2{4.8f, 10.5f};
    const auto right = (xz - glm::vec2{10.5f, -5.0f}) / glm::vec2{4.8f, 8.0f};
    return 0.83f * (1.0f - glm::smoothstep(0.62f, 1.0f, std::min(glm::length(left), glm::length(right))));
}

//————————————————————————————————————————————————————————————————————————————————
// 每个物种对海拔、坡度和局部密度作出不同响应。
//————————————————————————————————————————————————————————————————————————————————
float TerrainDensity(VegetationKind kind, glm::vec2 xz, float height, float slope,
                     const VegetationSettings& settings) {
    const float patch = DensityField(xz / 6.0f, settings.seed);
    switch (kind) {
    case VegetationKind::Grass:
        return settings.density * std::lerp(0.15f, 1.0f, patch)
            * (1.0f - glm::smoothstep(0.40f, 0.9f, slope))
            * (1.0f - glm::smoothstep(3.0f, 5.0f, height));
    case VegetationKind::Tree:
        return settings.density * glm::smoothstep(0.25f, 0.78f, patch)
            * glm::smoothstep(-1.1f, 0.6f, height)
            * (1.0f - glm::smoothstep(3.8f, 6.0f, height))
            * (1.0f - glm::smoothstep(0.43f, 0.95f, slope));
    case VegetationKind::Rock:
        return settings.density * 0.68f * std::lerp(0.35f, 1.0f, 1.0f - patch)
            * glm::smoothstep(0.15f, 0.77f, slope + std::max(0.0f, height - 1.35f) * 0.20f);
    case VegetationKind::Count:
        return 0.0f;
    }
    return 0.0f;
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 先在分层小格内抖动候选点，再用地形规则与无状态随机数接纳候选。
//————————————————————————————————————————————————————————————————————————————————
VegetationPatch GenerateVegetationChunk(const HeightField& field, const RoadMask& road, glm::ivec2 chunk,
                                         const VegetationSettings& settings) {
    constexpr std::array cellsPerAxis{16, 4, 4};
    constexpr std::array minimumScale{0.53f, 0.70f, 0.37f};
    constexpr std::array maximumScale{1.18f, 1.38f, 0.86f};
    constexpr std::array footprint{0.35f, 1.09f, 0.90f};
    const bool terrainRules = settings.mode == VegetationMode::TerrainRules;
    VegetationPatch instances;
    for (std::size_t kind = 0; kind < instances.size(); ++kind) {
        if (!terrainRules && static_cast<VegetationKind>(kind) == VegetationKind::Rock) continue;
        const int cells = cellsPerAxis[kind];
        const float spacing = field.Grid().ChunkSize() / cells;
        const auto stream = static_cast<std::uint32_t>(8 * kind + 2);
        const auto species = static_cast<VegetationKind>(kind);
        for (int z = 0; z < cells; ++z) {
            for (int x = 0; x < cells; ++x) {
                const auto grid = chunk * cells + glm::ivec2{x, z};
                const auto random = [&](std::uint32_t offset) { return Random01(grid, settings.seed, stream + offset); };
                const auto xz = (glm::vec2(grid) + 0.19f + 0.62f * glm::vec2{random(0), random(1)}) * spacing;
                const float y = field.HeightAt(xz);
                if (species != VegetationKind::Rock && settings.minPlantHeight &&
                    y < *settings.minPlantHeight) continue;
                float chance;
                if (terrainRules) {
                    const float d = field.Grid().Spacing();
                    const glm::vec2 east{d, 0.0f}, north{0.0f, d};
                    const auto gradient = glm::vec2{field.HeightAt(xz + east) - field.HeightAt(xz - east),
                                                   field.HeightAt(xz + north) - field.HeightAt(xz - north)} / (2.0f * d);
                    chance = TerrainDensity(species, xz, y, glm::length(gradient), settings);
                } else {
                    chance = MeadowDensity(species, xz);
                }
                const float scale = std::lerp(minimumScale[kind], maximumScale[kind], random(3));
                if (random(2) >= chance || road.Blocks(xz, scale * footprint[kind])) continue;
                const float angle = glm::two_pi<float>() * random(4);
                instances[kind].push_back({{xz.x, y, xz.y, scale},
                                           {std::cos(angle), std::sin(angle), std::lerp(0.76f, 1.21f, random(5)), 0.0f}});
            }
        }
    }
    return instances;
}

}  // namespace tinyworld
