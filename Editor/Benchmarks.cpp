#include "Editor/Benchmarks.h"

#include "Editor/FrameCapture.h"
#include "Engine/Renderer/WebGpuContext.h"
#include "Engine/Scene/Capstone.h"
#include "Engine/Scene/SceneFile.h"

#include <glm/common.hpp>
#include <webgpu/wgpu.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <tuple>

namespace tinyworld {
namespace {

// 完成一帧的命令缓冲并提交到 GPU。
// 窗口呈现、回读映射均由各自调用方在提交后处理。
//————————————————————————————————————————————————————————————————————————————————
void SubmitFrame(WebGpuContext& gpu, WGPUCommandEncoder encoder) {
    CommandHandle commands(wgpuCommandEncoderFinish(encoder, nullptr));
    if (!commands) {
        throw std::runtime_error("Cannot finish the frame command buffer");
    }
    const auto submitted = commands.get();
    wgpuQueueSubmit(gpu.Queue(), 1, &submitted);
}

//————————————————————————————————————————————————————————————————————————————————
}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 按同一机位与分辨率离屏绘制。逐帧等待 GPU，以区分 CPU 提交和包含 GPU 的整帧耗时。
//————————————————————————————————————————————————————————————————————————————————
void BenchmarkVegetation(WebGpuContext& gpu, WorldDemo& world) {
    using Clock = std::chrono::steady_clock;
    using Milliseconds = std::chrono::duration<double, std::milli>;
    constexpr PixelSize size{1280, 720};
    constexpr int warmup = 24;
    constexpr int samples = 120;
    constexpr int middle = samples / 2;
    constexpr int p95 = (samples * 95 + 99) / 100 - 1;
    FrameCapture target(gpu, size);
    for (const bool instanced : {false, true}) {
        world.scene.vegetation->instanced = instanced;
        std::array<double, samples> submit{}, complete{};
        for (int frame = -warmup; frame < samples; ++frame) {
            const auto start = Clock::now();
            EncoderHandle encoder(wgpuDeviceCreateCommandEncoder(gpu.Device(), nullptr));
            if (!encoder) throw std::runtime_error("Cannot create benchmark command encoder.");
            world.renderer->Encode(encoder.get(), target.View(), size, world.scene);
            SubmitFrame(gpu, encoder.get());
            const auto queued = Clock::now();
            // 原生队列栅栏直接等 GPU，避免初始化轮询的毫秒级休眠进入测量。
            wgpuDevicePoll(gpu.Device(), WGPU_TRUE, nullptr);
            gpu.ProcessEvents();
            const auto finished = Clock::now();
            if (frame >= 0) {
                submit[frame] = Milliseconds(queued - start).count();
                complete[frame] = Milliseconds(finished - start).count();
            }
        }
        std::sort(submit.begin(), submit.end());
        std::sort(complete.begin(), complete.end());
        const auto& stats = world.renderer->VegetationStatistics();
        std::printf("%s | %ux%u | %d frames | grass %u tree %u rock %u | color draws %u shadow draws %u"
                    " | CPU p50 %.3f ms | complete p50 %.3f ms p95 %.3f ms\n",
                    instanced ? "Instanced" : "One draw per plant", size.width, size.height, samples,
                    stats.instances[0], stats.instances[1], stats.instances[2], stats.colorDraws, stats.shadowDraws,
                    (submit[middle - 1] + submit[middle]) * 0.5,
                    (complete[middle - 1] + complete[middle]) * 0.5, complete[p95]);
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 四个机位各测 24 帧。固定光照、水相位和输出尺寸，每帧等 GPU 完成再计整帧耗时。
//————————————————————————————————————————————————————————————————————————————————
void BenchmarkTerrain(WebGpuContext& gpu, WorldDemo& world) {
    using Clock = std::chrono::steady_clock;
    using Milliseconds = std::chrono::duration<double, std::milli>;
    constexpr PixelSize size{1280, 720};
    constexpr int samplesPerView = 24;
    constexpr int frames = samplesPerView * static_cast<int>(WorldScene::kTerrainRoute.size());
    FrameCapture target(gpu, size);
    for (const auto [name, lod, cull] : {
         std::tuple{"full", false, false}, std::tuple{"lod", true, false},
         std::tuple{"cull", false, true}, std::tuple{"lod+cull", true, true}}) {
        world.scene.terrainPreview = TerrainPreview{lod, cull};
        std::array<double, frames> elapsed{};
        unsigned blocks{}, color{}, shadow{};
        int sample = 0;
        for (const auto& pose : WorldScene::kTerrainRoute) {
            world.scene.camera = pose;
            for (int i = -8; i < samplesPerView; ++i) {
                const auto start = Clock::now();
                EncoderHandle encoder(wgpuDeviceCreateCommandEncoder(gpu.Device(), nullptr));
                if (!encoder) throw std::runtime_error("Cannot create terrain benchmark encoder.");
                world.renderer->Encode(encoder.get(), target.View(), size, world.scene);
                SubmitFrame(gpu, encoder.get());
                wgpuDevicePoll(gpu.Device(), WGPU_TRUE, nullptr);
                gpu.ProcessEvents();
                if (i < 0) continue;
                elapsed[sample++] = Milliseconds(Clock::now() - start).count();
                const auto& stats = world.renderer->TerrainStatistics();
                blocks += stats.visible;
                color += stats.colorTriangles;
                shadow += stats.shadowTriangles;
            }
        }
        std::sort(elapsed.begin(), elapsed.end());
        std::printf("%s | %d frames | visible %.2f | color triangles %.0f | shadow triangles %.0f"
                    " | p50 %.3f ms | p95 %.3f ms\n", name, frames,
                    double(blocks) / frames, double(color) / frames, double(shadow) / frames,
                    (elapsed[frames / 2 - 1] + elapsed[frames / 2]) * 0.5, elapsed[(frames * 95 + 99) / 100 - 1]);
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 走固定块键路线，测量实时编码使用的同一套请求、驻留和上传预算。
//————————————————————————————————————————————————————————————————————————————————
void BenchmarkStreaming(WebGpuContext& gpu, WorldDemo& world) {
    using Clock = std::chrono::steady_clock;
    constexpr PixelSize size{1280, 720};
    FrameCapture target(gpu, size);
    const auto frame = [&] {
        EncoderHandle encoder(wgpuDeviceCreateCommandEncoder(gpu.Device(), nullptr));
        if (!encoder) throw std::runtime_error("Cannot create streaming benchmark encoder.");
        world.renderer->Encode(encoder.get(), target.View(), size, world.scene);
        SubmitFrame(gpu, encoder.get());
        wgpuDevicePoll(gpu.Device(), WGPU_TRUE, nullptr);
        gpu.ProcessEvents();
        const auto& stats = world.renderer->StreamStatistics();
        if (stats.resident > ChunkStreamer::kMaxResident ||
            stats.uploaded > ChunkStreamer::kUploadsPerFrame ||
            stats.queue.queued + stats.queue.working + stats.queue.ready > ChunkStreamer::kMaxResident) {
            throw std::runtime_error("Streaming exceeded a frame or residency limit.");
        }
    };
    const auto visit = [&](const char* name, glm::ivec2 key, bool settle = true) {
        world.scene.camera = WorldScene::kTerrainRoute[1];
        const auto center = (glm::vec2(key) + 0.5f) * HeightField::kChunkSize;
        world.scene.camera.target.x = center.x;
        world.scene.camera.target.z = center.y;
        const auto started = Clock::now();
        frame();
        const auto& entered = world.renderer->StreamStatistics();
        const auto stillQueued = entered.queue.queued + entered.queue.working + entered.queue.ready;
        int frames = 1;
        while (settle && !world.renderer->StreamingSettled()) {
            if (Clock::now() - started > std::chrono::seconds(12)) {
                throw std::runtime_error("Chunk streaming route timed out.");
            }
            world.renderer->WaitForChunkData();
            frame();
            ++frames;
        }
        const auto& stats = world.renderer->StreamStatistics();
        std::printf("%s | key %d,%d | requested %u | pending after entry %u | resident %u | bytes %zu"
                    " | evicted total %u | %d frames | %.3f ms\n",
                    name, key.x, key.y, stats.queue.wanted, stillQueued, stats.resident, stats.residentBytes,
                    stats.evicted, frames, std::chrono::duration<double, std::milli>(Clock::now() - started).count());
    };
    visit("start", {-2, 1});
    visit("step 1", {-1, 0});
    visit("step 2", {0, -1});
    visit("step 3", {1, -1});
    visit("cape", {2, -2});
    visit("quick turn A", {-2, 1}, false);
    visit("quick turn B", {2, -2}, false);
    visit("return", {-2, 1});
    const auto& stats = world.renderer->StreamStatistics();
    if (stats.evicted == 0) throw std::runtime_error("Route did not unload any terrain.");
    std::printf("peak resident %u | peak upload %zu bytes/frame | limit %u chunks/frame\n",
                stats.peakResident, stats.peakUploadedBytes, ChunkStreamer::kUploadsPerFrame);
    std::printf("CPU resident %u | cache hits %u | Disk Read %.3f ms | Decompress %.3f ms"
                " | Mesh Build %.3f ms | GPU Upload %.3f ms\n",
                stats.cpuResident, stats.cacheHits, stats.io.diskMs, stats.io.decompressMs, stats.meshMs, stats.uploadMs);
}

//————————————————————————————————————————————————————————————————————————————————
// 在保存前和重开后用同一个步行控制器走完全程，并报告可复现的路线量。
//————————————————————————————————————————————————————————————————————————————————
void VerifyTour(const ApplicationOptions& options, const std::filesystem::path& root) {
    auto scene = CreateWorldScene(options, root);
    const auto original = WalkCapstone(scene);
    scene.SetWalking(false);
    SaveScene(options.tourVerificationPath, scene);
    LoadScene(options.tourVerificationPath, scene);
    const auto reopened = WalkCapstone(scene);
    if (original.end != reopened.end || original.distance != reopened.distance ||
        original.steps != reopened.steps || original.chunksVisited != reopened.chunksVisited) {
        throw std::runtime_error("The reopened scene produced a different walk.");
    }
    std::printf("Tour before and after reload | %.2f m | %u unique chunks | %u input steps"
                " | bridge yes | min water clearance %.3f m | finish %.2f,%.2f,%.2f\n",
                reopened.distance, reopened.chunksVisited, reopened.steps, reopened.minimumWaterClearance,
                reopened.end.x, reopened.end.y, reopened.end.z);
}

//————————————————————————————————————————————————————————————————————————————————
// 对比两份场景在共同范围内的固定相机轨迹。开场加载和预热不进入采样。
//————————————————————————————————————————————————————————————————————————————————
void BenchmarkCapstone(WebGpuContext& gpu, const std::filesystem::path& root) {
    using Clock = std::chrono::steady_clock;
    using Milliseconds = std::chrono::duration<double, std::milli>;
    constexpr PixelSize size{1280, 720};
    constexpr std::array targets{glm::vec3{-1.0f, 1.0f, 18.0f}, glm::vec3{0.0f, 1.0f, 7.7f},
                                 glm::vec3{0.0f, 1.0f, 0.0f}, glm::vec3{4.6f, 1.0f, -10.7f},
                                 glm::vec3{15.7f, 1.6f, -13.2f}};
    constexpr int framesPerLeg = 30;
    constexpr int frameCount = static_cast<int>((targets.size() - 1) * framesPerLeg);
    FrameCapture target(gpu, size);
    const auto sample = root / "worlds" / "valley";
    ApplicationOptions options;
    options.chapter = Chapter::Capstone;
    options.scenePath = sample;
    options.dayHour = 12.0f;
    options.waterTime = 0.0f;
    WorldDemo world(gpu, gpu.SurfaceFormat(), root, options);
    const auto frame = [&] {
        EncoderHandle encoder(wgpuDeviceCreateCommandEncoder(gpu.Device(), nullptr));
        if (!encoder) throw std::runtime_error("Cannot create tour benchmark encoder.");
        world.renderer->Encode(encoder.get(), target.View(), size, world.scene);
        SubmitFrame(gpu, encoder.get());
        wgpuDevicePoll(gpu.Device(), WGPU_TRUE, nullptr);
        gpu.ProcessEvents();
    };
    const auto warm = [&] {
        world.scene.camera = {targets.front(), -20.0f, 21.0f, 13.0f};
        const auto deadline = Clock::now() + std::chrono::seconds(12);
        frame();
        while (!world.renderer->StreamingSettled()) {
            if (Clock::now() >= deadline) throw std::runtime_error("Could not warm up the tour benchmark.");
            world.renderer->WaitForChunkData();
            frame();
        }
    };
    for (const bool large : {false, true}) {
        if (large) {
            LoadScene(root / "worlds" / "exploration", world.scene);
        }
        world.scene.dayCycle->hour = 12.0f;
        warm();
        for (int i = 0; i < 12; ++i) frame();
        std::array<double, frameCount> elapsed{};
        unsigned draws{}, peakDraws{}, live{}, peakLive{};
        std::size_t peakBytes{};
        for (int i = 0; i < frameCount; ++i) {
            const int leg = i / framesPerLeg;
            const float t = static_cast<float>(i % framesPerLeg + 1) / framesPerLeg;
            world.scene.camera.target = glm::mix(targets[leg], targets[leg + 1], t);
            const auto start = Clock::now();
            frame();
            elapsed[i] = Milliseconds(Clock::now() - start).count();
            const auto& stream = world.renderer->StreamStatistics();
            const auto calls = world.renderer->SceneDraws();
            draws += calls;
            live += stream.resident;
            peakDraws = std::max(peakDraws, calls);
            peakLive = std::max(peakLive, stream.resident);
            peakBytes = std::max(peakBytes, stream.residentBytes);
        }
        std::sort(elapsed.begin(), elapsed.end());
        std::printf("%s | %zu world chunks | %d frames | p50 %.3f ms | p95 %.3f ms"
                    " | draws mean %.1f peak %u | resident mean %.1f peak %u | peak %zu bytes\n",
                    large ? "Exploration 1024 m" : "Sample 48 m", world.scene.terrain->Grid().ChunkCount(), frameCount,
                    (elapsed[frameCount / 2 - 1] + elapsed[frameCount / 2]) * 0.5,
                    elapsed[(frameCount * 95 + 99) / 100 - 1], double(draws) / frameCount, peakDraws,
                    double(live) / frameCount, peakLive, peakBytes);
    }
    // 回切原样区，复用同一个 GPU 渲染器验证索引、边界和水面绑定。
    LoadScene(sample, world.scene);
    warm();
    if (world.scene.terrain->Grid() != TerrainGrid{} ||
        world.renderer->StreamStatistics().resident != 15u) {
        throw std::runtime_error("The renderer did not restore the small streaming grid.");
    }
    std::puts("GPU grid reload: 48 m -> 1024 m -> 48 m passed.");
}

}  // namespace tinyworld
