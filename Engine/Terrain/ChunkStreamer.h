#pragma once

#include "Engine/Terrain/ChunkCache.h"
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
// 同一个块键下的一次 CPU 交付。共享顶点和植被来自同一份只读源。
//————————————————————————————————————————————————————————————————————————————————
struct ChunkPayload {
    std::size_t index{};
    TerrainBounds bounds;
    std::vector<SceneVertex> vertices;  // 两档 LOD 共用的顶点
    VegetationPatch plants;
    std::vector<SceneVertex> road;  // 块内道路网格
    std::shared_ptr<const ChunkTile> tile; // 保存同块的权重与摆件，内存关卡为空
    ChunkReadTimes io;  // 本次磁盘读取与解压耗时
    double meshMs{};     // 地形及植被构建耗时
    bool cacheHit{};     // 本次直接取用 CPU 驻留块
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
    unsigned cpuResident{}; // CPU 缓存中的块数
    unsigned cacheHits{};   // 此场景已交付的缓存命中次数
    ChunkReadTimes io;      // 最近一次实际读盘和解压，毫秒
    double meshMs{};       // 最近一个已交付块的构建耗时，毫秒
    double uploadMs{};     // 本帧 GPU 上传调用的 CPU 墙钟耗时，毫秒
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
                const VegetationSettings& plants, const std::filesystem::path& directory = {},
                std::shared_ptr<ChunkCache> cache = {});

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
        std::optional<ChunkPayload> payload; // 已完成的 CPU 结果
        std::uint64_t ticket{};  // 过期工作不能发布到下一次对同一键的请求
    };
    struct Source {
        HeightField heights;
        std::optional<RoadSpline> road;
        VegetationSettings plants;  // 固定种子与生态设置
        std::filesystem::path directory; // 为空时从编辑器快照构建
        std::shared_ptr<ChunkCache> cache; // 磁盘场景的 CPU 块缓存
    };

    void Run(std::stop_token stop);
    [[nodiscard]] std::optional<std::size_t> FirstWithState(State state) const;

    mutable std::mutex mMutex;
    std::condition_variable_any mChanged;
    std::vector<Slot> mSlots; // 当前布局的块槽
    std::uint64_t mTicket{}; // 请求序号，视点折返时保持递增
    std::vector<std::size_t> mPriority; // 想要的键，按离视点的块距离排序
    std::shared_ptr<const Source> mSource;
    std::exception_ptr mFailure;
    glm::ivec2 mFocus{}; // 当前相机或角色的需求中心
    std::optional<glm::ivec2> mUrgent; // 优先补齐的碰撞块
    std::jthread mWorker; // 必须最后声明，析构时先停止并加入工作线程
};

}  // namespace tinyworld
