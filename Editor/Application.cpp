#include "Editor/Application.h"

#include "Engine/Scene/DayCycle.h"

#include "Editor/EditorUi.h"
#include "Editor/FrameCapture.h"
#include "Editor/SmokeTest.h"
#include "Editor/WorldEditor.h"
#include "Engine/Core/Log.h"
#include "Engine/Platform/ExecutablePath.h"
#include "Engine/Platform/Window.h"
#include "Engine/Renderer/TriangleRenderer.h"
#include "Engine/Renderer/WebGpuContext.h"
#include "Engine/Renderer/WorldRenderer.h"
#include "Engine/Scene/SceneFile.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <webgpu/wgpu.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <variant>

namespace tinyworld {
namespace {

//————————————————————————————————————————————————————————————————————————————————
// 复用小屋与小车预设，并在创建 GPU 资源前确定本章是否提供阴影。
//————————————————————————————————————————————————————————————————————————————————
WorldScene CreateWorldScene(const ApplicationOptions& options, const std::filesystem::path& root) {
    WorldScene scene;
    const bool terrainChapter = options.chapter >= Chapter::Terrain;
    const bool walkChapter = options.chapter == Chapter::Walk || terrainChapter;
    const bool hasSky = options.chapter == Chapter::Sky || walkChapter;
    if (options.chapter == Chapter::SunShadows || hasSky) {
        scene.shadows = options.shadowOverrides.value_or(ShadowSettings{});
        if (terrainChapter && !options.shadowOverrides) {
            // 对扩大的太阳投影实测偏移，在平缓地表抑制自阴影条纹。
            scene.shadows->depthBias = 0.0012f;
        }
    }
    if (hasSky) {
        scene.environment = options.environmentOverrides.value_or(EnvironmentSettings{});
    }
    if (walkChapter) {
        scene.spawn.emplace();
    }
    if (terrainChapter) {
        scene.terrain.emplace();
        scene.canPreviewLod = options.chapter == Chapter::Terrain;
        scene.previewLod = options.lodPreview;
        scene.canSculpt = options.chapter >= Chapter::Sculpt;
        scene.showChunkBorders = options.chunkBorders;
    }
    auto scenePath = options.scenePath;
    if (options.chapter >= Chapter::Materials) {
        scene.materials.emplace();
        scene.paint.emplace();
        if (scenePath.empty() && options.worldPreset != WorldPreset::Overlap && options.worldPreset != WorldPreset::Evening) {
            scenePath = root / "scenes" / "ch07" / "valley.json";
        }
    }
    if (options.chapter >= Chapter::Road) {
        scene.road.emplace();
    }
    scene.canPlaceProps = options.chapter >= Chapter::Props;
    scene.canUseGltfProps = options.chapter >= Chapter::Village;
    if (scene.canUseGltfProps && options.scenePath.empty()) {
        scenePath = options.chapter >= Chapter::Coast ? root / "scenes" / "ch15" / "coast.json"
                                                       : root / "scenes" / "ch13" / "village.json";
    }
    if (options.chapter >= Chapter::Meadow) {
        scene.vegetation.emplace();
        if (options.chapter >= Chapter::Ecology) scene.vegetation->mode = VegetationMode::TerrainRules;
    }
    if (options.chapter >= Chapter::Water) {
        scene.water.emplace();
        scene.water->coast = options.chapter >= Chapter::Coast;
        scene.water->view = options.waterView.value_or(WaterView::Lit);
        scene.water->seconds = options.waterTime.value_or(0.0f);
    }
    if (options.chapter >= Chapter::Atmosphere) {
        scene.atmosphere.emplace();
        if (options.fogEnabled) scene.atmosphere->fogEnabled = *options.fogEnabled;
        if (options.shadowDistance) scene.atmosphere->shadowDistance = *options.shadowDistance;
    }
    if (options.chapter >= Chapter::Lightmap) {
        scene.lightmapEnabled = options.lightmapEnabled.value_or(true);
        if (options.scenePath.empty()) scenePath = root / "scenes" / "ch17" / "porch.json";
    }
    if (options.chapter >= Chapter::DayCycle) scene.dayCycle.emplace();
    if (scenePath.empty()) {
        scene.ApplyPreset(options.worldPreset);
    } else {
        LoadScene(scenePath, scene);
        if (scene.vegetation && options.scenePath.empty() && !scene.canUseGltfProps) {
            scene.camera = {{0.0f, 0.5f, 2.0f}, 13.0f, 30.0f, 38.0f};
        }
        if (options.shadowOverrides) {
            scene.shadows = options.shadowOverrides;
        }
        if (options.environmentOverrides) {
            scene.environment = options.environmentOverrides;
        }
        if (scene.dayCycle && options.scenePath.empty() && options.worldPreset == WorldPreset::Default) {
            scene.camera = WorldScene::kDayCamera;
        }
        if (options.worldPreset == WorldPreset::Porch) scene.camera = WorldScene::kPorchCamera;
        if (options.worldPreset == WorldPreset::Shore) scene.camera = WorldScene::kShoreCamera;
        scene.SetDriving(options.worldPreset == WorldPreset::Drive);
        scene.SetWalking(options.worldPreset == WorldPreset::Walk);
        if (scene.materials) {
            scene.materials->spheres = options.worldPreset == WorldPreset::Materials;
        }
    }
    if (scene.dayCycle && options.dayHour) scene.dayCycle->hour = WrapDayHour(*options.dayHour);
    return scene;
}

//————————————————————————————————————————————————————————————————————————————————
// 将小屋演示的 CPU 场景、GPU 资源和面板绑定到同一个局部生命周期。
//————————————————————————————————————————————————————————————————————————————————
struct WorldDemo {
    WorldScene scene;       // 编辑器与渲染器共同使用的场景
    WorldRenderer renderer; // 场景模型与深度绘制管线
    WorldEditor editor;    // 借用本对象的场景并接入 ImGui

