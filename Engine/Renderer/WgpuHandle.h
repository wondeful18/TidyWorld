#pragma once

#include <webgpu/webgpu.h>

#include <utility>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 独占一个由 WebGPU C API 返回的引用。
// 借用的句柄通过 get 传递，拥有的引用在移动或析构时成对释放。
//————————————————————————————————————————————————————————————————————————————————
template <typename Handle, void (*Release)(Handle)>
class WgpuHandle {
public:
    //————————————————————————————————————————————————————————————————————————————————
    // 接收一个拥有的引用，或创建一个空句柄。
    //————————————————————————————————————————————————————————————————————————————————
    explicit WgpuHandle(Handle value = nullptr) : mValue(value) {}

    //————————————————————————————————————————————————————————————————————————————————
    // 释放当前拥有的引用。
    //————————————————————————————————————————————————————————————————————————————————
    ~WgpuHandle() { reset(); }

    //————————————————————————————————————————————————————————————————————————————————
    // 由单个对象拥有引用，禁用复制构造。
    //————————————————————————————————————————————————————————————————————————————————
    WgpuHandle(const WgpuHandle&) = delete;

    //————————————————————————————————————————————————————————————————————————————————
    // 禁用复制赋值，避免两个包装对象释放同一个拥有的引用。
    //————————————————————————————————————————————————————————————————————————————————
    WgpuHandle& operator=(const WgpuHandle&) = delete;

    //————————————————————————————————————————————————————————————————————————————————
    // 从另一个包装对象接管引用并将其清空。
    //————————————————————————————————————————————————————————————————————————————————
    WgpuHandle(WgpuHandle&& other) noexcept : mValue(std::exchange(other.mValue, nullptr)) {}

    //————————————————————————————————————————————————————————————————————————————————
    // 释放自身原引用，再接管传入对象的引用。
    //————————————————————————————————————————————————————————————————————————————————
    WgpuHandle& operator=(WgpuHandle&& other) noexcept {
        if (this != &other) {
            reset(std::exchange(other.mValue, nullptr));
        }
        return *this;
    }

    //————————————————————————————————————————————————————————————————————————————————
    // 返回当前引用供调用者借用，保持所有权。
    //————————————————————————————————————————————————————————————————————————————————
    [[nodiscard]] Handle get() const { return mValue; }

    //————————————————————————————————————————————————————————————————————————————————
    // 检查包装对象是否持有引用。
    //————————————————————————————————————————————————————————————————————————————————
    explicit operator bool() const { return mValue != nullptr; }

    //————————————————————————————————————————————————————————————————————————————————
    // 交还原引用，并接管一个新取得的引用。
    //————————————————————————————————————————————————————————————————————————————————
    void reset(Handle value = nullptr) {
        if (const auto old = std::exchange(mValue, value)) {
            Release(old);
        }
    }

private:
    Handle mValue;  // 当前独占的 WebGPU 引用，可以为空
};

using InstanceHandle = WgpuHandle<WGPUInstance, wgpuInstanceRelease>;
using SurfaceHandle = WgpuHandle<WGPUSurface, wgpuSurfaceRelease>;
using AdapterHandle = WgpuHandle<WGPUAdapter, wgpuAdapterRelease>;
using DeviceHandle = WgpuHandle<WGPUDevice, wgpuDeviceRelease>;
using QueueHandle = WgpuHandle<WGPUQueue, wgpuQueueRelease>;
using TextureHandle = WgpuHandle<WGPUTexture, wgpuTextureRelease>;
using TextureViewHandle = WgpuHandle<WGPUTextureView, wgpuTextureViewRelease>;
using SamplerHandle = WgpuHandle<WGPUSampler, wgpuSamplerRelease>;
using BufferHandle = WgpuHandle<WGPUBuffer, wgpuBufferRelease>;
using ShaderHandle = WgpuHandle<WGPUShaderModule, wgpuShaderModuleRelease>;
using PipelineHandle = WgpuHandle<WGPURenderPipeline, wgpuRenderPipelineRelease>;
using PipelineLayoutHandle = WgpuHandle<WGPUPipelineLayout, wgpuPipelineLayoutRelease>;
using BindGroupLayoutHandle = WgpuHandle<WGPUBindGroupLayout, wgpuBindGroupLayoutRelease>;
using BindGroupHandle = WgpuHandle<WGPUBindGroup, wgpuBindGroupRelease>;
using EncoderHandle = WgpuHandle<WGPUCommandEncoder, wgpuCommandEncoderRelease>;
using PassHandle = WgpuHandle<WGPURenderPassEncoder, wgpuRenderPassEncoderRelease>;
using CommandHandle = WgpuHandle<WGPUCommandBuffer, wgpuCommandBufferRelease>;

}  // namespace tinyworld
