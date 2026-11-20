#include "Editor/Application.h"

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

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <chrono>
#include <optional>
#include <stdexcept>
#include <variant>

namespace tinyworld {
namespace {

//————————————————————————————————————————————————————————————————————————————————
// 将第二章的 CPU 场景、GPU 资源和面板绑定到同一个局部生命周期。
//————————————————————————————————————————————————————————————————————————————————
struct WorldDemo {
    WorldScene scene;       // 编辑器与渲染器共同使用的场景
    WorldRenderer renderer; // 三个模型与深度绘制管线
    WorldEditor editor;    // 借用本对象的场景并接入 ImGui

    //————————————————————————————————————————————————————————————————————————————————
    // 上传网格，再按命令行选项设置初始布局和灯光。
    //————————————————————————————————————————————————————————————————————————————————
    WorldDemo(WebGpuContext& gpu, WGPUTextureFormat format,
              const std::filesystem::path& root, WorldPreset preset)
        : renderer(gpu, format, scene, root), editor(scene) {
        scene.ApplyPreset(preset);
    }
};

using Demo = std::variant<TriangleRenderer, WorldDemo>;

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

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 按命令行选择章节，并以固定的执行顺序驱动两条绘制路径。
// 逆序析构确保编辑器和 GPU 对象都早于窗口释放。
//————————————————————————————————————————————————————————————————————————————————
int RunApplication(const ApplicationOptions& options) {
    Log log;
    const bool isWorld = options.chapter == Chapter::World;
    const char* title = isWorld ? "TinyWorld ch02 - Cottage, Tree and Sun"
        : options.direct ? "TinyWorld ch01 - Direct Triangle" : "TinyWorld ch01 - Editor";
    Window window(title, options.capturePath.empty());
    log.Write("APP", "GLFW window ready: 1280 x 720 logical units");
    WebGpuContext gpu(window, log);
    const auto format = options.direct ? gpu.SurfaceFormat() : SceneViewport::kColorFormat;
    const auto root = ExecutableDirectory();
    Demo demo = isWorld
        ? Demo(std::in_place_type<WorldDemo>, gpu, format, root, options.worldPreset)
        : Demo(std::in_place_type<TriangleRenderer>, gpu, format, root / "shaders" / "triangle.wgsl");
    log.Write("GPU", isWorld ? "Chapter 02 meshes, Lambert pipeline and depth state ready"
                             : "WGSL and triangle pipeline validated");

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
