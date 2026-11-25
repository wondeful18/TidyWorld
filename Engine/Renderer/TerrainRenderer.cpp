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
// 将地形顶点转换成块内坐标，绘制时再加块原点。
//————————————————————————————————————————————————————————————————————————————————
std::vector<WorldVertex> TerrainRenderer::LocalVertices(const std::vector<SceneVertex>& geometry,
                                                       TerrainGrid grid, glm::ivec2 key) {
    auto vertices = WithReflectivity(geometry);
    const auto origin = glm::vec2(key) * grid.ChunkSize();
    for (auto& vertex : vertices) {
        vertex.geometry.position[0] -= origin.x;
        vertex.geometry.position[2] -= origin.y;
    }
    return vertices;
}


//————————————————————————————————————————————————————————————————————————————————
// 在需要流送的章节中先分配 GPU 槽，首帧根据视点请求块。
//————————————————————————————————————————————————————————————————————————————————
TerrainRenderer::TerrainRenderer(WebGpuContext& gpu, WGPUBindGroupLayout layout, const WorldScene& scene,
                                 SurfaceMaterials* materials, VegetationRenderer* vegetation)
    : mGpu(gpu), mObjectLayout(layout), mMaterials(materials), mVegetation(vegetation) {
    const auto& terrain = *scene.terrain;
    mTerrainGrid = terrain.Grid();
    BuildTerrainIndices(mTerrainGrid);
    if (scene.streamTerrain) mStream.emplace();
    if (mStream) {
        // 分配索引槽而不是 GPU 顶点。第一个请求由 Encode 的视点决定。
        mTerrainDraws.resize(mTerrainGrid.ChunkCount());
        mRoadMesh.emplace(MakeDrawBinding(mGpu, layout));
    } else {
        for (int z = mTerrainGrid.FirstKey(); z < mTerrainGrid.EndKey(); ++z) {
            for (int x = mTerrainGrid.FirstKey(); x < mTerrainGrid.EndKey(); ++x) {
                const auto mesh = MakeTerrainVertices(terrain, {x, z}, !scene.road);
                const auto index = mTerrainDraws.size();
                mTerrainDraws.push_back(UploadMesh(mGpu, LocalVertices(mesh, mTerrainGrid, {x, z}), layout, "Terrain chunk"));
                mTerrainBounds[index] = MeasureTerrainBounds(terrain, {x, z});
                mChunkRevisions[index] = terrain.ChunkRevision(index);
            }
        }
    }
    if (!terrain.IsStreamed()) {
        mChunkBorders.emplace(UploadMesh(mGpu, WithReflectivity(MakeChunkBorders(terrain)), layout, "Chunk borders"));
        mBorderRevision = terrain.Revision();
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 处理笔刷修订或后台交付，再准备本帧颜色与深度需要的块。
//————————————————————————————————————————————————————————————————————————————————
void TerrainRenderer::Sync(const WorldScene& scene) {
    if (mMaterials && scene.paint) {
        mMaterials->SyncPaint(mGpu.Queue(), *scene.paint);
    }
    const auto& terrain = *scene.terrain;
    if (mStream) {
        SyncStreamedTerrain(scene);
        SyncTerrainBorders(terrain, scene.showChunkBorders);
    } else {
        SyncTerrain(terrain, scene.showChunkBorders, !scene.road);
        if (scene.road) SyncRoad(terrain, *scene.road);
        if (mVegetation && scene.vegetation) mVegetation->Sync(terrain, scene.road, *scene.vegetation);
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 共边附近的笔刷会给两个块相同的新修订号，两个通道会一起看到新网格。
//————————————————————————————————————————————————————————————————————————————————
void TerrainRenderer::SyncTerrain(const HeightField& field, bool showBorders, bool paintTrail) {
    std::size_t index = 0;
    for (int z = field.Grid().FirstKey(); z < field.Grid().EndKey(); ++z) {
        for (int x = field.Grid().FirstKey(); x < field.Grid().EndKey(); ++x, ++index) {
            const auto revision = field.ChunkRevision(index);
            if (mChunkRevisions[index] == revision) {
                continue;
            }
            const auto vertices = LocalVertices(MakeTerrainVertices(field, {x, z}, paintTrail), field.Grid(), {x, z});
            wgpuQueueWriteBuffer(mGpu.Queue(), mTerrainDraws[index].vertices.get(), 0,
                                 vertices.data(), vertices.size() * sizeof(WorldVertex));
            mTerrainBounds[index] = MeasureTerrainBounds(field, {x, z});
            mChunkRevisions[index] = revision;
        }
    }
    SyncTerrainBorders(field, showBorders);
}

//————————————————————————————————————————————————————————————————————————————————
// 调试边界继续读全局高度，在打开开关后按修订号更新。
//————————————————————————————————————————————————————————————————————————————————
void TerrainRenderer::SyncTerrainBorders(const HeightField& field, bool showBorders) {
    if (mChunkBorders && showBorders && mBorderRevision != field.Revision()) {
        const auto vertices = WithReflectivity(MakeChunkBorders(field));
        wgpuQueueWriteBuffer(mGpu.Queue(), mChunkBorders->vertices.get(), 0,
                             vertices.data(), vertices.size() * sizeof(WorldVertex));
        mBorderRevision = field.Revision();
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 共享索引只随布局变化。粗网格保留原始共边样点。
 //————————————————————————————————————————————————————————————————————————————————
void TerrainRenderer::BuildTerrainIndices(TerrainGrid grid) {
    const std::size_t count = grid == TerrainGrid::Exploration() ? 4 : 2;
    for (auto& indices : mTerrainIndices) indices.reset();
    mTerrainIndexCounts = {};
    for (std::size_t lod = 0; lod < count; ++lod) {
        const auto indices = MakeTerrainIndices(grid.Cells(), 1 << lod);
        const auto bytes = indices.size() * sizeof(indices.front());
        mTerrainIndices[lod] = MakeBuffer(mGpu.Device(), WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst,
                                           bytes, "Shared terrain indices");
        wgpuQueueWriteBuffer(mGpu.Queue(), mTerrainIndices[lod].get(), 0, indices.data(), bytes);
        mTerrainIndexCounts[lod] = static_cast<std::uint32_t>(indices.size());
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 避让边距不改变路面，拖动控制点或雕刻高度才需要裁剪并上传。
//————————————————————————————————————————————————————————————————————————————————
void TerrainRenderer::SyncRoad(const HeightField& field, const RoadSpline& road) {
    if (mLastRoad && mLastRoad->points == road.points && mLastRoad->width == road.width &&
        mLastRoad->junction == road.junction && mLastRoad->junctionPoint == road.junctionPoint &&
        mLastRoad->branchLength == road.branchLength &&
        mRoadTerrainRevision == field.Revision()) {
        return;
    }
    const auto mesh = MakeRoadMesh(field, road);
    if (mesh.empty()) {
        mRoadMesh.reset();
    } else {
        mRoadMesh.emplace(UploadMesh(mGpu, WithReflectivity(mesh), mObjectLayout, "Spline road"));
    }
    mLastRoad = road;
    mRoadTerrainRevision = field.Revision();
}

//————————————————————————————————————————————————————————————————————————————————
// 颜色通道先做相机裁剪。每块的 LOD 与常量也供太阳深度通道使用。
//————————————————————————————————————————————————————————————————————————————————

void TerrainRenderer::Prepare(const WorldScene& scene, const glm::mat4& viewProjection, glm::vec3 eye,
                              const DrawUniformWriter& uniforms, const glm::vec4& shadows, bool showSpheres) {
    const bool exploration = mTerrainGrid == TerrainGrid::Exploration();
    mTerrainStats = {};
    const auto frustum = MakeTerrainFrustum(viewProjection);
    const bool useLod = scene.terrainPreview ? scene.terrainPreview->lod : scene.previewLod;
    const bool cull = scene.terrainPreview && scene.terrainPreview->cull;
    for (std::size_t i = 0; i < mTerrainDraws.size(); ++i) {
        if (!mTerrainDraws[i].count) continue;
        const auto key = mTerrainGrid.Key(i);
        const auto center = (glm::vec2(key) + 0.5f) * mTerrainGrid.ChunkSize();
        const auto distance = glm::length(center - glm::vec2{eye.x, eye.z});
        mLod[i] = !useLod ? 0 : exploration
            ? (distance > 224.0f ? 3 : distance > 128.0f ? 2 : distance > 55.0f ? 1 : 0)
            : (IsCoarseTerrainChunk(key, eye) ? 1 : 0);
        mTerrainVisible[i] = !cull || IntersectsTerrainFrustum(frustum, mTerrainBounds[i]);
    }
    SurfaceParameters terrainMaterial;
    if (scene.materials && scene.materials->view != MaterialView::VertexColor) {
        terrainMaterial.terrainMode = 1 + static_cast<std::uint32_t>(scene.materials->view);
        terrainMaterial.showTrail = scene.road ? 0.0f : 1.0f;
        terrainMaterial.splatOrigin = glm::vec2{-mTerrainGrid.HalfExtent()};
        terrainMaterial.splatExtent = 2.0f * mTerrainGrid.HalfExtent();
    }
    for (std::size_t i = 0; i < mTerrainDraws.size(); ++i) {
        if (!mTerrainDraws[i].count) continue;
        auto material = terrainMaterial;
        if (mStreamCache) {
            const float chunkSize = mTerrainGrid.ChunkSize();
            const float pixel = chunkSize / kSplatTileCells;
            material.splatOrigin = glm::vec2(mTerrainGrid.Key(i)) * chunkSize - pixel;
            material.splatExtent = chunkSize + 2.0f * pixel;
        }
        const auto origin = glm::vec2(mTerrainGrid.Key(i)) * mTerrainGrid.ChunkSize();
        glm::mat4 model{1.0f};
        model[3] = {origin.x, 0.0f, origin.y, 1.0f};
        uniforms.Set(mTerrainDraws[i], model, shadows, material);
    }
    mShowBorders = !scene.storage && mChunkBorders.has_value() && scene.showChunkBorders && !showSpheres;
    mShowRoad = mRoadMesh.has_value() && scene.road && !showSpheres;
    if (mShowRoad) {
        uniforms.Set(*mRoadMesh, glm::mat4{1.0f}, shadows,
                      SurfaceParameters{.roadSurface = 1 + static_cast<std::uint32_t>(scene.road->surface)});
    }
    if (mShowBorders) {
        uniforms.Set(*mChunkBorders, glm::mat4{1.0f}, glm::vec4{0.0f});
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 提交地形索引三角形。相机视野外的块仍可向视野内投影。
//————————————————————————————————————————————————————————————————————————————————

unsigned TerrainRenderer::Draw(WGPURenderPassEncoder pass, bool shadows, const TerrainFrustum* shadowFrustum) {
    unsigned draws = 0;
    for (std::size_t i = 0; i < mTerrainDraws.size(); ++i) {
        if (!mTerrainDraws[i].count) continue;
        if (shadowFrustum && !IntersectsTerrainFrustum(*shadowFrustum, mTerrainBounds[i])) continue;
        // 相机画面外的地形仍可能把阴影投到画面内。
        if (!shadows && !mTerrainVisible[i]) {
            ++mTerrainStats.culled;
            continue;
        }
        const auto& draw = mTerrainDraws[i];
        const std::size_t lod = mLod[i];
        const auto count = mTerrainIndexCounts[lod];
        wgpuRenderPassEncoderSetBindGroup(pass, 0, draw.binding.get(), 0, nullptr);
        if (!shadows && mStreamCache) wgpuRenderPassEncoderSetBindGroup(pass, 3, mTileMaterials[i].get(), 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, draw.vertices.get(), 0, draw.count * sizeof(WorldVertex));
        wgpuRenderPassEncoderSetIndexBuffer(pass, mTerrainIndices[lod].get(), WGPUIndexFormat_Uint16, 0,
                                            count * sizeof(std::uint16_t));
        wgpuRenderPassEncoderDrawIndexed(pass, count, 1, 0, 0, 0);
        ++draws;
        if (shadows) {
            mTerrainStats.shadowTriangles += count / 3;
        } else {
            ++mTerrainStats.visible;
            ++(mLod[i] ? mTerrainStats.coarse : mTerrainStats.fine);
            ++mTerrainStats.lods[lod];
            mTerrainStats.colorTriangles += count / 3;
        }
    }
    if (!shadows && mStreamCache) wgpuRenderPassEncoderSetBindGroup(pass, 3, mMaterials->Binding(), 0, nullptr);
    return draws;
}

//————————————————————————————————————————————————————————————————————————————————
// 道路与调试边界只在不透明通道叠绘。
//————————————————————————————————————————————————————————————————————————————————

unsigned TerrainRenderer::DrawOverlays(WGPURenderPassEncoder pass) const {
    unsigned draws = 0;
    if (mShowRoad) {
        // 路面接收地形和地标的阴影；薄路面本身只在颜色通道绘制。
        if (mStream) {
            for (const auto& tile : mStreamRoadDraws) {
                if (!tile.count) continue;
                DrawMesh(pass, tile, mRoadMesh->binding.get());
                ++draws;
            }
        } else {
            DrawMesh(pass, *mRoadMesh);
            ++draws;
        }
    }
    if (mShowBorders) {
        DrawMesh(pass, *mChunkBorders);
        ++draws;
    }
    return draws;
}

//————————————————————————————————————————————————————————————————————————————————
// 旧 JSON 场景的摆件只在地形块已经上传时绘制。
//————————————————————————————————————————————————————————————————————————————————

bool TerrainRenderer::ResidentAt(glm::vec3 position) const {
    return !mStream || mTerrainDraws[mTerrainGrid.Index(mTerrainGrid.KeyAt({position.x, position.z}))].count != 0;
}

}  // namespace tinyworld
