#pragma once

#include "Engine/Terrain/HeightField.h"

#include <filesystem>

namespace tinyworld {

struct WorldScene;
struct RoadSpline;

//————————————————————————————————————————————————————————————————————————————————
// 将编辑状态写成当前章节的 JSON。出错时抛出异常，保留已有的存档。
//————————————————————————————————————————————————————————————————————————————————
void SaveScene(const std::filesystem::path& path, const WorldScene& scene);

//————————————————————————————————————————————————————————————————————————————————
// 读取完整场景后替换当前状态。读取或校验失败时抛出异常。
//————————————————————————————————————————————————————————————————————————————————
void LoadScene(const std::filesystem::path& path, WorldScene& scene);

//————————————————————————————————————————————————————————————————————————————————
// 将当前道路可用的植被避让遮罩写为二值 PGM，保留已有文件至写入成功。
//————————————————————————————————————————————————————————————————————————————————
void ExportRoadMask(const std::filesystem::path& path, const RoadSpline& road,
                    TerrainGrid grid = TerrainGrid{});

//————————————————————————————————————————————————————————————————————————————————
// 从旧 JSON 导入离线素材，或者从编辑器源场景更新同布局的包。未变的块保持原文件。
//————————————————————————————————————————————————————————————————————————————————
void SaveWorldPackage(const std::filesystem::path& directory, const WorldScene& scene,
                      bool exploration = false, bool compress = true);
void PackWorldFile(const std::filesystem::path& source, const std::filesystem::path& directory,
                   bool exploration, bool compress);

}  // namespace tinyworld
