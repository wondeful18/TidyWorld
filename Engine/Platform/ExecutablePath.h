#pragma once

#include <filesystem>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 返回当前可执行文件所在的绝对目录。
// 资源部署与查找使用该路径，独立于进程工作目录。
//————————————————————————————————————————————————————————————————————————————————
[[nodiscard]] std::filesystem::path ExecutableDirectory();

}  // namespace tinyworld
