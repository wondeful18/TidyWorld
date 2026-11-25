#include "Engine/Terrain/ChunkCache.h"

#include <utility>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 运行时在打开清单后固定块路径，编辑器可以同时发布下一版清单。
//————————————————————————————————————————————————————————————————————————————————
std::filesystem::path ChunkCache::File(const std::filesystem::path& directory, glm::ivec2 key) const {
    const auto base = ChunkPath(directory, key);
    if (mVersions.empty()) return base;
    const auto& version = mVersions[mGrid.Index(key)];
    return version.empty() ? base : directory / "chunks" / "updates" / version / base.filename();
}

//————————————————————————————————————————————————————————————————————————————————
// LRU 的查找和移动在同一把锁内完成，返回后不再持锁。
//————————————————————————————————————————————————————————————————————————————————
std::shared_ptr<const ChunkTile> ChunkCache::Find(glm::ivec2 key) const {
    if (!mGrid.Contains(key)) return {};
    const std::lock_guard lock(mMutex);
    const auto item = mEntries.find(mGrid.Index(key));
    if (item == mEntries.end()) return {};
    mRecent.splice(mRecent.begin(), mRecent, item->second.age);
    return item->second.tile;
}

//————————————————————————————————————————————————————————————————————————————————
// 缓存至多保留 49 个正在使用或最近访问过的块。
//————————————————————————————————————————————————————————————————————————————————
void ChunkCache::Store(glm::ivec2 key, std::shared_ptr<const ChunkTile> tile) {
    const std::lock_guard lock(mMutex);
    const auto index = mGrid.Index(key);
    if (auto item = mEntries.find(index); item != mEntries.end()) {
        item->second.tile = std::move(tile);
        mRecent.splice(mRecent.begin(), mRecent, item->second.age);
        return;
    }
    mRecent.push_front(index);
    mEntries.emplace(index, Entry{std::move(tile), mRecent.begin()});
    if (mEntries.size() > kCapacity) {
        mEntries.erase(mRecent.back());
        mRecent.pop_back();
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 返回当前 CPU 驻留数量，用于面板和测试。
//————————————————————————————————————————————————————————————————————————————————
std::size_t ChunkCache::Size() const {
    const std::lock_guard lock(mMutex);
    return mEntries.size();
}

//————————————————————————————————————————————————————————————————————————————————
// 每帧只保留步行实际阻塞的最后一个请求。
//————————————————————————————————————————————————————————————————————————————————
void ChunkCache::Prioritize(glm::ivec2 key) {
    if (!mGrid.Contains(key)) return;
    const std::lock_guard lock(mMutex);
    mPriority = key;
}

//————————————————————————————————————————————————————————————————————————————————
// 将待处理请求移交给流送队列。
//————————————————————————————————————————————————————————————————————————————————
std::optional<glm::ivec2> ChunkCache::TakePriority() {
    const std::lock_guard lock(mMutex);
    return std::exchange(mPriority, {});
}

}  // namespace tinyworld
