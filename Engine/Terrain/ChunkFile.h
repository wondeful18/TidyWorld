#pragma once

#include "Engine/Scene/PlacedProp.h"
#include "Engine/Terrain/MaterialSplat.h"

#include <filesystem>

namespace tinyworld {

inline constexpr int kSplatTileCells = 32; // 每块权重纹理的有效像素数
inline constexpr int kSplatTileSide = kSplatTileCells + 2; // 线性采样用的外圈像素

//————————————————————————————————————————————————————————————————————————————————
// 一块世界数据。高度里的外圈样点也供边缘法线和植被坡度查询使用。
//————————————————————————————————————————————————————————————————————————————————
struct ChunkTile {
    HeightField heights;           // 块内和一圈幽灵边界的高度，米
    MaterialSplat::Pixels splat;   // 按 Z 行排列的线性 RGBA8 权重
    std::vector<PlacedProp> props;  // 原点落在本块的实例
    std::vector<SceneVertex> road;  // 裁入本块的道路三角形
};

struct ChunkReadTimes {
    double diskMs{};        // 打开并读取完整文件的墙钟时间，毫秒
    double decompressMs{};  // LZ4 解压时间，毫秒
};

//————————————————————————————————————————————————————————————————————————————————
// 生成带符号块键的文件名，例如 c_+00_-01.twchunk。
//————————————————————————————————————————————————————————————————————————————————
std::filesystem::path ChunkPath(const std::filesystem::path& world, glm::ivec2 key);

//————————————————————————————————————————————————————————————————————————————————
// 读取和校验一个块文件。文件格式与机器的结构体对齐无关。
//————————————————————————————————————————————————————————————————————————————————
ChunkTile ReadChunk(const std::filesystem::path& path, TerrainGrid grid, glm::ivec2 key, ChunkReadTimes& times);

//————————————————————————————————————————————————————————————————————————————————
// 返回可直接原子写入文件的 TWCK v1 字节串。压缩无收益时保留原始块体。
//————————————————————————————————————————————————————————————————————————————————
std::string EncodeChunk(const ChunkTile& tile, glm::ivec2 key, bool compress);

}  // namespace tinyworld
