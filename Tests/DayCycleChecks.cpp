#include "Engine/Scene/DayCycle.h"
#include "Engine/Scene/SceneFile.h"

#include <glm/geometric.hpp>

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace tinyworld;

//————————————————————————————————————————————————————————————————————————————————
// 用同样的条件验收 Debug 和 Release。
//————————————————————————————————————————————————————————————————————————————————
void Require(bool passed, const char* message) {
    if (!passed) throw std::runtime_error(message);
}

//————————————————————————————————————————————————————————————————————————————————
// 检查色温、方位和日夜混合在关键时刻的相对关系，以及午夜的周期连续性。
//————————————————————————————————————————————————————————————————————————————————
void CheckDaylight() {
    const auto dawn = EvaluateDaylight(6.75f);
    const auto noon = EvaluateDaylight(12.0f);
    const auto dusk = EvaluateDaylight(17.25f);
    const auto night = EvaluateDaylight(21.0f);
    Require(dawn.sun.TowardLight().x > 0.8f && dusk.sun.TowardLight().x < -0.8f &&
            noon.sun.TowardLight().y > dawn.sun.TowardLight().y,
            "Sun should cross the valley from east to west and rise higher at noon.");
    Require(noon.sun.intensity > dawn.sun.intensity && dawn.sun.intensity > 0.0f &&
            dawn.sun.color.b < noon.sun.color.b && dawn.sun.color.g < noon.sun.color.g,
            "Sunrise should be warm and weaker than noon.");
    Require(noon.nightMix == 0.0f && night.nightMix == 1.0f && dawn.nightMix > 0.0f &&
            dawn.nightMix < 1.0f && night.sun.intensity == 0.0f && night.ambient < noon.ambient,
            "Day, twilight and night must produce distinct solar and environmental values.");
    const auto midnight = EvaluateDaylight(24.0f);
    const auto start = EvaluateDaylight(0.0f);
    const auto before = EvaluateDaylight(23.999f);
    Require(WrapDayHour(24.25f) == 0.25f && WrapDayHour(-0.25f) == 23.75f &&
            midnight.nightMix == start.nightMix && midnight.sun.azimuth == start.sun.azimuth &&
            glm::distance(before.sun.TowardLight(), start.sun.TowardLight()) < 0.001f,
            "Midnight must wrap without a visual jump.");
}

//————————————————————————————————————————————————————————————————————————————————
// 用同一个门廊存档检查旧版兼容、时刻保存和新版能力门槛。
//————————————————————————————————————————————————————————————————————————————————
void CheckScene(const std::filesystem::path& root, const std::filesystem::path& scratch) {
    WorldScene scene;
    scene.terrain.emplace();
    scene.road.emplace();
    scene.canSculpt = true;
    scene.canPlaceProps = true;
    scene.canUseGltfProps = true;
    scene.vegetation.emplace().mode = VegetationMode::TerrainRules;
    scene.lightmapEnabled = true;
    scene.dayCycle = DayCycleSettings{21.25f, false};
    scene.lightingLab = LightingLab{WalkerLamp::Spot, true, 1.0f};
    scene.normalMaps = false;

    LoadScene(root / "scenes/ch17/porch.json", scene);
    Require(scene.dayCycle->hour == 21.25f, "A Chapter 17 scene should keep the receiving timeline time.");
    const auto path = scratch / "reopened.json";
    scene.dayCycle->playing = true;
    SaveScene(path, scene);
    scene.dayCycle = DayCycleSettings{};
    LoadScene(path, scene);
    Require(scene.dayCycle->hour == 21.25f && !scene.dayCycle->playing && scene.lightmapEnabled == true,
            "Reopen must restore the saved hour, pause playback and retain the lightmap.");
    Require(scene.lightingLab && scene.lightingLab->lamp == WalkerLamp::Spot &&
                scene.lightingLab->toneMap && scene.lightingLab->exposureEv == 1.0f && !scene.normalMaps,
            "Reopening a scene must keep the live comparison controls.");
    scene.dayCycle.reset();
    const auto originalCount = scene.props.size();
    bool rejected = false;
    try { LoadScene(path, scene); } catch (const std::runtime_error&) { rejected = true; }
    Require(rejected && !scene.dayCycle && scene.props.size() == originalCount,
            "Chapter 17 must keep its scene when it rejects a day-cycle file.");
}

}  // namespace

//————————————————————————————————————————————————————————————————————————————————
// 校验曲线并以构建目录中的场景复现存档行为。
//————————————————————————————————————————————————————————————————————————————————
int main(int argc, char** argv) {
    if (argc != 3) return 1;
    try {
        const auto scratch = std::filesystem::path(argv[2]) / "day-cycle-checks";
        std::filesystem::create_directories(scratch);
        CheckDaylight();
        CheckScene(argv[1], scratch);
        std::cout << "Daylight curves, midnight wrap and scene reopen checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
