#include "Editor/Application.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string_view>

//————————————————————————————————————————————————————————————————————————————————
// 读取章节和输出选项，并把失败报告为非零进程退出码。
//————————————————————————————————————————————————————————————————————————————————
int main(int argc, char** argv) {
    tinyworld::ApplicationOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--direct") {
            options.direct = true;
        } else if (argument == "--smoke-test") {
            options.smokeTest = true;
        } else if (argument == "--capture" && index + 1 < argc) {
            options.capturePath = argv[++index];
        } else if (argument == "--chapter" && index + 1 < argc) {
            const std::string_view value = argv[++index];
            if (value != "01" && value != "02") {
                std::cerr << "Choose chapter 01 or 02.\n";
                return EXIT_FAILURE;
            }
            options.chapter = value == "02" ? tinyworld::Chapter::World : tinyworld::Chapter::Triangle;
        } else if (argument == "--preset" && index + 1 < argc) {
            const std::string_view value = argv[++index];
            if (value == "overlap") {
                options.worldPreset = tinyworld::WorldPreset::Overlap;
            } else if (value == "evening") {
                options.worldPreset = tinyworld::WorldPreset::Evening;
            } else if (value == "drive") {
                options.worldPreset = tinyworld::WorldPreset::Drive;
            } else {
                std::cerr << "Choose preset overlap, evening or drive.\n";
                return EXIT_FAILURE;
            }
        } else if (argument == "--help") {
            std::cout << "TinyWorld [--chapter 01|02] [--preset overlap|evening|drive] [--direct]\n"
                         "          [--smoke-test | --capture output.ppm]\n";
            return EXIT_SUCCESS;
        } else {
            std::cerr << "Unknown option: " << argument << " (use --help)\n";
            return EXIT_FAILURE;
        }
    }
    if (options.smokeTest && !options.capturePath.empty()) {
        std::cerr << "Choose either --smoke-test or --capture.\n";
        return EXIT_FAILURE;
    }
    if (options.chapter != tinyworld::Chapter::World && options.worldPreset != tinyworld::WorldPreset::Default) {
        std::cerr << "Scene presets require --chapter 02.\n";
        return EXIT_FAILURE;
    }
    try {
        return tinyworld::RunApplication(options);
    } catch (const std::exception& error) {
        std::cerr << "[FATAL] " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
