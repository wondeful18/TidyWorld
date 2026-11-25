#include "Chapters/ch02/src/SandboxPresets.h"

#include "Engine/Scene/SceneFile.h"

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 只有第五章载入平地存档。未指定存档时沿用小屋示例。
//————————————————————————————————————————————————————————————————————————————————
WorldScene CreateSandboxScene(const ApplicationOptions& options) {
    WorldScene scene;
    if (options.chapter >= Chapter::SunShadows) scene.shadows = options.shadowOverrides.value_or(ShadowSettings{});
    if (options.chapter >= Chapter::Sky) scene.environment = options.environmentOverrides.value_or(EnvironmentSettings{});
    if (options.chapter == Chapter::Walk) scene.spawn.emplace();
    if (options.scenePath.empty()) {
        scene.ApplyPreset(options.worldPreset);
    } else {
        LoadScene(options.scenePath, scene);
        if (options.shadowOverrides) scene.shadows = options.shadowOverrides;
        if (options.environmentOverrides) scene.environment = options.environmentOverrides;
        scene.SetDriving(options.worldPreset == WorldPreset::Drive);
        scene.SetWalking(options.worldPreset == WorldPreset::Walk);
    }
    return scene;
}

}  // namespace tinyworld
