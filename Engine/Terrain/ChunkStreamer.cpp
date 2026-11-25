#include "Engine/Terrain/ChunkStreamer.h"

#include "Engine/Terrain/TerrainMesh.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace tinyworld {

namespace {

//————————————————————————————————————————————————————————————————————————————————
// 第九章已把路面裁进半米格。用三角形重心可以无缝分派到块键。
//————————————————————————————————————————————————————————————————————————————————
auto SplitRoad(const HeightField& field, const std::optional<RoadSpline>& road) {
    std::vector<std::vector<SceneVertex>> chunks(field.Grid().ChunkCount());
    if (!road) return chunks;
    const auto mesh = MakeRoadMesh(field, *road);
    const auto grid = field.Grid();
    for (std::size_t i = 0; i < mesh.size(); i += 3) {
        glm::vec2 center{0.0f};
        for (std::size_t j = i; j < i + 3; ++j) {
            center += glm::vec2{mesh[j].position[0], mesh[j].position[2]};
        }
        auto& chunk = chunks[grid.Index(grid.KeyAt(center / 3.0f))];
        chunk.insert(chunk.end(), mesh.begin() + i, mesh.begin() + i + 3);
    }
    return chunks;
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 线程等待需求，析构时 stop_token 会唤醒停止等待的条件变量。
//————————————————————————————————————————————————————————————————————————————————
ChunkStreamer::ChunkStreamer() : mWorker([this](std::stop_token stop) { Run(stop); }) {}

//————————————————————————————————————————————————————————————————————————————————
// 视点改变时取消旧需求，场景源改变时所有旧结果都需要重新生成。
//————————————————————————————————————————————————————————————————————————————————
void ChunkStreamer::Update(glm::ivec2 focus, const HeightField& heights, const std::optional<RoadSpline>& road,
                           const VegetationSettings& plants, const std::filesystem::path& directory,
                           std::shared_ptr<ChunkCache> cache) {
    const std::lock_guard lock(mMutex);
    const bool changed = !mSource || mSource->heights.Revision() != heights.Revision() ||
                         mSource->directory != directory || mSource->cache != cache || mSource->road != road || mSource->plants.mode != plants.mode ||
                         mSource->plants.seed != plants.seed || mSource->plants.density != plants.density ||
                         mSource->plants.minPlantHeight != plants.minPlantHeight;
    const auto urgent = cache ? cache->TakePriority() : std::nullopt;
    if (!changed && focus == mFocus && !urgent && !mUrgent) return;
    mUrgent = urgent;
    const auto grid = heights.Grid();
    if (!mSource || mSource->heights.Grid() != grid) {
        mSlots.clear();
        mSlots.resize(grid.ChunkCount());
    }
    if (changed) {
        mSource = std::make_shared<const Source>(Source{heights, road, plants, directory, std::move(cache)});
        mFailure = nullptr;
    }
    mFocus = focus;
    mPriority.clear();
    const int radius = grid == TerrainGrid::Exploration() ? 10 : kRadius;
    for (std::size_t index = 0; index < grid.ChunkCount(); ++index) {
        const auto delta = grid.Key(index) - focus;
        auto& slot = mSlots[index];
        const bool priority = urgent && grid.Key(index) == *urgent;
        if (!priority && std::max(std::abs(delta.x), std::abs(delta.y)) > radius) {
            slot = {};
            continue;
        }
        if (changed || slot.state == State::Empty || (priority && slot.state == State::Delivered)) {
            slot = {State::Queued, std::nullopt, ++mTicket};
        }
        mPriority.push_back(index);
    }
    std::stable_sort(mPriority.begin(), mPriority.end(), [focus, grid, urgent](std::size_t a, std::size_t b) {
        if (urgent) {
            const bool aFirst = grid.Key(a) == *urgent;
            const bool bFirst = grid.Key(b) == *urgent;
            if (aFirst != bFirst) return aFirst;
        }
        const auto distance = [focus, grid](std::size_t index) {
            const auto d = grid.Key(index) - focus;
            return d.x * d.x + d.y * d.y;
        };
        return distance(a) < distance(b);
    });
    mChanged.notify_all();
}

//————————————————————————————————————————————————————————————————————————————————
// 在已加锁的范围内按当前优先顺序找一个指定状态的键。
//————————————————————————————————————————————————————————————————————————————————
std::optional<std::size_t> ChunkStreamer::FirstWithState(State state) const {
    const auto match = std::find_if(mPriority.begin(), mPriority.end(),
                                    [&](std::size_t index) { return mSlots[index].state == state; });
    if (match == mPriority.end()) return std::nullopt;
    return *match;
}

//————————————————————————————————————————————————————————————————————————————————
// 先认领键，再脱锁构建 CPU 数据。发布时复核键与不可变源的身份。
//————————————————————————————————————————————————————————————————————————————————
void ChunkStreamer::Run(std::stop_token stop) {
    std::shared_ptr<const Source> lastSource;
    RoadMask mask;
    std::vector<std::vector<SceneVertex>> roadChunks;
    while (!stop.stop_requested()) {
        std::shared_ptr<const Source> source;
        std::size_t index{};
        std::uint64_t ticket{};
        {
            std::unique_lock lock(mMutex);
            if (!mChanged.wait(lock, stop, [&] {
                constexpr auto readyLimit = 8;
                const auto ready = std::count_if(mPriority.begin(), mPriority.end(),
                    [&](std::size_t item) { return mSlots[item].state == State::Ready; });
                const bool urgentQueued = mUrgent &&
                    mSlots[mSource->heights.Grid().Index(*mUrgent)].state == State::Queued;
                return !mFailure && (ready < readyLimit || urgentQueued) &&
                       FirstWithState(State::Queued).has_value();
            })) return;
            if (stop.stop_requested()) return;
            index = *FirstWithState(State::Queued);
            mSlots[index].state = State::Working;
            source = mSource;
            ticket = mSlots[index].ticket;
        }
        try {
            const auto grid = source->heights.Grid();
            if (source != lastSource) {
                mask = source->road ? MakeRoadMask(*source->road, grid) : RoadMask(grid);
                if (!source->cache) roadChunks = SplitRoad(source->heights, source->road);
                lastSource = source;
            }
            const auto key = grid.Key(index);
            ChunkPayload result;
            result.index = index;
            if (source->cache) {
                result.tile = source->cache->Find(key);
                result.cacheHit = static_cast<bool>(result.tile);
                if (!result.tile) {
                    result.tile = std::make_shared<const ChunkTile>(
                        ReadChunk(source->cache->File(source->directory, key), grid, key, result.io));
                    source->cache->Store(key, result.tile);
                }
            }
            const auto start = std::chrono::steady_clock::now();
            const auto& heights = result.tile ? result.tile->heights : source->heights;
            result.bounds = MeasureTerrainBounds(heights, key);
            result.vertices = MakeTerrainVertices(heights, key, !source->road);
            result.plants = GenerateVegetationChunk(heights, mask, key, source->plants);
            if (!result.tile) result.road = roadChunks[index];
            result.meshMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            {
                const std::lock_guard lock(mMutex);
                if (mSource == source) {
                    auto& slot = mSlots[index];
                    if (slot.state == State::Working && slot.ticket == ticket) {
                        slot = {State::Ready, std::move(result), ticket};
                    }
                }
            }
        } catch (...) {
            const std::lock_guard lock(mMutex);
            if (mSource == source && mSlots[index].ticket == ticket) mFailure = std::current_exception();
        }
        mChanged.notify_all();
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 取走可上传的最近块，移动结果向量，无需在两线程之间复制网格。
//————————————————————————————————————————————————————————————————————————————————
std::optional<ChunkPayload> ChunkStreamer::TakeReady() {
    const std::lock_guard lock(mMutex);
    if (mFailure) std::rethrow_exception(mFailure);
    for (const auto index : mPriority) {
        auto& slot = mSlots[index];
        if (slot.state == State::Delivered) continue;
        if (slot.state != State::Ready) return std::nullopt;
        auto result = std::move(slot.payload);
        slot = {State::Delivered, std::nullopt};
        mChanged.notify_all();
        return result;
    }
    return std::nullopt;
}

bool ChunkStreamer::Wants(std::size_t index) const {
    const std::lock_guard lock(mMutex);
    return index < mSlots.size() && mSlots[index].state != State::Empty;
}

ChunkQueueStats ChunkStreamer::Statistics() const {
    const std::lock_guard lock(mMutex);
    ChunkQueueStats result{mFocus, static_cast<unsigned>(mPriority.size())};
    for (const auto index : mPriority) {
        const auto state = mSlots[index].state;
        result.queued += state == State::Queued;
        result.working += state == State::Working;
        result.ready += state == State::Ready;
    }
    return result;
}

void ChunkStreamer::WaitForResult(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mMutex);
    mChanged.wait_for(lock, timeout, [&] {
        if (mFailure) return true;
        for (const auto index : mPriority) {
            const auto state = mSlots[index].state;
            if (state != State::Delivered) return state == State::Ready;
        }
        return true;
    });
}

}  // namespace tinyworld
