#include "Engine/Scene/Lightmap.h"
#include "Engine/Scene/SceneFile.h"
#include "Engine/Scene/WorldScene.h"
#include "Tools/LightmapBaker/Bake.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace tinyworld;

//————————————————————————————————————————————————————————————————————————————————
// 同时覆盖 Debug 和 Release，失败时输出具体的课件验收项。
//————————————————————————————————————————————————————————————————————————————————
void Require(bool passed, const char* message) {
    if (!passed) throw std::runtime_error(message);
}

//————————————————————————————————————————————————————————————————————————————————
// 以版本 6 或 7 的编辑器能力构造接收场景，沿用应用的读取门槛。
//————————————————————————————————————————————————————————————————————————————————
WorldScene MakeScene(bool withLightmaps) {
    WorldScene scene;
    scene.terrain.emplace();
    scene.canSculpt = true;
    scene.road.emplace();
    scene.canPlaceProps = true;
    scene.canUseGltfProps = true;
    scene.vegetation.emplace().mode = VegetationMode::TerrainRules;
    if (withLightmaps) scene.lightmapEnabled = true;
    return scene;
}

//————————————————————————————————————————————————————————————————————————————————
// 验证图集可以重烘焙，覆盖像素的柱影与图岛外扩均保留下来。
//————————————————————————————————————————————————————————————————————————————————
void CheckAtlas(const std::filesystem::path& root, const std::filesystem::path& scratch) {
    const auto atlas = ReadLightmap(root / "lightmaps/ch17/porch.twlm");
    const auto asset = LoadGltfAsset(root / kPropAssets[kPorchAsset].id);
    Require(BakePorch(asset).pixels == atlas.pixels, "Deployed lightmap differs from a fresh bake.");
    const auto texel = [&](std::size_t x, std::size_t y) {
        return atlas.pixels.begin() + 4 * (y * atlas.width + x);
    };
    const auto lit = texel(45, 45);
    const auto shadow = texel(67, 53);
    const auto border = texel(4, 45);
    Require(lit[0] > 35 && lit[0] > lit[1] && lit[3] == 255, "Expected warm direct light on the deck.");
    Require(shadow[0] == 0 && shadow[1] == 0 && shadow[2] == 0 && shadow[3] == 255,
            "Expected a covered deck texel shadowed by the post.");
    Require(border[3] == 128 && std::equal(border, border + 3, texel(5, 45)) && texel(0, 0)[3] == 0,
            "Expected RGB padding next to a chart and empty space outside the padding.");
    const auto broken = scratch / "broken.twlm";
    WriteLightmap(broken, atlas);
    std::ofstream(broken, std::ios::binary | std::ios::app).put('\0');
    bool rejected = false;
    try { (void)ReadLightmap(broken); } catch (const std::runtime_error&) { rejected = true; }
    Require(rejected, "Trailing lightmap bytes were accepted.");
}

//————————————————————————————————————————————————————————————————————————————————
// 保存再打开，检查相对贴图引用、预览开关与旧章节的版本保护。
//————————————————————————————————————————————————————————————————————————————————
void CheckScene(const std::filesystem::path& root, const std::filesystem::path& scratch) {
    auto scene = MakeScene(true);
    LoadScene(root / "scenes/ch17/porch.json", scene);
    *scene.lightmapEnabled = false;
    const auto path = scratch / "reopened.json";
    SaveScene(path, scene);
    LoadScene(path, scene);
    const auto porch = std::find_if(scene.props.begin(), scene.props.end(),
                                    [](const auto& prop) { return prop.asset == kPorchAsset; });
    Require(porch != scene.props.end() && porch->lightmap == kPropAssets[kPorchAsset].lightmap &&
            scene.lightmapEnabled == false, "The scene lost its atlas path or preview setting.");
    auto legacy = MakeScene(false);
    LoadScene(root / "scenes/ch15/coast.json", legacy);
    const auto originalCount = legacy.props.size();
    bool rejected = false;
    try { LoadScene(path, legacy); } catch (const std::runtime_error&) { rejected = true; }
    Require(rejected && !legacy.lightmapEnabled && legacy.props.size() == originalCount,
            "The previous chapter must reject a lightmap scene without modifying its state.");
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 指定验收临时目录，检查构建后部署的模型、图集和 JSON。
//————————————————————————————————————————————————————————————————————————————————
int main(int argc, char** argv) {
    if (argc != 3) return 1;
    try {
        const auto scratch = std::filesystem::path(argv[2]) / "lightmap-checks";
        std::filesystem::create_directories(scratch);
        CheckAtlas(argv[1], scratch);
        CheckScene(argv[1], scratch);
        std::cout << "Lightmap is reproducible; occlusion, padding and scene reopen checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
