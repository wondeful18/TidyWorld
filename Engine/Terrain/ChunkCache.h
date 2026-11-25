#pragma once

#include "Engine/Terrain/ChunkFile.h"

#include <list>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 工作线程发布块，查询线程借用不可变数据。GPU 逐出独立于最近访问的 CPU 块。
//————————————————————————————————————————————————————————————————————————————————
class ChunkCache {
public:
    static constexpr std::size_t kCapacity = 49; // 最近访问块的 CPU 驻留上限

    explicit ChunkCache(TerrainGrid grid, std::vector<std::string> versions = {})
        : mGrid(grid), mVersions(std::move(versions)) {}

    //————————————————————————————————————————————————————————————————————————————————
    // 解析当前快照中的块文件。无覆盖时使用初次打包的默认文件。
    //————————————————————————————————————————————————————————————————————————————————
    std::filesystem::path File(const std::filesystem::path& directory, glm::ivec2 key) const;

    //————————————————————————————————————————————————————————————————————————————————
    // 命中时把该块移到最近访问端。借出的块可安全跨过下一次淘汰。
    //————————————————————————————————————————————————————————————————————————————————
    std::shared_ptr<const ChunkTile> Find(glm::ivec2 key) const;
    void Store(glm::ivec2 key, std::shared_ptr<const ChunkTile> tile);
    std::size_t Size() const;

    //————————————————————————————————————————————————————————————————————————————————
    // 游戏线程报告阻塞步行的块，渲染线程会将它与当前视野需求合并。
    //————————————————————————————————————————————————————————————————————————————————
    void Prioritize(glm::ivec2 key);
    std::optional<glm::ivec2> TakePriority();

private:
    struct Entry {
        std::shared_ptr<const ChunkTile> tile;  // CPU 高度与同块资源
        std::list<std::size_t>::iterator age;  // 最近访问顺序中的位置
    };

    TerrainGrid mGrid;  // 块键到缓存索引的映射
    const std::vector<std::string> mVersions;  // 此快照中各块的版本目录，空值用基础文件
    mutable std::mutex mMutex;  // 同时保护缓存、顺序和优先请求
    mutable std::list<std::size_t> mRecent;  // 队首是最近访问的块
    mutable std::unordered_map<std::size_t, Entry> mEntries;  // 缓存中的已读块
    std::optional<glm::ivec2> mPriority;  // 步行等待的下一个块
};

}  // namespace tinyworld
