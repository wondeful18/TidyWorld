#include "Engine/Renderer/TerrainRenderer.h"

#include "Engine/Renderer/SurfaceMaterials.h"
#include "Engine/Renderer/VegetationRenderer.h"
#include "Engine/Renderer/WebGpuContext.h"
#include "Engine/Scene/WorldScene.h"
#include "Engine/Terrain/TerrainMesh.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <chrono>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 每帧先回收越界块，再上传最多两份 CPU 结果。旧版本可显示到替换成功为止。
//————————————————————————————————————————————————————————————————————————————————
void TerrainRenderer::SyncStreamedTerrain(const WorldScene& scene) {
    const auto focus = scene.IsWalking() ? scene.WalkerPose().position : scene.camera.target;
    const auto grid = scene.terrain->Grid();
    const auto cache = scene.storage ? scene.storage->cache : nullptr;
    if (grid != mTerrainGrid || mStreamCache != cache) {
        if (grid.Cells() != mTerrainGrid.Cells()) BuildTerrainIndices(grid);
        mStreamCache = cache;
        // 同一数组槽在新行宽下对应不同的键，必须先清空旧资源。
        for (std::size_t index = 0; index < mTerrainDraws.size(); ++index) mVegetation->EvictChunk(index);
        mTerrainDraws.clear();
        mTerrainDraws.resize(grid.ChunkCount());
        mStreamRoadDraws = decltype(mStreamRoadDraws)();
        mTileMaterials.clear();
        mTileProps.clear();
        if (cache) {
            mTileMaterials.resize(grid.ChunkCount());
            mTileProps.resize(grid.ChunkCount());
        }
        mChunkBytes.fill(0);
        mStreamStats = {};
        mTerrainGrid = grid;
        mChunkBorders.reset();
        if (!cache) {
            mChunkBorders.emplace(UploadMesh(mGpu, WithReflectivity(MakeChunkBorders(*scene.terrain)),
                                             mObjectLayout, "Chunk borders"));
            mBorderRevision = scene.terrain->Revision();
        }
    }
    mStream->Update(grid.KeyAt({focus.x, focus.z}), *scene.terrain, scene.road, *scene.vegetation,
                    scene.storage ? scene.storage->directory : std::filesystem::path{}, cache);
    auto& stats = mStreamStats;
    stats.uploaded = 0;
    stats.uploadedBytes = 0;
    stats.uploadMs = 0.0;
    for (std::size_t index = 0; index < mTerrainDraws.size(); ++index) {
        if (!mTerrainDraws[index].count || mStream->Wants(index)) continue;
        mTerrainDraws[index] = Drawable();
        mStreamRoadDraws[index] = Drawable();
        if (cache) {
            mTileMaterials[index].reset();
            mTileProps[index] = std::vector<PlacedProp>{};
        }
        mVegetation->EvictChunk(index);
        stats.residentBytes -= mChunkBytes[index];
        mChunkBytes[index] = 0;
        --stats.resident;
        ++stats.evicted;
    }
    const unsigned limit = grid == TerrainGrid::Exploration() ? 6 : ChunkStreamer::kUploadsPerFrame;
    const auto uploadStart = std::chrono::steady_clock::now();
    while (stats.uploaded < limit) {
        auto cpu = mStream->TakeReady();
        if (!cpu) break;
        const auto index = cpu->index;
        auto fine = UploadMesh(mGpu, LocalVertices(cpu->vertices, grid, grid.Key(index)), mObjectLayout, "Streamed terrain LOD 0");
        Drawable road;
        const auto& roadVertices = cpu->tile ? cpu->tile->road : cpu->road;
        if (!roadVertices.empty()) road = UploadMesh(mGpu, WithReflectivity(roadVertices), nullptr, "Streamed spline road");
        auto bytes = (fine.count + road.count) * sizeof(WorldVertex) + mVegetation->UploadChunk(index, cpu->plants);
        if (cpu->tile) {
            mTileMaterials[index] = mMaterials->UploadTile(mGpu, cpu->tile->splat, kSplatTileSide);
            mTileProps[index] = cpu->tile->props;
            bytes += cpu->tile->splat.size() * sizeof(MaterialSplat::Pixel);
            stats.cacheHits += cpu->cacheHit;
            if (!cpu->cacheHit) stats.io = cpu->io;
        }
        stats.meshMs = cpu->meshMs;
        if (!mTerrainDraws[index].count) ++stats.resident;
        stats.residentBytes = stats.residentBytes - mChunkBytes[index] + bytes;
        mTerrainDraws[index] = std::move(fine);
        mStreamRoadDraws[index] = std::move(road);
        mTerrainBounds[index] = cpu->bounds;
        mChunkBytes[index] = bytes;
        stats.uploadedBytes += bytes;
        ++stats.uploaded;
    }
    stats.queue = mStream->Statistics();
    if (stats.uploaded) {
        stats.uploadMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - uploadStart).count();
    }
    if (cache) stats.cpuResident = static_cast<unsigned>(cache->Size());
    stats.peakResident = std::max(stats.peakResident, stats.resident);
    stats.peakUploadedBytes = std::max(stats.peakUploadedBytes, stats.uploadedBytes);
}

//————————————————————————————————————————————————————————————————————————————————
// 离屏捕获等三张稳定帧。正在等待替换的旧块依然计入驻留。
//————————————————————————————————————————————————————————————————————————————————
bool TerrainRenderer::StreamingSettled() const {
    if (!mStream) return true;
    const auto& s = mStreamStats;
    return s.queue.wanted != 0 && s.resident == s.queue.wanted &&
           s.queue.queued == 0 && s.queue.working == 0 && s.queue.ready == 0;
}

void TerrainRenderer::WaitForChunkData() {
    if (mStream) mStream->WaitForResult(std::chrono::milliseconds(200));
}

//————————————————————————————————————————————————————————————————————————————————
// 前面的章节走原有整图合批，第二十章遍历已上传的实例流。
//————————————————————————————————————————————————————————————————————————————————
std::uint32_t TerrainRenderer::DrawVegetation(WGPURenderPassEncoder pass, bool instanced, bool shadows) const {
    if (!mStream) return mVegetation->Draw(pass, instanced, shadows);
    std::uint32_t draws{};
    for (std::size_t index = 0; index < mTerrainDraws.size(); ++index) {
        if (mTerrainDraws[index].count) draws += mVegetation->DrawChunk(pass, index, instanced, shadows);
    }
    return draws;
}

}  // namespace tinyworld
