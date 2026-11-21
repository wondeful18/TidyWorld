#include "Editor/EditorUi.h"

#include "Editor/WorldEditor.h"

#include "Engine/Core/Log.h"
#include "Engine/Renderer/RenderPass.h"
#include "Engine/Renderer/WebGpuContext.h"

#include <imgui.h>
#include <ImGuizmo.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_wgpu.h>

#include <stdexcept>

namespace tinyworld {
namespace {

//————————————————————————————————————————————————————————————————————————————————
// 在首次绘制时构建方便观察的停靠布局。
// 主场景在中央，左侧用于对象信息，底部用于日志与统计。
//————————————————————————————————————————————————————————————————————————————————
void BuildDockSpace() {
    constexpr ImGuiID dock = 0x54494E59;
    if (!ImGui::DockBuilderGetNode(dock)) {
        ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dock, ImGui::GetMainViewport()->WorkSize);
        ImGuiID center = dock;
        ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.22f, nullptr, &center);
        const auto bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.25f, nullptr, &center);
        const auto inspector = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.60f, nullptr, &left);
        ImGuiID console = bottom;
        const auto statistics = ImGui::DockBuilderSplitNode(console, ImGuiDir_Right, 0.33f, nullptr, &console);
        ImGui::DockBuilderDockWindow("Scene Viewport", center);
        ImGui::DockBuilderDockWindow("Hierarchy", left);
        ImGui::DockBuilderDockWindow("Inspector", inspector);
        ImGui::DockBuilderDockWindow("Console", console);
        ImGui::DockBuilderDockWindow("Statistics", statistics);
        ImGui::DockBuilderFinish(dock);
    }
    ImGui::DockSpaceOverViewport(dock);
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 启用单窗口 Docking 并依次启动 GLFW 与 WebGPU 后端。
// 若后一个启动失败，先清理已经启动的前一个后端。
//————————————————————————————————————————————————————————————————————————————————
EditorUi::EditorUi(Window& window, WebGpuContext& gpu) : mWindow(window), mDevice(gpu.Device()) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.FrameRounding = 4.0f;
    style.Colors[ImGuiCol_WindowBg] = {0.075f, 0.096f, 0.13f, 1.0f};
    style.Colors[ImGuiCol_TabSelected] = {0.14f, 0.28f, 0.34f, 1.0f};
    style.Colors[ImGuiCol_DockingPreview] = {0.21f, 0.66f, 0.65f, 0.7f};

    if (!ImGui_ImplGlfw_InitForOther(window.Native(), true)) {
        ImGui::DestroyContext();
        throw std::runtime_error("Cannot initialize ImGui GLFW backend");
    }
    ImGui_ImplWGPU_InitInfo init;
    init.Device = mDevice;
    init.RenderTargetFormat = gpu.SurfaceFormat();
    if (!ImGui_ImplWGPU_Init(&init)) {
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        throw std::runtime_error("Cannot initialize ImGui WebGPU backend");
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 断开 WebGPU 与 GLFW 后端，再销毁 ImGui 上下文。
// SceneViewport 的 GPU 资源随后由其成员析构释放。
//————————————————————————————————————————————————————————————————————————————————
EditorUi::~EditorUi() {
    ImGui_ImplWGPU_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

//————————————————————————————————————————————————————————————————————————————————
// 更新输入、测量离屏视口并生成整个编辑器的绘制列表。
// 统计面板标明帧间隔、离屏尺寸和 GLFW 原始输入坐标。
//————————————————————————————————————————————————————————————————————————————————
void EditorUi::BuildFrame(const Log& log, PixelSize framebuffer, double frameIntervalMs, WorldEditor* world) {
    ImGui_ImplWGPU_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    if (world) {
        ImGuizmo::BeginFrame();
    }
    BuildDockSpace();
    if (!world) {
        mScene.Build(mDevice);
    }

    if (ImGui::Begin("Hierarchy")) {
        if (world) {
            world->DrawHierarchy();
        } else {
            ImGui::TextDisabled("Scene objects arrive in Chapter 02.");
        }
    }
    ImGui::End();
    if (ImGui::Begin("Inspector")) {
        if (world) {
            world->DrawInspector();
        } else {
            ImGui::TextDisabled("No scene object selected.");
        }
    }
    ImGui::End();
    if (world) {
        mScene.Build(mDevice, world, static_cast<float>(frameIntervalMs / 1000.0));
    }
    if (ImGui::Begin("Console")) {
        for (const auto& line : log.Snapshot()) {
            ImGui::TextUnformatted(line.c_str());
        }
    }
    ImGui::End();
    if (ImGui::Begin("Statistics")) {
        const auto scene = mScene.ActiveSize();
        const auto [x, y] = mWindow.CursorPosition();
        const auto dpi = ImGui::GetIO().DisplayFramebufferScale;
        if (frameIntervalMs > 0.0) {
            ImGui::Text("Frame interval: %.2f ms  |  %.0f fps", frameIntervalMs, 1000.0 / frameIntervalMs);
        } else {
            ImGui::TextUnformatted("GPU capture mode");
        }
        ImGui::Text("Output: %u x %u px", framebuffer.width, framebuffer.height);
        ImGui::Text("Scene: %u x %u px   Scale: %.1f x %.1f", scene.width, scene.height, dpi.x, dpi.y);
        ImGui::Text("Cursor: %.0f, %.0f   Scene focus: %s", x, y, mScene.IsFocused() ? "yes" : "no");
    }
    ImGui::End();
    ImGui::Render();
}

//————————————————————————————————————————————————————————————————————————————————
// 将界面和 Scene Viewport 图片合成到 Surface 视图。
// 官方 WebGPU 后端为场景视图创建本帧的纹理绑定。
//————————————————————————————————————————————————————————————————————————————————
void EditorUi::Encode(WGPUCommandEncoder encoder, WGPUTextureView target) const {
    auto pass = BeginColorPass(encoder, target, {0.0, 0.0, 0.0, 1.0}, "Editor UI pass");
    ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), pass.get());
    wgpuRenderPassEncoderEnd(pass.get());
}

//————————————————————————————————————————————————————————————————————————————————
// UI 优先响应该帧的键盘输入。
// 焦点在场景中且 UI 没有捕获键盘时才交给场景命令。
//————————————————————————————————————————————————————————————————————————————————
bool EditorUi::AllowsSceneKeyboard() const {
    return mScene.IsFocused() && !ImGui::GetIO().WantCaptureKeyboard;
}

}  // namespace tinyworld
