#pragma once

#include "Engine/Terrain/TerrainVisibility.h"
#include "Engine/Terrain/Vegetation.h"

#include <chrono>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 同一个块键下的一次 CPU 交付。两档地形和植被来自同一份只读源。
//————————————————————————————————————————————————————————————————————————————————
struct ChunkPayload {
    std::size_t index{};
    TerrainBounds bounds;
    std::vector<SceneVertex> fine;
    std::vector<SceneVertex> coarse;
    VegetationPatch plants;
    std::vector<SceneVertex> road;
};

struct ChunkQueueStats {
    glm::ivec2 focus{};
    unsigned wanted{};
    unsigned queued{};
    unsigned working{};
    unsigned ready{};
};

struct TerrainStreamStats {
    ChunkQueueStats queue;
    unsigned resident{};
    unsigned peakResident{};
    unsigned uploaded{};
    unsigned evicted{};
    std::size_t residentBytes{};
    std::size_t uploadedBytes{};
    std::size_t peakUploadedBytes{};
};

//————————————————————————————————————————————————————————————————————————————————
// 一个 CPU 工作者和一份按当前视点重建的优先顺序。所有 GPU 资源归调用者。
//————————————————————————————————————————————————————————————————————————————————
class ChunkStreamer {
public:
    static constexpr int kRadius = 2; // 裁到样区边界的 5×5 邻域
    static constexpr unsigned kMaxResident = (2 * kRadius + 1) * (2 * kRadius + 1);
    static constexpr unsigned kUploadsPerFrame = 2;

    ChunkStreamer();

    //————————————————————————————————————————————————————————————————————————————————
    // 更新需求和只读源。仅当高度、种植参数或道路变化时复制源数据。
    //————————————————————————————————————————————————————————————————————————————————
    void Update(glm::ivec2 focus, const HeightField& heights, const std::optional<RoadSpline>& road,
                const VegetationSettings& plants);

    //————————————————————————————————————————————————————————————————————————————————
    // 渲染线程取走当前最优先的 CPU 结果。返回空时可先保留已有 GPU 块。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] std::optional<ChunkPayload> TakeReady();

    [[nodiscard]] bool Wants(std::size_t index) const;
    [[nodiscard]] ChunkQueueStats Statistics() const;

    //————————————————————————————————————————————————————————————————————————————————
    // 离屏诊断可等待 CPU 结果，实时帧从不调用此方法。
    //————————————————————————————————————————————————————————————————————————————————
    void WaitForResult(std::chrono::milliseconds timeout);

private:
    enum class State { Empty, Queued, Working, Ready, Delivered };
    struct Slot {
        State state{State::Empty};
        std::optional<ChunkPayload> payload;
    };
    struct Source {
        HeightField heights;
        std::optional<RoadSpline> road;
        VegetationSettings plants;
    };

    void Run(std::stop_token stop);
    [[nodiscard]] std::optional<std::size_t> FirstWithState(State state) const;

    mutable std::mutex mMutex;
    std::condition_variable_any mChanged;
    std::array<Slot, HeightField::kChunkCount> mSlots;
    std::vector<std::size_t> mPriority; // 想要的键，按离视点的块距离排序
    std::shared_ptr<const Source> mSource;
    std::exception_ptr mFailure;
    glm::ivec2 mFocus{};
    std::jthread mWorker; // 必须最后声明，析构时先停止并加入工作线程
};

}  // namespace tinyworld
