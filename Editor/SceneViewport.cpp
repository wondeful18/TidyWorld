#include "Editor/SceneViewport.h"

#include "Editor/WorldEditor.h"

#include <imgui.h>

#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 把 ImGui 的逻辑区域换算为纹理像素数，并构建图片控件。
// 后端契约接受 WGPUTextureView，窗口本身并不拥有该 GPU 资源。
//————————————————————————————————————————————————————————————————————————————————
void SceneViewport::Build(WGPUDevice device, WorldEditor* world) {
    mVisible = false;
    mFocused = false;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    const bool opened = ImGui::Begin("Scene Viewport");
    ImGui::PopStyleVar();
    try {
        if (opened) {
            mFocused = ImGui::IsWindowFocused();
            const auto available = ImGui::GetContentRegionAvail();
            const auto scale = ImGui::GetIO().DisplayFramebufferScale;
            if (available.x > 0.0f && available.y > 0.0f && scale.x > 0.0f && scale.y > 0.0f) {
                const PixelSize size{static_cast<std::uint32_t>(std::ceil(available.x * scale.x)),
                                     static_cast<std::uint32_t>(std::ceil(available.y * scale.y))};
                if (size != mSize) {
                    Resize(device, size);
                }
                const ImTextureID id = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(mView.get()));
                ImGui::Image(ImTextureRef{id}, available);
                mVisible = true;
                if (world) {
                    world->DrawViewport(mSize);
                }
            }
        }
    } catch (...) {
        ImGui::End();
        throw;
    }
    ImGui::End();
}

//————————————————————————————————————————————————————————————————————————————————
// 只把面板本帧引用的纹理视图交给场景绘制。
// 即使旧纹理尚在，隐藏的面板也无需占用绘制时间。
//————————————————————————————————————————————————————————————————————————————————
WGPUTextureView SceneViewport::ActiveTarget() const {
    return mVisible ? mView.get() : nullptr;
}

//————————————————————————————————————————————————————————————————————————————————
// 返回本帧离屏绘制覆盖的像素范围。
//————————————————————————————————————————————————————————————————————————————————
PixelSize SceneViewport::ActiveSize() const {
    return mVisible ? mSize : PixelSize{};
}

//————————————————————————————————————————————————————————————————————————————————
// 分配一个供场景通道写入并供 UI 通道采样的颜色纹理。
// 更新所有权前先准备新视图，使失败时仍有旧资源可供清理。
//————————————————————————————————————————————————————————————————————————————————
void SceneViewport::Resize(WGPUDevice device, PixelSize size) {
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.label = {"Scene Viewport color", WGPU_STRLEN};
    descriptor.size = {size.width, size.height, 1};
    descriptor.format = kColorFormat;
    descriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    TextureHandle texture(wgpuDeviceCreateTexture(device, &descriptor));
    if (!texture) {
        throw std::runtime_error("Cannot allocate Scene Viewport texture");
    }
    TextureViewHandle view(wgpuTextureCreateView(texture.get(), nullptr));
    if (!view) {
        throw std::runtime_error("Cannot create Scene Viewport texture view");
    }
    // 首先交还旧视图，再交还它所引用的纹理。
    mView = std::move(view);
    mTexture = std::move(texture);
    mSize = size;
}

}  // namespace tinyworld
