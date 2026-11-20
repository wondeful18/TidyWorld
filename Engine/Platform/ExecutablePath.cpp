#include "Engine/Platform/ExecutablePath.h"

#include <cstdint>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <mach-o/dyld.h>
#endif

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 从操作系统取得正在运行的程序路径。
// 使用文件系统路径类型以支持非 ASCII 工作区名称。
//————————————————————————————————————————————————————————————————————————————————
std::filesystem::path ExecutableDirectory() {
#ifdef _WIN32
    std::vector<wchar_t> buffer(32768);
    const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        throw std::runtime_error("Cannot locate the Windows executable");
    }
#else
    std::uint32_t capacity = 0;
    _NSGetExecutablePath(nullptr, &capacity);
    std::vector<char> buffer(capacity);
    if (_NSGetExecutablePath(buffer.data(), &capacity) != 0) {
        throw std::runtime_error("Cannot locate the macOS executable");
    }
#endif
    return std::filesystem::canonical(buffer.data()).parent_path();
}

}  // namespace tinyworld
