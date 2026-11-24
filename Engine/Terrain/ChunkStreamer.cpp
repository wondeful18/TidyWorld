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
    std::array<std::vector<SceneVertex>, HeightField::kChunkCount> chunks;
    if (!road) return chunks;
    const auto mesh = MakeRoadMesh(field, *road);
    for (std::size_t i = 0; i < mesh.size(); i += 3) {
        glm::vec2 center{0.0f};
        for (std::size_t j = i; j < i + 3; ++j) {
            center += glm::vec2{mesh[j].position[0], mesh[j].position[2]};
        }
        auto& chunk = chunks[TerrainIndex(HeightField::ChunkAt(center / 3.0f))];
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
                           const VegetationSettings& plants) {
    const std::lock_guard lock(mMutex);
    const bool changed = !mSource || mSource->heights.Revision() != heights.Revision() ||
                         mSource->road != road || mSource->plants.mode != plants.mode ||
                         mSource->plants.seed != plants.seed || mSource->plants.density != plants.density ||
                         mSource->plants.minPlantHeight != plants.minPlantHeight;
    if (!changed && focus == mFocus) return;
    if (changed) mSource = std::make_shared<const Source>(Source{heights, road, plants});
    mFocus = focus;
    mPriority.clear();
    for (std::size_t index = 0; index < mSlots.size(); ++index) {
        const auto delta = TerrainKey(index) - focus;
        auto& slot = mSlots[index];
        if (std::max(std::abs(delta.x), std::abs(delta.y)) > kRadius) {
            slot = {};
            continue;
        }
        if (changed || slot.state == State::Empty) slot = {State::Queued, std::nullopt};
        mPriority.push_back(index);
    }
    std::stable_sort(mPriority.begin(), mPriority.end(), [focus](std::size_t a, std::size_t b) {
        const auto distance = [focus](std::size_t index) {
            const auto d = TerrainKey(index) - focus;
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
    std::array<std::vector<SceneVertex>, HeightField::kChunkCount> roadChunks;
    try {
        while (!stop.stop_requested()) {
            std::shared_ptr<const Source> source;
            std::size_t index{};
            {
                std::unique_lock lock(mMutex);
                if (!mChanged.wait(lock, stop, [&] { return FirstWithState(State::Queued).has_value(); })) return;
                if (stop.stop_requested()) return;
                index = *FirstWithState(State::Queued);
                mSlots[index].state = State::Working;
                source = mSource;
            }
            if (source != lastSource) {
                mask = source->road ? MakeRoadMask(*source->road) : RoadMask{};
                roadChunks = SplitRoad(source->heights, source->road);
                lastSource = source;
            }
            const auto key = TerrainKey(index);
            const auto& heights = source->heights;
            const bool paintTrail = !source->road;
            ChunkPayload result{index, MeasureTerrainBounds(heights, key),
                                MakeTerrainChunk(heights, key, paintTrail),
                                MakeTerrainChunk(heights, key, paintTrail, true),
                                GenerateVegetationChunk(heights, mask, key, source->plants), roadChunks[index]};
            {
                const std::lock_guard lock(mMutex);
                auto& slot = mSlots[index];
                if (mSource == source && slot.state == State::Working) {
                    slot = {State::Ready, std::move(result)};
                }
            }
            mChanged.notify_all();
        }
    } catch (...) {
        const std::lock_guard lock(mMutex);
        mFailure = std::current_exception();
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
        return result;
    }
    return std::nullopt;
}

bool ChunkStreamer::Wants(std::size_t index) const {
    const std::lock_guard lock(mMutex);
    return mSlots[index].state != State::Empty;
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
