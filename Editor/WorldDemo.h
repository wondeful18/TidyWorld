#pragma once

#include "Chapters/ch02/src/SandboxRenderer.h"
#include "Editor/ChapterPresets.h"
#include "Editor/WorldEditor.h"
#include "Engine/Renderer/WorldRenderer.h"

#include <memory>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 将小屋演示的 CPU 场景、GPU 资源和面板绑定到同一个局部生命周期。
//————————————————————————————————————————————————————————————————————————————————
struct WorldDemo {
    WorldScene scene;       // 编辑器与渲染器共同使用的场景
    std::unique_ptr<WorldRenderer> renderer; // 从第六章开始的地形与世界渲染
    std::unique_ptr<SandboxRenderer> sandbox; // 前四个 3D 热身章节的平地渲染
    WorldEditor editor;    // 借用本对象的场景并接入 ImGui

    //————————————————————————————————————————————————————————————————————————————————
    // 按初始场景设置上传网格，并将同一场景交由编辑器和渲染器使用。
    //————————————————————————————————————————————————————————————————————————————————
    WorldDemo(WebGpuContext& gpu, WGPUTextureFormat format,
              const std::filesystem::path& root, const ApplicationOptions& options)
        : scene(CreateWorldScene(options, root)),
          renderer(scene.terrain ? std::make_unique<WorldRenderer>(gpu, format, scene, root) : nullptr),
          sandbox(scene.terrain ? nullptr : std::make_unique<SandboxRenderer>(gpu, format, scene, root)),
          editor(scene, options.scenePath, options.sculptTools || options.chapter == Chapter::Sculpt,
                 renderer ? renderer->PaintPreviews() : MaterialPreviews{},
                 renderer ? &renderer->VegetationStatistics() : nullptr,
                 renderer ? &renderer->TerrainStatistics() : nullptr,
                 renderer ? &renderer->StreamStatistics() : nullptr,
                 renderer ? &renderer->SceneDraws() : nullptr) {}

    void Encode(WGPUCommandEncoder encoder, WGPUTextureView target, PixelSize size) {
        if (renderer) renderer->Encode(encoder, target, size, scene);
        else sandbox->Encode(encoder, target, size, scene);
    }

    [[nodiscard]] bool StreamingSettled() const { return !renderer || renderer->StreamingSettled(); }
    void WaitForChunkData() { if (renderer) renderer->WaitForChunkData(); }

    //————————————————————————————————————————————————————————————————————————————————
    // 禁用复制和隐式移动，使 editor 对 scene 的借用始终指向本实例。
    //————————————————————————————————————————————————————————————————————————————————
    WorldDemo(const WorldDemo&) = delete;

    //————————————————————————————————————————————————————————————————————————————————
    // 场景通过预设原地更新，整套演示对象在应用中保持固定地址。
    //————————————————————————————————————————————————————————————————————————————————
    WorldDemo& operator=(const WorldDemo&) = delete;
};


}  // namespace tinyworld
