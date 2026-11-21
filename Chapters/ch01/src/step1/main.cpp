#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdlib>
#include <iostream>

//————————————————————————————————————————————————————————————————————————————————
// 创建一扇没有图形 API 上下文的原生窗口，等待关闭事件后释放它。
//————————————————————————————————————————————————————————————————————————————————
int main() {
    if (!glfwInit()) {
        std::cerr << "GLFW initialization failed\n";
        return EXIT_FAILURE;
    }

    // 下一步会由 WebGPU 为这个原生窗口创建呈现目标。
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(640, 480, "TinyWorld - First Window", nullptr, nullptr);
    if (!window) {
        std::cerr << "Window creation failed\n";
        glfwTerminate();
        return EXIT_FAILURE;
    }

    std::cout << "GLFW window ready" << std::endl;
    while (!glfwWindowShouldClose(window)) {
        glfwWaitEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
