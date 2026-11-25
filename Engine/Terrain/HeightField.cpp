#include "Engine/Terrain/HeightField.h"
#include "Engine/Terrain/ChunkCache.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace tinyworld {
namespace {

//————————————————————————————————————————————————————————————————————————————————
// 新建、修改与重开使用进程内不同的号，渲染器无需判断高度场来自哪里。
//————————————————————————————————————————————————————————————————————————————————
std::uint64_t NextRevision() {
    static std::atomic<std::uint64_t> revision{};
    return ++revision;
}

//————————————————————————————————————————————————————————————————————————————————
// 由离丘顶的距离生成一个平滑隆起。宽度与高度均以米计。
//————————————————————————————————————————————————————————————————————————————————
float Hill(float x, float z, float centerX, float centerZ, float width, float height) {
    const float dx = (x - centerX) / width;
    const float dz = (z - centerZ) / width;
    return height * std::exp(-(dx * dx + dz * dz));
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 样点和块分别按 Z 行存储，调用方传入已落在布局范围内的编号。
//————————————————————————————————————————————————————————————————————————————————
std::size_t TerrainGrid::SampleIndex(int x, int z) const {
    return (z - FirstSample()) * SampleSide() + x - FirstSample();
}

glm::ivec2 TerrainGrid::Key(std::size_t index) const {
    return {FirstKey() + static_cast<int>(index % Side()), FirstKey() + static_cast<int>(index / Side())};
}

std::size_t TerrainGrid::Index(glm::ivec2 key) const {
    return (key.y - FirstKey()) * Side() + key.x - FirstKey();
}

//————————————————————————————————————————————————————————————————————————————————
// 世界坐标先钳位，再向下取整。正端点归到最后一个块。
//————————————————————————————————————————————————————————————————————————————————
glm::ivec2 TerrainGrid::KeyAt(glm::vec2 xz) const {
    xz = glm::clamp(xz, glm::vec2{-HalfExtent()}, glm::vec2{HalfExtent()});
    return {std::min(static_cast<int>(std::floor(xz.x / ChunkSize())), EndKey() - 1),
            std::min(static_cast<int>(std::floor(xz.y / ChunkSize())), EndKey() - 1)};
}

//————————————————————————————————————————————————————————————————————————————————
// 生成小径中心线，让地表颜色顺着谷底缓慢转弯。
//————————————————————————————————————————————————————————————————————————————————
float TerrainTrailX(float z) {
    return 1.45f * std::sin(0.24f * z) + 0.55f * std::sin(0.37f * z + 0.6f);
}

//————————————————————————————————————————————————————————————————————————————————
// 在整数网格上只计算一次每个样点。相邻块由此共享同一条边的端点高度。
//————————————————————————————————————————————————————————————————————————————————
HeightField::HeightField(TerrainGrid grid)
    : mGrid(grid), mHeights(grid.SampleSide() * grid.SampleSide()), mChunkRevisions(grid.ChunkCount()) {
    for (int iz = mGrid.FirstSample(); iz <= mGrid.LastSample(); ++iz) {
        for (int ix = mGrid.FirstSample(); ix <= mGrid.LastSample(); ++ix) {
            const float x = ix * mGrid.Spacing();
            const float z = iz * mGrid.Spacing();
            const float rise = 1.12f * std::sin(-0.23f * z) + 0.18f * std::sin(0.33f * x + 0.18f * z);
            const float west = Hill(x, z, -10.5f, -5.5f, 6.7f, 3.1f);
            const float east = Hill(x, z, 10.0f, -0.5f, 6.2f, 3.5f);
            mHeights[mGrid.SampleIndex(ix, iz)] = rise + west + east;
        }
    }
    Invalidate(glm::ivec2{mGrid.FirstSample()}, glm::ivec2{mGrid.LastSample()});
}

//————————————————————————————————————————————————————————————————————————————————
// 样点表包含块的两端和每端向外一格，局部查询可以读到边缘法线所需的邻居。
//————————————————————————————————————————————————————————————————————————————————
HeightField::HeightField(TerrainGrid grid, glm::ivec2 key, Samples halo)
    : mTile(key), mGrid(grid), mHeights(std::move(halo)), mRevision(NextRevision()) {
    const auto side = grid.Cells() + 3;
    if (!grid.Contains(key) || mHeights.size() != static_cast<std::size_t>(side * side)) {
        throw std::runtime_error("Invalid chunk height halo.");
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 游览布局只有元数据，异步读盘成功的块由 CPU 缓存持有。
//————————————————————————————————————————————————————————————————————————————————
HeightField::HeightField(TerrainGrid grid, std::shared_ptr<ChunkCache> cache)
    : mCache(std::move(cache)), mGrid(grid), mRevision(NextRevision()) {}

//————————————————————————————————————————————————————————————————————————————————
// 游览时只用目标块内部的四个样点。未驻留时可将玩家脚下的块提升到队首。
//————————————————————————————————————————————————————————————————————————————————
std::optional<float> HeightField::TryHeight(glm::vec2 xz, bool request) const {
    if (!mCache) return HeightAt(xz);
    const auto key = mGrid.KeyAt(xz);
    if (const auto tile = mCache->Find(key)) return tile->heights.HeightAt(xz);
    if (request) mCache->Prioritize(key);
    return std::nullopt;
}

//————————————————————————————————————————————————————————————————————————————————
// 从世界样点编号换算数组里的 X、Z 索引。
//————————————————————————————————————————————————————————————————————————————————
float HeightField::Sample(int ix, int iz) const {
    if (mTile) {
        const auto local = glm::ivec2{ix, iz} - *mTile * mGrid.Cells() + 1;
        const int side = mGrid.Cells() + 3;
        const int x = std::clamp(local.x, 0, side - 1);
        const int z = std::clamp(local.y, 0, side - 1);
        return mHeights[z * side + x];
    }
    if (mCache) throw std::logic_error("Use TryHeight for cached terrain.");
    const int x = std::clamp(ix, mGrid.FirstSample(), mGrid.LastSample());
    const int z = std::clamp(iz, mGrid.FirstSample(), mGrid.LastSample());
    return mHeights[mGrid.SampleIndex(x, z)];
}

//————————————————————————————————————————————————————————————————————————————————
// 先定位单元格，再对斜边两侧的三角形分别做线性插值。
//————————————————————————————————————————————————————————————————————————————————
float HeightField::HeightAt(glm::vec2 xz) const {
    if (mCache) {
        const auto height = TryHeight(xz);
        if (!height) throw std::logic_error("Terrain chunk has not loaded.");
        return *height;
    }
    const float sx = std::clamp(xz.x / mGrid.Spacing(), static_cast<float>(mGrid.FirstSample()), static_cast<float>(mGrid.LastSample()));
    const float sz = std::clamp(xz.y / mGrid.Spacing(), static_cast<float>(mGrid.FirstSample()), static_cast<float>(mGrid.LastSample()));
    const int ix = std::min(static_cast<int>(std::floor(sx)), mGrid.LastSample() - 1);
    const int iz = std::min(static_cast<int>(std::floor(sz)), mGrid.LastSample() - 1);
    const float fx = sx - ix;
    const float fz = sz - iz;

    const float a = Sample(ix, iz);
    const float b = Sample(ix + 1, iz);
    const float c = Sample(ix, iz + 1);
    const float d = Sample(ix + 1, iz + 1);
    if (fx + fz <= 1.0f) {
        return a + fx * (b - a) + fz * (c - a);
    }
    return d + (1.0f - fx) * (c - d) + (1.0f - fz) * (b - d);
}

//————————————————————————————————————————————————————————————————————————————————
// 用 floor 分配负坐标，避免把原点左侧的窄条误归给块 0。
//————————————————————————————————————————————————————————————————————————————————
glm::ivec2 HeightField::ChunkAt(glm::vec2 xz) {
    return TerrainGrid{}.KeyAt(xz);
}

//————————————————————————————————————————————————————————————————————————————————
// 先取得圆的样点包围盒，再按距离给实际修改赋权。平滑始终读取修改前的高度。
//————————————————————————————————————————————————————————————————————————————————
bool HeightField::ApplyBrush(glm::vec2 center, const TerrainBrush& brush, float seconds) {
    if (mCache || mTile) return false;
    if (brush.radius <= 0.0f || seconds <= 0.0f || brush.strength <= 0.0f) {
        return false;
    }
    const auto low = glm::ivec2(glm::clamp(glm::floor((center - brush.radius) / mGrid.Spacing()),
                                           glm::vec2{static_cast<float>(mGrid.FirstSample())}, glm::vec2{static_cast<float>(mGrid.LastSample())}));
    const auto high = glm::ivec2(glm::clamp(glm::ceil((center + brush.radius) / mGrid.Spacing()),
                                            glm::vec2{static_cast<float>(mGrid.FirstSample())}, glm::vec2{static_cast<float>(mGrid.LastSample())}));
    const auto before = mHeights;
    glm::ivec2 changedLow{mGrid.LastSample()};
    glm::ivec2 changedHigh{mGrid.FirstSample()};
    const auto previous = [&](int x, int z) {
        return before[mGrid.SampleIndex(std::clamp(x, mGrid.FirstSample(), mGrid.LastSample()),
                                  std::clamp(z, mGrid.FirstSample(), mGrid.LastSample()))];
    };
    for (int z = low.y; z <= high.y; ++z) {
        for (int x = low.x; x <= high.x; ++x) {
            const auto delta = glm::vec2{x * mGrid.Spacing(), z * mGrid.Spacing()} - center;
            const float t = std::max(0.0f, 1.0f - glm::length(delta) / brush.radius);
            const float weight = t * t * (3.0f - 2.0f * t);
            if (weight == 0.0f) {
                continue;
            }
            auto& height = mHeights[mGrid.SampleIndex(x, z)];
            const float amount = weight * brush.strength * seconds;
            float next{};
            switch (brush.mode) {
            case BrushMode::Raise:
                next = height + amount;
                break;
            case BrushMode::Lower:
                next = height - amount;
                break;
            case BrushMode::Smooth: {
                const float mean = 0.25f * (previous(x - 1, z) + previous(x + 1, z) +
                                            previous(x, z - 1) + previous(x, z + 1));
                next = std::lerp(height, mean, 1.0f - std::exp(-amount));
                break;
            }
            case BrushMode::Flatten:
                next = std::lerp(height, brush.targetHeight, 1.0f - std::exp(-amount));
                break;
            }
            next = std::clamp(next, -kHeightLimit, kHeightLimit);
            if (next != height) {
                height = next;
                changedLow = glm::min(changedLow, glm::ivec2{x, z});
                changedHigh = glm::max(changedHigh, glm::ivec2{x, z});
            }
        }
    }
    if (changedLow.x > changedHigh.x || changedLow.y > changedHigh.y) {
        return false;
    }
    Invalidate(changedLow, changedHigh);
    return true;
}

//————————————————————————————————————————————————————————————————————————————————
// 用与绘制一致的两个平面分别求射线交点，再检查点是否落在该半个方格内。
//————————————————————————————————————————————————————————————————————————————————
bool HeightField::Raycast(glm::vec3 origin, glm::vec3 direction, glm::vec3& hit) const {
    if (mCache || mTile) return false;
    float nearest = std::numeric_limits<float>::infinity();
    const auto gridOrigin = glm::vec2{origin.x, origin.z} / mGrid.Spacing();
    const auto dxz = glm::vec2{direction.x, direction.z} / mGrid.Spacing();
    for (int z = mGrid.FirstSample(); z < mGrid.LastSample(); ++z) {
        for (int x = mGrid.FirstSample(); x < mGrid.LastSample(); ++x) {
            const auto oxz = gridOrigin - glm::vec2{x, z};
            const float a = Sample(x, z);
            const float b = Sample(x + 1, z);
            const float c = Sample(x, z + 1);
            const float d = Sample(x + 1, z + 1);
            const auto intersect = [&](float intercept, glm::vec2 slope, bool first) {
                const float denominator = direction.y - glm::dot(slope, dxz);
                if (std::abs(denominator) < 1.0e-6f) {
                    return;
                }
                const float distance = (intercept + glm::dot(slope, oxz) - origin.y) / denominator;
                if (distance < 0.0f || distance >= nearest) {
                    return;
                }
                const auto p = oxz + dxz * distance;
                constexpr float eps = 1.0e-4f;
                if (p.x >= -eps && p.x <= 1.0f + eps && p.y >= -eps && p.y <= 1.0f + eps &&
                    (first ? p.x + p.y <= 1.0f + eps : p.x + p.y >= 1.0f - eps)) {
                    nearest = distance;
                }
            };
            intersect(a, {b - a, c - a}, true);
            intersect(c + b - d, {d - c, d - b}, false);
        }
    }
    if (!std::isfinite(nearest)) {
        return false;
    }
    hit = origin + direction * nearest;
    return true;
}

//————————————————————————————————————————————————————————————————————————————————
// 整表校验成功才替换，调用者可继续使用已有的高度场对象。
//————————————————————————————————————————————————————————————————————————————————
void HeightField::ReplaceSamples(const Samples& samples) {
    if (samples.size() != mHeights.size()) throw std::runtime_error("Invalid terrain sample count.");
    for (const auto height : samples) {
        if (!std::isfinite(height) || std::abs(height) > kHeightLimit) {
            throw std::runtime_error("Terrain sample height is out of range.");
        }
    }
    mHeights = samples;
    Invalidate(glm::ivec2{mGrid.FirstSample()}, glm::ivec2{mGrid.LastSample()});
}

//————————————————————————————————————————————————————————————————————————————————
// 块的样点边界是闭区间，触及共边时相邻两块都会更新。
//————————————————————————————————————————————————————————————————————————————————
void HeightField::Invalidate(glm::ivec2 low, glm::ivec2 high) {
    mRevision = NextRevision();
    low -= 1;
    high += 1;
    std::size_t index = 0;
    for (int z = mGrid.FirstKey(); z < mGrid.EndKey(); ++z) {
        for (int x = mGrid.FirstKey(); x < mGrid.EndKey(); ++x, ++index) {
            const glm::ivec2 start = glm::ivec2{x, z} * mGrid.Cells();
            const glm::ivec2 end = start + mGrid.Cells();
            if (start.x <= high.x && end.x >= low.x && start.y <= high.y && end.y >= low.y) {
                mChunkRevisions[index] = mRevision;
            }
        }
    }
}

}  // namespace tinyworld
