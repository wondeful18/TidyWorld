#include "Engine/Renderer/WebGpuContext.h"

#include "Engine/Core/Log.h"
#include "Engine/Platform/Surface.h"

#include <webgpu/wgpu.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>

namespace tinyworld {
namespace {

//————————————————————————————————————————————————————————————————————————————————
// 将 WebGPU 的字符串视图转成可读的本地视图。
// 回调返回前，调用方需要复制仍要使用的内容。
//————————————————————————————————————————————————————————————————————————————————
std::string_view AsText(WGPUStringView value) {
    if (!value.data) {
        return {};
    }
    return value.length == WGPU_STRLEN ? std::string_view(value.data)
                                      : std::string_view(value.data, value.length);
}

//————————————————————————————————————————————————————————————————————————————————
// 返回课程首选原生后端的日志名称。
//————————————————————————————————————————————————————————————————————————————————
const char* BackendName(WGPUBackendType backend) {
    switch (backend) {
    case WGPUBackendType_Metal: return "Metal";
    case WGPUBackendType_D3D12: return "D3D12";
    default: return "other";
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 返回实际 Surface 颜色格式的日志名称。
//————————————————————————————————————————————————————————————————————————————————
const char* FormatName(WGPUTextureFormat format) {
    switch (format) {
    case WGPUTextureFormat_BGRA8Unorm: return "BGRA8Unorm";
    case WGPUTextureFormat_RGBA8Unorm: return "RGBA8Unorm";
    case WGPUTextureFormat_BGRA8UnormSrgb: return "BGRA8UnormSrgb";
    case WGPUTextureFormat_RGBA8UnormSrgb: return "RGBA8UnormSrgb";
    default: return "unknown";
    }
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 复制仅在回调期间有效的诊断，并标记请求结束。
// 内存不足时保留成功状态，主循环仍会检查请求结果。
//————————————————————————————————————————————————————————————————————————————————
void WebGpuContext::RequestState::Complete(bool success, WGPUStringView message) noexcept {
    succeeded = success;
    try {
        error = AsText(message);
    } catch (...) {
        error.clear();
    }
    done = true;
}

//————————————————————————————————————————————————————————————————————————————————
// 选择兼容窗口的原生 Adapter 并建立设备。
// 请求回调把拥有的引用交给 RAII 成员，设备诊断可从任意线程到达。
//————————————————————————————————————————————————————————————————————————————————
WebGpuContext::WebGpuContext(Window& window, Log& log) : mWindow(window), mLog(log) {
    mInstance.reset(wgpuCreateInstance(nullptr));
    if (!mInstance) {
        throw std::runtime_error("wgpuCreateInstance failed");
    }
    mSurface.reset(CreateWindowSurface(mInstance.get(), mWindow.Native()));
    if (!mSurface) {
        throw std::runtime_error("Cannot create WebGPU Surface for the GLFW window");
    }

    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    options.compatibleSurface = mSurface.get();
#ifdef __APPLE__
    options.backendType = WGPUBackendType_Metal;
#else
    options.backendType = WGPUBackendType_D3D12;
#endif
    WGPURequestAdapterCallbackInfo adapterCallback = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    adapterCallback.mode = WGPUCallbackMode_AllowProcessEvents;
    adapterCallback.userdata1 = this;
    adapterCallback.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter,
                                  WGPUStringView message, void* user, void*) {
        auto& context = *static_cast<WebGpuContext*>(user);
        context.mAdapter.reset(adapter);
        context.mAdapterState.Complete(status == WGPURequestAdapterStatus_Success && adapter, message);
    };
    wgpuInstanceRequestAdapter(mInstance.get(), &options, adapterCallback);
    WaitFor(mAdapterState, "RequestAdapter");

    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    if (wgpuAdapterGetInfo(mAdapter.get(), &info) != WGPUStatus_Success) {
        throw std::runtime_error("Cannot read WebGPU adapter information");
    }
    const auto adapterName = std::string(AsText(info.device));
    const auto backend = info.backendType;
    wgpuAdapterInfoFreeMembers(info);
    mLog.Write("GPU", "Adapter: " + adapterName + " / " + BackendName(backend));
    ReadSurfaceCapabilities();

    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.label = {"TinyWorld device", WGPU_STRLEN};
    descriptor.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    descriptor.deviceLostCallbackInfo.userdata1 = this;
    descriptor.deviceLostCallbackInfo.callback = [](const WGPUDevice*, WGPUDeviceLostReason reason,
                                                   WGPUStringView message, void* user, void*) {
        if (reason != WGPUDeviceLostReason_Destroyed && reason != WGPUDeviceLostReason_CallbackCancelled) {
            static_cast<WebGpuContext*>(user)->ReportGpuFailure("Device lost", message);
        }
    };
    descriptor.uncapturedErrorCallbackInfo.userdata1 = this;
    descriptor.uncapturedErrorCallbackInfo.callback = [](const WGPUDevice*, WGPUErrorType,
                                                        WGPUStringView message, void* user, void*) {
        static_cast<WebGpuContext*>(user)->ReportGpuFailure("Uncaptured WebGPU error", message);
    };

    WGPURequestDeviceCallbackInfo deviceCallback = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    deviceCallback.mode = WGPUCallbackMode_AllowProcessEvents;
    deviceCallback.userdata1 = this;
    deviceCallback.callback = [](WGPURequestDeviceStatus status, WGPUDevice device,
                                 WGPUStringView message, void* user, void*) {
        auto& context = *static_cast<WebGpuContext*>(user);
        context.mDevice.reset(device);
        context.mDeviceState.Complete(status == WGPURequestDeviceStatus_Success && device, message);
    };
    wgpuAdapterRequestDevice(mAdapter.get(), &descriptor, deviceCallback);
    WaitFor(mDeviceState, "RequestDevice");
    mQueue.reset(wgpuDeviceGetQueue(mDevice.get()));
    if (!mQueue) {
        throw std::runtime_error("Cannot acquire WebGPU Queue");
    }
    mLog.Write("GPU", "Device and Queue ready");
}

//————————————————————————————————————————————————————————————————————————————————
// 先清除 Surface 对当前设备的配置，再释放持有的引用。
// 日志和回调状态直到 GPU 成员销毁后才离开作用域。
//————————————————————————————————————————————————————————————————————————————————
WebGpuContext::~WebGpuContext() {
    if (mSurface) {
        wgpuSurfaceUnconfigure(mSurface.get());
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 保留原生设备分配的首选格式中可用于本章 8 位 SDR 的格式。
// 优先使用 Unorm，与 ImGui 直接传递的显示颜色一致。
//————————————————————————————————————————————————————————————————————————————————
void WebGpuContext::ReadSurfaceCapabilities() {
    WGPUSurfaceCapabilities capabilities = WGPU_SURFACE_CAPABILITIES_INIT;
    if (wgpuSurfaceGetCapabilities(mSurface.get(), mAdapter.get(), &capabilities) != WGPUStatus_Success) {
        throw std::runtime_error("Adapter cannot present to the current Surface");
    }
    const auto supported = [&capabilities](WGPUTextureFormat format) {
        return capabilities.formatCount > 0 &&
            std::find(capabilities.formats, capabilities.formats + capabilities.formatCount, format)
                != capabilities.formats + capabilities.formatCount;
    };
    if (mFormat == WGPUTextureFormat_Undefined) {
        constexpr std::array formats{WGPUTextureFormat_BGRA8Unorm, WGPUTextureFormat_RGBA8Unorm,
                                    WGPUTextureFormat_BGRA8UnormSrgb, WGPUTextureFormat_RGBA8UnormSrgb};
        const auto preferred = std::find_if(formats.begin(), formats.end(), supported);
        if (preferred != formats.end()) {
            mFormat = *preferred;
        }
    }
    const bool compatible = supported(mFormat);
    wgpuSurfaceCapabilitiesFreeMembers(capabilities);
    if (!compatible) {
        throw std::runtime_error("Surface has no compatible 8-bit color format");
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 更新窗口像素大小并使用 FIFO 呈现。
// 逻辑大小与 framebuffer 大小在高 DPI 环境中可能不同。
//————————————————————————————————————————————————————————————————————————————————
void WebGpuContext::Configure(PixelSize size) {
    WGPUSurfaceConfiguration configuration = WGPU_SURFACE_CONFIGURATION_INIT;
    configuration.device = mDevice.get();
    configuration.format = mFormat;
    configuration.width = size.width;
    configuration.height = size.height;
    configuration.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(mSurface.get(), &configuration);
    mConfiguredSize = size;
    mLog.Write("SURFACE", std::to_string(size.width) + " x " + std::to_string(size.height) +
        " px / " + FormatName(mFormat) + " / FIFO");
}

//————————————————————————————————————————————————————————————————————————————————
// 主线程在等待请求期间主动处理事件。
// 设定截止时间，避免初始化或验证无限等待。
//————————————————————————————————————————————————————————————————————————————————
void WebGpuContext::WaitFor(const RequestState& state, std::string_view operation) {
    using namespace std::chrono_literals;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!state.done && std::chrono::steady_clock::now() < deadline) {
        ProcessEvents();
        if (!state.done) {
            std::this_thread::sleep_for(1ms);
        }
    }
    if (!state.done) {
        throw std::runtime_error(std::string(operation) + ": timed out");
    }
    if (!state.succeeded) {
        throw std::runtime_error(std::string(operation) + ": " + (state.error.empty() ? "request failed" : state.error));
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 将设备错误带回主线程，不在回调内调用任何 WebGPU API。
// 即使日志写入失败，主循环也会停止渲染。
//————————————————————————————————————————————————————————————————————————————————
void WebGpuContext::ReportGpuFailure(std::string_view category, WGPUStringView message) noexcept {
    mGpuFailed = true;
    try {
        mLog.Write("ERROR", std::string(category) + ": " + std::string(AsText(message)));
    } catch (...) {
        std::fputs("[ERROR] WebGPU callback reported a failure\n", stderr);
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 处理队列回调并检查设备健康状态。
// 窗口最小化期间也继续驱动回调。
//————————————————————————————————————————————————————————————————————————————————
void WebGpuContext::ProcessEvents() {
    wgpuInstanceProcessEvents(mInstance.get());
    if (mGpuFailed) {
        throw std::runtime_error("WebGPU device failure; see the preceding diagnostic");
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 在窗口大小变化或 Surface 恢复后取得一个新的帧纹理。
// 对暂时失败执行跳帧或有界恢复，避免在同一帧内反复取图。
//————————————————————————————————————————————————————————————————————————————————
TextureHandle WebGpuContext::AcquireFrame(PixelSize size) {
    if (size.width == 0 || size.height == 0) {
        return TextureHandle{};
    }
    if (mConfiguredSize != size) {
        Configure(size);
    }

    WGPUSurfaceTexture frame = WGPU_SURFACE_TEXTURE_INIT;
    wgpuSurfaceGetCurrentTexture(mSurface.get(), &frame);
    TextureHandle texture(frame.texture);
    // wgpu-native 扩展了标准状态值。被遮挡的窗口可以长时间等待，不消耗失败重试额度。
    if (static_cast<unsigned>(frame.status) == WGPUSurfaceGetCurrentTextureStatus_Occluded) {
        if (!mOccluded) {
            mLog.Write("SURFACE", "window occluded; waiting for a drawable");
            mOccluded = true;
        }
        return TextureHandle{};
    }
    mOccluded = false;
    switch (frame.status) {
    case WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal:
    case WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal:
        if (!texture) {
            throw std::runtime_error("WebGPU acquired an empty Surface texture");
        }
        mRetryCount = 0;
        if (frame.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
            // 本帧仍可呈现，下一帧在取得纹理之前再重新配置。
            mConfiguredSize = {};
        }
        return texture;
    case WGPUSurfaceGetCurrentTextureStatus_Timeout:
        if (mRetryCount++ == 0) {
            mLog.Write("WARN", "Surface acquisition timed out; skipping frame");
        }
        break;
    case WGPUSurfaceGetCurrentTextureStatus_Outdated:
        mLog.Write("WARN", "Surface is outdated; reconfiguring on the next frame");
        ++mRetryCount;
        mConfiguredSize = {};
        break;
    case WGPUSurfaceGetCurrentTextureStatus_Lost:
        mLog.Write("WARN", "Surface lost; recreating the window binding");
        ++mRetryCount;
        texture.reset();
        mSurface.reset();
        mSurface.reset(CreateWindowSurface(mInstance.get(), mWindow.Native()));
        if (!mSurface) {
            throw std::runtime_error("Cannot recreate the lost Surface");
        }
        ReadSurfaceCapabilities();
        mConfiguredSize = {};
        break;
    default:
        throw std::runtime_error("Surface acquisition failed, status " + std::to_string(frame.status));
    }
    if (mRetryCount >= 120) {
        throw std::runtime_error("Surface remained unavailable after 120 retries");
    }
    return TextureHandle{};
}

//————————————————————————————————————————————————————————————————————————————————
// 将已经编码和提交的 Surface 纹理交给窗口系统。
// 错误状态由调用方统一记录和处理。
//————————————————————————————————————————————————————————————————————————————————
void WebGpuContext::Present() {
    if (wgpuSurfacePresent(mSurface.get()) != WGPUStatus_Success) {
        throw std::runtime_error("wgpuSurfacePresent failed");
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 收集当前验证作用域报告的 WGSL 或 Pipeline 错误。
// 附上调用方的文件或资源名，便于定位失败。
//————————————————————————————————————————————————————————————————————————————————
void WebGpuContext::CheckValidation(std::string_view label) {
    mValidationState = {};
    WGPUPopErrorScopeCallbackInfo callback = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowProcessEvents;
    callback.userdata1 = &mValidationState;
    callback.callback = [](WGPUPopErrorScopeStatus status, WGPUErrorType error,
                           WGPUStringView message, void* user, void*) {
        auto& state = *static_cast<RequestState*>(user);
        state.Complete(status == WGPUPopErrorScopeStatus_Success && error == WGPUErrorType_NoError, message);
    };
    wgpuDevicePopErrorScope(mDevice.get(), callback);
    WaitFor(mValidationState, label);
}

//————————————————————————————————————————————————————————————————————————————————
// 等待已提交命令的完成信号。
// 自动验证可借此确认工作到达真实 GPU。
//————————————————————————————————————————————————————————————————————————————————
void WebGpuContext::WaitForSubmittedWork() {
    mQueueState = {};
    WGPUQueueWorkDoneCallbackInfo callback = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowProcessEvents;
    callback.userdata1 = &mQueueState;
    callback.callback = [](WGPUQueueWorkDoneStatus status, WGPUStringView message, void* user, void*) {
        auto& state = *static_cast<RequestState*>(user);
        state.Complete(status == WGPUQueueWorkDoneStatus_Success, message);
    };
    wgpuQueueOnSubmittedWorkDone(mQueue.get(), callback);
    WaitFor(mQueueState, "Queue completion");
    ProcessEvents();
}

}  // namespace tinyworld
