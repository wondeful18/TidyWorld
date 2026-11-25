#pragma once

#include "Engine/Scene/WorldScene.h"
#include "Engine/Terrain/ChunkFile.h"

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 离线把原有游线与外围地形合成；沿每个块边复制同一份样点。
//————————————————————————————————————————————————————————————————————————————————
class WorldChunkPacking {
public:
    WorldChunkPacking(const WorldScene& scene, bool exploration);
    [[nodiscard]] TerrainGrid Grid() const { return mHeights.Grid(); }
    [[nodiscard]] const std::optional<RoadSpline>& Road() const { return mRoad; }
    [[nodiscard]] ChunkTile MakeTile(glm::ivec2 key) const;

private:
    const WorldScene& mScene;  // 借用编辑器中的材质与摆件
    HeightField mHeights;     // 供打包期间查询的完整高度，运行时无需这份数组
    std::optional<RoadSpline> mRoad; // 结业布局可在原游线前接山脊步道
    std::vector<std::vector<SceneVertex>> mRoads; // 每个块内的道路三角形
    std::vector<std::vector<PlacedProp>> mProps;  // 按原点分派到块的实例
};

}  // namespace tinyworld
