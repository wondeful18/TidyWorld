#include "Tools/LightmapBaker/Bake.h"

#include <exception>
#include <iostream>

//————————————————————————————————————————————————————————————————————————————————
// 使用同一套 glTF 读取器，把带 UV2 的门廊转换成可部署图集。
//————————————————————————————————————————————————————————————————————————————————
int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: TinyWorldLightmapBaker porch.glb output.twlm\n";
        return 1;
    }
    try {
        const auto asset = tinyworld::LoadGltfAsset(argv[1]);
        tinyworld::WriteLightmap(argv[2], tinyworld::BakePorch(asset));
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
