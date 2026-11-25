#pragma once

#include "Engine/Platform/Window.h"
#include "Engine/Renderer/WgpuHandle.h"

namespace tinyworld {

class WorldEditor;

//————————————————————————————————————————————————————————————————————————————————
// 管理 Scene Viewport 中用于显示场景的离屏颜色目标。
// ImGui 借用 TextureView，本对象持有纹理与视图。
//————————————————————————————————————————————————————————————————————————————————
class SceneViewport {
public:
    static constexpr WGPUTextureFormat kColorFormat = WGPUTextureFormat_RGBA8UnormSrgb;  // 保存 SDR 显示颜色的格式

    //————————————————————————————————————————————————————————————————————————————————
    // 测量面板空间并提交图片控件。
    // 面板可见时才根据 framebuffer 比例准备离屏纹理。
    //————————————————————————————————————————————————————————————————————————————————
    void Build(WGPUDevice device, WorldEditor* world = nullptr, float deltaSeconds = 0.0f);

    //————————————————————————————————————————————————————————————————————————————————
    // 返回本帧要绘入的场景目标，折叠或隐藏时为空。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] WGPUTextureView ActiveTarget() const;

    //————————————————————————————————————————————————————————————————————————————————
    // 返回当前场景目标像素尺寸，隐藏时报告零尺寸。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] PixelSize ActiveSize() const;

    //————————————————————————————————————————————————————————————————————————————————
    // 查询 Scene Viewport 是否拥有键盘焦点。
    // 游戏输入接入后可以用它与 UI capture 状态共同判断。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] bool IsFocused() const { return mFocused; }

private:
    //————————————————————————————————————————————————————————————————————————————————
    // 创建指定尺寸的新纹理和视图，成功后替换旧资源。
    //————————————————————————————————————————————————————————————————————————————————
    void Resize(WGPUDevice device, PixelSize size);

    TextureHandle mTexture;       // 可渲染且可采样的场景纹理
    TextureViewHandle mDisplayView;  // ImGui 读取已经编码的显示字节
    TextureViewHandle mView;      // 提供给渲染通道和 ImGui 的视图
    PixelSize mSize{};            // 已分配离屏纹理的像素尺寸
    bool mVisible{};              // 本帧已经向 ImGui 提交图片
    bool mFocused{};              // 本帧场景面板持有的输入焦点
};

}  // namespace tinyworld
