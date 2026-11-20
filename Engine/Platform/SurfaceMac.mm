#include "Engine/Platform/Surface.h"

#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 将 Cocoa 窗口的内容视图接到 WebGPU。
// CAMetalLayer 只是呈现载体，GPU 命令仍全部经 WebGPU 提交。
//————————————————————————————————————————————————————————————————————————————————
WGPUSurface CreateWindowSurface(WGPUInstance instance, GLFWwindow* window) {
    @autoreleasepool {
        NSWindow* native = glfwGetCocoaWindow(window);
        NSView* view = native.contentView;
        CAMetalLayer* layer = [CAMetalLayer layer];
        layer.contentsScale = native.backingScaleFactor;
        view.wantsLayer = YES;
        view.layer = layer;

        WGPUSurfaceSourceMetalLayer source = WGPU_SURFACE_SOURCE_METAL_LAYER_INIT;
        source.layer = layer;
        WGPUSurfaceDescriptor descriptor = WGPU_SURFACE_DESCRIPTOR_INIT;
        descriptor.nextInChain = &source.chain;
        return wgpuInstanceCreateSurface(instance, &descriptor);
    }
}

}  // namespace tinyworld
