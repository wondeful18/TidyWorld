#include "Engine/Renderer/ShaderModule.h"

#include "Engine/Renderer/WebGpuContext.h"

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// WGSL 字节在模块创建时读取，验证作用域负责返回解析错误。
//————————————————————————————————————————————————————————————————————————————————
ShaderHandle LoadWgslModule(WebGpuContext& gpu, const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Cannot open shader: " + path.string());
    }
    const std::string code(std::istreambuf_iterator<char>(stream), {});
    if (stream.bad() || code.empty()) {
        throw std::runtime_error("Cannot read shader: " + path.string());
    }
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = {code.data(), code.size()};
    const auto label = path.filename().string();
    WGPUShaderModuleDescriptor descriptor = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    descriptor.label = {label.data(), label.size()};
    descriptor.nextInChain = &wgsl.chain;

    wgpuDevicePushErrorScope(gpu.Device(), WGPUErrorFilter_Validation);
    ShaderHandle shader(wgpuDeviceCreateShaderModule(gpu.Device(), &descriptor));
    gpu.CheckValidation(path.string());
    if (!shader) {
        throw std::runtime_error("Cannot create shader module: " + path.string());
    }
    return shader;
}

}  // namespace tinyworld