    //————————————————————————————————————————————————————————————————————————————————
    // 按初始场景设置上传网格，并将同一场景交由编辑器和渲染器使用。
    //————————————————————————————————————————————————————————————————————————————————
    WorldDemo(WebGpuContext& gpu, WGPUTextureFormat format,
              const std::filesystem::path& root, const ApplicationOptions& options)
        : scene(CreateWorldScene(options, root)), renderer(gpu, format, scene, root),
          editor(scene, options.scenePath, options.sculptTools || options.chapter == Chapter::Sculpt,
                 renderer.PaintPreviews(), &renderer.VegetationStatistics()) {}

    //————————————————————————————————————————————————————————————————————————————————
    // 禁用复制和隐式移动，使 editor 对 scene 的借用始终指向本实例。
    //————————————————————————————————————————————————————————————————————————————————
    WorldDemo(const WorldDemo&) = delete;

    //————————————————————————————————————————————————————————————————————————————————
    // 场景通过预设原地更新，整套演示对象在应用中保持固定地址。
    //————————————————————————————————————————————————————————————————————————————————
    WorldDemo& operator=(const WorldDemo&) = delete;
};

using Demo = std::variant<TriangleRenderer, WorldDemo>;

//————————————————————————————————————————————————————————————————————————————————
// 直接在返回的 variant 中构造选中的章节，避免搬动含有场景引用的演示。
//————————————————————————————————————————————————————————————————————————————————
Demo CreateDemo(WebGpuContext& gpu, WGPUTextureFormat format, const std::filesystem::path& root,
                const ApplicationOptions& options) {
    if (options.chapter != Chapter::Triangle) {
        return Demo(std::in_place_type<WorldDemo>, gpu, format, root, options);
    }
    return Demo(std::in_place_type<TriangleRenderer>, gpu, format, root / "shaders" / "triangle.wgsl");
}

//————————————————————————————————————————————————————————————————————————————————
// 直接呈现没有 ImGui，从获得系统焦点的 GLFW 窗口读取驾驶按键。
//————————————————————————————————————————————————————————————————————————————————
CarInput ReadDirectRoamInput(GLFWwindow* window) {
    if (!glfwGetWindowAttrib(window, GLFW_FOCUSED)) {
        return {};
    }
    const bool forward = glfwGetKey(window, GLFW_KEY_W) || glfwGetKey(window, GLFW_KEY_UP);
    const bool reverse = glfwGetKey(window, GLFW_KEY_S) || glfwGetKey(window, GLFW_KEY_DOWN);
    const bool left = glfwGetKey(window, GLFW_KEY_A) || glfwGetKey(window, GLFW_KEY_LEFT);
    const bool right = glfwGetKey(window, GLFW_KEY_D) || glfwGetKey(window, GLFW_KEY_RIGHT);
    return {
        static_cast<float>(forward - reverse),
        static_cast<float>(left - right),
        glfwGetKey(window, GLFW_KEY_R) || glfwGetKey(window, GLFW_KEY_HOME),
    };
}

//————————————————————————————————————————————————————————————————————————————————
// 将同一个颜色目标和尺寸交给已选择的章节渲染器。
//————————————————————————————————————————————————————————————————————————————————
void EncodeScene(Demo& demo, WGPUCommandEncoder encoder, WGPUTextureView target, PixelSize size) {
    if (auto* world = std::get_if<WorldDemo>(&demo)) {
        world->renderer.Encode(encoder, target, size, world->scene);
    } else {
        std::get<TriangleRenderer>(demo).Encode(encoder, target);
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 编码正常的场景通道与可选 UI 通道。
// output 可指向已获取的 Surface 视图，也可指向诊断捕获目标。
//————————————————————————————————————————————————————————————————————————————————
void EncodeFrame(WGPUCommandEncoder encoder, WGPUTextureView output, Demo& demo,
                 EditorUi* editor, const Log& log, PixelSize framebuffer, double frameIntervalMs) {
    if (editor) {
        auto* world = std::get_if<WorldDemo>(&demo);
        editor->BuildFrame(log, framebuffer, frameIntervalMs, world ? &world->editor : nullptr);
        if (const auto scene = editor->SceneTarget()) {
            // 先写完离屏纹理，再让窗口通道读取它，两次使用分别处于独立通道。
            EncodeScene(demo, encoder, scene, editor->SceneSize());
        }
        editor->Encode(encoder, output);
    } else {
        EncodeScene(demo, encoder, output, framebuffer);
    }
}

//————————————————————————————————————————————————————————————————————————————————
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
// 编码并提交一帧，窗口资源由本函数持有到呈现完成。
// editor 为空时将场景直接写入 Surface。
//————————————————————————————————————————————————————————————————————————————————
bool RenderFrame(WebGpuContext& gpu, Demo& demo, EditorUi* editor,
                 const Log& log, PixelSize framebuffer, double frameIntervalMs) {
    auto texture = gpu.AcquireFrame(framebuffer);
    if (!texture) {
        return false;
    }
    TextureViewHandle surfaceView(wgpuTextureCreateView(texture.get(), nullptr));
    EncoderHandle encoder(wgpuDeviceCreateCommandEncoder(gpu.Device(), nullptr));
    if (!surfaceView || !encoder) {
        throw std::runtime_error("Cannot create frame view or command encoder");
    }

    EncodeFrame(encoder.get(), surfaceView.get(), demo, editor, log, framebuffer, frameIntervalMs);
    SubmitFrame(gpu, encoder.get());
    gpu.Present();
    return true;
}

//————————————————————————————————————————————————————————————————————————————————
// 复用正常绘制通道，并在最后一帧追加回读命令。
// 连续生成几个帧，让 ImGui 完成首轮停靠布局和字体纹理创建。
//————————————————————————————————————————————————————————————————————————————————
void CaptureFrames(WebGpuContext& gpu, Demo& demo, EditorUi* editor,
                   Log& log, PixelSize size, const std::filesystem::path& path) {
    if (size.width == 0 || size.height == 0) {
        throw std::runtime_error("Capture requires a nonzero GLFW framebuffer");
    }
    log.Write("CAPTURE", "GPU readback output; no Surface present");
    FrameCapture capture(gpu, size);
    for (int frame = 0; frame < 3; ++frame) {
        glfwPollEvents();
        gpu.ProcessEvents();
        EncoderHandle encoder(wgpuDeviceCreateCommandEncoder(gpu.Device(), nullptr));
        if (!encoder) {
            throw std::runtime_error("Cannot create capture command encoder");
        }
        EncodeFrame(encoder.get(), capture.View(), demo, editor, log, size, 0.0);
        if (frame == 2) {
            capture.EncodeCopy(encoder.get());
        }
        SubmitFrame(gpu, encoder.get());
    }
    capture.Write(gpu, path);
    gpu.WaitForSubmittedWork();
    log.Write("CAPTURE", "wrote " + path.string());
}

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
            world.renderer.Encode(encoder.get(), target.View(), size, world.scene);
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
        const auto& stats = world.renderer.VegetationStatistics();
        std::printf("%s | %ux%u | %d frames | grass %u tree %u rock %u | color draws %u shadow draws %u"
                    " | CPU p50 %.3f ms | complete p50 %.3f ms p95 %.3f ms\n",
                    instanced ? "Instanced" : "One draw per plant", size.width, size.height, samples,
                    stats.instances[0], stats.instances[1], stats.instances[2], stats.colorDraws, stats.shadowDraws,
                    (submit[middle - 1] + submit[middle]) * 0.5,
                    (complete[middle - 1] + complete[middle]) * 0.5, complete[p95]);
    }
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 按命令行选择章节，并以固定的执行顺序驱动两条绘制路径。
// 逆序析构确保编辑器和 GPU 对象都早于窗口释放。
//————————————————————————————————————————————————————————————————————————————————
int RunApplication(const ApplicationOptions& options) {
    Log log;
    const auto root = ExecutableDirectory();
    if (!options.roadMaskPath.empty()) {
        const auto scene = CreateWorldScene(options, root);
        ExportRoadMask(options.roadMaskPath, scene.road.value());
        log.Write("ROAD", "wrote " + options.roadMaskPath.string());
        return 0;
    }
    const bool isWorld = options.chapter != Chapter::Triangle;
    const char* title = options.direct ? "TinyWorld ch01 - Direct Triangle" : "TinyWorld ch01 - Editor";
    const char* ready = "WGSL and triangle pipeline validated";
    if (options.chapter == Chapter::World) {
        title = "TinyWorld ch02 - Cottage, Tree and Sun";
        ready = "Chapter 02 meshes, Lambert pipeline and depth state ready";
    } else if (options.chapter == Chapter::SunShadows) {
        title = "TinyWorld ch03 - Sunlight and Shadows";
        ready = "Chapter 03 meshes, sun depth and world color pipelines ready";
    } else if (options.chapter == Chapter::Sky) {
        title = "TinyWorld ch04 - Sky and Environment";
        ready = "Chapter 04 cubemap, panorama, sun depth and world pipelines ready";
    } else if (options.chapter == Chapter::Walk) {
        title = "TinyWorld ch05 - Scene Files and Walking";
        ready = "Chapter 05 scene and third-person world pipelines ready";
    } else if (options.chapter == Chapter::Terrain) {
        title = "TinyWorld ch06 - Heightfield and Robot Walk";
        ready = "Chapter 06 heightfield chunks and robot ready";
    } else if (options.chapter == Chapter::Sculpt) {
        title = "TinyWorld ch07 - Sculpt a Valley";
        ready = "Chapter 07 sculpting and version 2 scenes ready";
    } else if (options.chapter == Chapter::Materials) {
        title = "TinyWorld ch08 - Terrain Materials";
        ready = "Chapter 08 PBR terrain and material samples ready";
    } else if (options.chapter == Chapter::Road) {
        title = "TinyWorld ch09 - Spline Roads";
        ready = "Chapter 09 spline road and planting mask ready";
    } else if (options.chapter == Chapter::Props) {
        title = "TinyWorld ch10 - Place Scene Objects";
        ready = "Chapter 10 prop placement and transform gizmos ready";
    } else if (options.chapter == Chapter::Meadow) {
        title = "TinyWorld ch11 - Grass, Treeline and Instancing";
        ready = "Chapter 11 grass cards, trees and instancing ready";
    } else if (options.chapter == Chapter::Ecology) {
        title = "TinyWorld ch12 - Terrain Ecology";
        ready = "Chapter 12 seeded terrain vegetation and road exclusion ready";
    } else if (options.chapter == Chapter::Village) {
        title = "TinyWorld ch13 - Village Assets and Footbridge";
        ready = "Chapter 13 glTF meshes, PBR textures and bridge walking ready";
    } else if (options.chapter == Chapter::Water) {
        title = "TinyWorld ch14 - Lake, Stream and Shore";
        ready = "Chapter 14 opaque scene depth and water pipeline ready";
    } else if (options.chapter == Chapter::Coast) {
        title = "TinyWorld ch15 - Estuary and Coast";
        ready = "Chapter 15 coastal terrain, ocean waves and shore route ready";
    } else if (options.chapter == Chapter::Atmosphere) {
        title = "TinyWorld ch16 - Sun, Shadows and Distance Fog";
        ready = "Chapter 16 shared sky color, shadow distance and linear fog ready";
    } else if (options.chapter == Chapter::Lightmap) {
        title = "TinyWorld ch17 - Bake a Lightmap";
        ready = "Chapter 17 CPU-baked porch lightmap and UV2 ready";
    } else if (options.chapter == Chapter::DayCycle) {
        title = "TinyWorld ch18 - Day and Night";
        ready = "Chapter 18 shared day-night sky, reflections and solar shadows ready";
    }
    Window window(title, options.capturePath.empty() && !options.benchmarkVegetation);
    log.Write("APP", "GLFW window ready: 1280 x 720 logical units");
    WebGpuContext gpu(window, log);
    const auto format = options.direct || options.benchmarkVegetation ? gpu.SurfaceFormat() : SceneViewport::kColorFormat;
    Demo demo = CreateDemo(gpu, format, root, options);
    log.Write("GPU", ready);
    if (options.benchmarkVegetation) {
        BenchmarkVegetation(gpu, std::get<WorldDemo>(demo));
        return 0;
    }

    std::optional<EditorUi> editor;
    if (!options.direct) {
        editor.emplace(window, gpu);
    }
    if (!options.capturePath.empty()) {
        CaptureFrames(gpu, demo, editor ? &*editor : nullptr, log, window.FramebufferPixels(), options.capturePath);
        return 0;
    }
    std::optional<SmokeTest> smoke;
    if (options.smokeTest) {
        smoke.emplace(options.direct);
    }
    log.Write("APP", options.direct ? (isWorld ? "direct world path" : "direct triangle path")
                                    : "offscreen scene + ImGui path");

    using Clock = std::chrono::steady_clock;
    auto previous = Clock::now();
    bool awaitingFirstFrame = true;
    while (!glfwWindowShouldClose(window.Native())) {
        glfwPollEvents();
        gpu.ProcessEvents();
        const auto now = Clock::now();
        const double frameIntervalMs = std::chrono::duration<double, std::milli>(now - previous).count();
        previous = now;
        const auto framebuffer = window.FramebufferPixels();

        bool presented = false;
        if (framebuffer.width != 0 && framebuffer.height != 0) {
            if (auto* world = std::get_if<WorldDemo>(&demo)) {
                auto& scene = world->scene;
                const double seconds = frameIntervalMs / 1000.0;
                if (scene.dayCycle && scene.dayCycle->playing) {
                    const float hours = static_cast<float>(std::min(seconds, 0.1)) * 0.75f;
                    scene.dayCycle->hour = WrapDayHour(scene.dayCycle->hour + hours);
                }
                if (scene.water) {
                    scene.water->seconds = std::fmod(scene.water->seconds + static_cast<float>(std::min(seconds, 0.1)),
                                                     WaterSettings::kLoopSeconds);
                }
                if (options.direct && scene.IsWalking()) {
                    const auto keys = ReadDirectRoamInput(window.Native());
                    scene.UpdateWalk({keys.throttle, keys.steering, keys.reset}, seconds);
                } else if (options.direct && scene.IsDriving()) {
                    scene.UpdateDrive(ReadDirectRoamInput(window.Native()), static_cast<float>(seconds));
                }
            }
            presented = RenderFrame(gpu, demo, editor ? &*editor : nullptr, log, framebuffer, frameIntervalMs);
        }
        if (!presented) {
            // 最小化、遮挡或暂时取不到纹理时等待事件，避免忙等。
            glfwWaitEventsTimeout(0.03);
        }
        if (awaitingFirstFrame && presented) {
            awaitingFirstFrame = false;
            log.Write("FRAME", "first frame presented");
        }
        if (smoke && smoke->Observe(window, log, framebuffer, editor ? editor->SceneSize() : PixelSize{}, presented)) {
            gpu.WaitForSubmittedWork();
            log.Write("TEST", "PASS: resize, minimize, restore and GPU completion");
            return 0;
        }
        if (glfwGetKey(window.Native(), GLFW_KEY_ESCAPE) == GLFW_PRESS &&
            (!editor || editor->AllowsSceneKeyboard())) {
            glfwSetWindowShouldClose(window.Native(), GLFW_TRUE);
        }
    }
    if (smoke) {
        throw std::runtime_error("Window closed before the smoke test completed");
    }
    gpu.WaitForSubmittedWork();
    log.Write("APP", "normal shutdown");
    return 0;
}

}  // namespace tinyworld
