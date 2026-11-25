#include "Engine/Scene/SceneFile.h"

#include <cstdio>
#include <stdexcept>
#include <string_view>

int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::runtime_error("WorldChunker input.json output_directory [--exploration] [--raw]");
        bool exploration = false;
        bool compress = true;
        for (int i = 3; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (arg == "--exploration") exploration = true;
            else if (arg == "--raw") compress = false;
            else throw std::runtime_error("Unknown WorldChunker option.");
        }
        tinyworld::PackWorldFile(std::filesystem::u8path(argv[1]), std::filesystem::u8path(argv[2]),
                                 exploration, compress);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
