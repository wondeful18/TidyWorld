#include "Engine/Core/Log.h"

#include <iostream>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 将同一条已格式化的日志发往两个输出位置。
// 每次输出后刷新终端，启动失败时也能立即看到诊断。
//————————————————————————————————————————————————————————————————————————————————
void Log::Write(std::string_view level, std::string_view message) {
    std::lock_guard lock(mMutex);
    std::string line = "[";
    line.append(level).append("] ").append(message);
    std::cerr << line << '\n' << std::flush;
    if (mLines.size() == 128) {
        mLines.pop_front();
    }
    mLines.push_back(std::move(line));
}

//————————————————————————————————————————————————————————————————————————————————
// 在锁的保护下复制日志缓冲。
// 回调线程和编辑器线程可以各自继续工作。
//————————————————————————————————————————————————————————————————————————————————
std::deque<std::string> Log::Snapshot() const {
    std::lock_guard lock(mMutex);
    return mLines;
}

}  // namespace tinyworld
