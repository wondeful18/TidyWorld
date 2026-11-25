#include "Editor/ChapterPresets.h"

#include "Chapters/ch02/src/SandboxPresets.h"

#include "Engine/Scene/Capstone.h"
#include "Engine/Scene/DayCycle.h"
#include "Engine/Scene/SceneFile.h"

#include <glm/common.hpp>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 复用小屋与小车预设，并在创建 GPU 资源前确定本章是否提供阴影。
//————————————————————————————————————————————————————————————————————————————————
WorldScene CreateWorldScene(const ApplicationOptions& options, const std::filesystem::path& root) {
    if (options.chapter < Chapter::Terrain) return CreateSandboxScene(options);
    WorldScene scene;
    scene.shadows = options.shadowOverrides.value_or(ShadowSettings{});
    if (!options.shadowOverrides) scene.shadows->depthBias = 0.0012f;
    scene.environment = options.environmentOverrides.value_or(EnvironmentSettings{});
    scene.spawn.emplace();
    scene.terrain.emplace();
    scene.canPreviewLod = options.chapter == Chapter::Terrain;
    scene.previewLod = options.lodPreview;
    scene.canSculpt = options.chapter >= Chapter::Sculpt;
    scene.showChunkBorders = options.chunkBorders;
    auto scenePath = options.scenePath;
    if (options.chapter >= Chapter::Materials) {
        scene.materials.emplace();
        scene.paint.emplace();
        if (scenePath.empty() && options.worldPreset != WorldPreset::Overlap && options.worldPreset != WorldPreset::Evening) {
            scenePath = root / "scenes" / "ch07" / "valley.json";
        }
    }
    if (options.chapter >= Chapter::Road) {
        scene.road.emplace();
    }
    scene.canPlaceProps = options.chapter >= Chapter::Props;
    scene.canUseGltfProps = options.chapter >= Chapter::Village;
    if (scene.canUseGltfProps && options.scenePath.empty()) {
        scenePath = options.chapter >= Chapter::Coast ? root / "scenes" / "ch15" / "coast.json"
                                                       : root / "scenes" / "ch13" / "village.json";
    }
    if (options.chapter >= Chapter::Meadow) {
        scene.vegetation.emplace();
        if (options.chapter >= Chapter::Ecology) scene.vegetation->mode = VegetationMode::TerrainRules;
    }
    if (options.chapter >= Chapter::Water) {
        scene.water.emplace();
        scene.water->coast = options.chapter >= Chapter::Coast;
        scene.water->view = options.waterView.value_or(WaterView::Lit);
        scene.water->seconds = options.waterTime.value_or(0.0f);
    }
    if (options.chapter >= Chapter::Atmosphere) {
        scene.atmosphere.emplace();
        if (options.fogEnabled) scene.atmosphere->fogEnabled = *options.fogEnabled;
        if (options.shadowDistance) scene.atmosphere->shadowDistance = *options.shadowDistance;
    }
    if (options.chapter >= Chapter::Lightmap) {
        scene.lightmapEnabled = options.lightmapEnabled.value_or(true);
        if (options.scenePath.empty()) scenePath = root / "scenes" / "ch17" / "porch.json";
    }
    if (options.chapter >= Chapter::DayCycle) scene.dayCycle.emplace();
    scene.capstone = options.chapter == Chapter::Capstone;
    scene.streamTerrain = options.chapter >= Chapter::TerrainStreaming;
    if (options.scenePath.empty()) {
        if (scene.capstone) {
            scenePath = !options.tourVerificationPath.empty()
                ? root / "scenes" / "ch21" / "tour.json" : root / "worlds" / "exploration";
        } else if (scene.streamTerrain) {
            scenePath = root / "worlds" / "valley";
        }
    }
    if (options.chapter >= Chapter::TerrainVisibility) {
        scene.terrainPreview = TerrainPreview{options.terrainLod.value_or(true), options.terrainCull.value_or(true)};
    }
    if (scenePath.empty()) {
        scene.ApplyPreset(options.worldPreset);
    } else {
        LoadScene(scenePath, scene);
        if (scene.atmosphere && options.fogEnabled) scene.atmosphere->fogEnabled = *options.fogEnabled;
        if (scene.atmosphere && options.shadowDistance) scene.atmosphere->shadowDistance = *options.shadowDistance;
        if (scene.vegetation && options.scenePath.empty() && !scene.canUseGltfProps) {
            scene.camera = {{0.0f, 0.5f, 2.0f}, 13.0f, 30.0f, 38.0f};
        }
        if (options.shadowOverrides) {
            scene.shadows = options.shadowOverrides;
        }
        if (options.environmentOverrides) {
            scene.environment = options.environmentOverrides;
        }
        if (scene.dayCycle && !scene.capstone && options.scenePath.empty() && options.worldPreset == WorldPreset::Default) {
            scene.camera = WorldScene::kDayCamera;
        }
        if (options.worldPreset == WorldPreset::Porch) scene.camera = WorldScene::kPorchCamera;
        if (options.worldPreset == WorldPreset::Shore) scene.camera = WorldScene::kShoreCamera;
        scene.SetDriving(options.worldPreset == WorldPreset::Drive);
        scene.SetWalking(options.worldPreset == WorldPreset::Walk);
        if (scene.materials) {
            scene.materials->spheres = options.worldPreset == WorldPreset::Materials;
        }
    }
    if (scene.terrainPreview && !scene.IsWalking()) {
        if (options.terrainView) scene.camera = WorldScene::kTerrainRoute[*options.terrainView];
        else if (!scene.capstone && options.scenePath.empty() && options.worldPreset == WorldPreset::Default) {
            scene.camera = WorldScene::kTerrainRoute[1];
        }
    }
    if (options.worldPreset == WorldPreset::Bridge) scene.camera = WorldScene::kBridgeCamera;
    if (options.tourStop) scene.camera = kTourStops[*options.tourStop].camera;
    if (scene.dayCycle && options.dayHour) scene.dayCycle->hour = WrapDayHour(*options.dayHour);
    scene.normalMaps = options.normalMaps.value_or(true);
    if (options.chapter == Chapter::DayCycle) scene.lightingLab = options.lightingLab;
    return scene;
}

//————————————————————————————————————————————————————————————————————————————————
// 保留三角形直绘标题，并为各章提供自己的启动日志。
//————————————————————————————————————————————————————————————————————————————————
ChapterPresentation DescribeChapter(const ApplicationOptions& options) {
    const char* title = options.direct ? "TinyWorld ch01 - Direct Triangle" : "TinyWorld ch01 - Editor";
    const char* ready = "WGSL and triangle pipeline validated";
    if (options.chapter == Chapter::World) {
        title = "TinyWorld ch02 - Cottage, Tree and Sun";
        ready = "Chapter 02 meshes, Lambert pipeline and depth state ready";
    } else if (options.chapter == Chapter::SunShadows) {
        title = "TinyWorld ch03 - Sunlight and Shadows";
        ready = "Chapter 03 meshes, sun depth and world color pipelines ready";
    } else if (options.chapter == Chapter::Sky) {
        title = "TinyWorld ch04 - Sky and Environment";
        ready = "Chapter 04 cubemap, panorama, sun depth and world pipelines ready";
    } else if (options.chapter == Chapter::Walk) {
        title = "TinyWorld ch05 - Scene Files and Walking";
        ready = "Chapter 05 scene and third-person world pipelines ready";
    } else if (options.chapter == Chapter::Terrain) {
        title = "TinyWorld ch06 - Heightfield and Robot Walk";
        ready = "Chapter 06 heightfield chunks and robot ready";
    } else if (options.chapter == Chapter::Sculpt) {
        title = "TinyWorld ch07 - Sculpt a Valley";
        ready = "Chapter 07 sculpting and version 2 scenes ready";
    } else if (options.chapter == Chapter::Materials) {
        title = "TinyWorld ch08 - Terrain Materials";
        ready = "Chapter 08 PBR terrain and material samples ready";
    } else if (options.chapter == Chapter::Road) {
        title = "TinyWorld ch09 - Spline Roads";
        ready = "Chapter 09 spline road and planting mask ready";
    } else if (options.chapter == Chapter::Props) {
        title = "TinyWorld ch10 - Place Scene Objects";
        ready = "Chapter 10 prop placement and transform gizmos ready";
    } else if (options.chapter == Chapter::Meadow) {
        title = "TinyWorld ch11 - Grass, Treeline and Instancing";
        ready = "Chapter 11 grass cards, trees and instancing ready";
    } else if (options.chapter == Chapter::Ecology) {
        title = "TinyWorld ch12 - Terrain Ecology";
        ready = "Chapter 12 seeded terrain vegetation and road exclusion ready";
    } else if (options.chapter == Chapter::Village) {
        title = "TinyWorld ch13 - Village Assets and Footbridge";
        ready = "Chapter 13 glTF meshes, PBR textures and bridge walking ready";
    } else if (options.chapter == Chapter::Water) {
        title = "TinyWorld ch14 - Lake, Stream and Shore";
        ready = "Chapter 14 opaque scene depth and water pipeline ready";
    } else if (options.chapter == Chapter::Coast) {
        title = "TinyWorld ch15 - Estuary and Coast";
        ready = "Chapter 15 coastal terrain, ocean waves and shore route ready";
    } else if (options.chapter == Chapter::Atmosphere) {
        title = "TinyWorld ch16 - Sun, Shadows and Distance Fog";
        ready = "Chapter 16 shared sky color, shadow distance and linear fog ready";
    } else if (options.chapter == Chapter::Lightmap) {
        title = "TinyWorld ch17 - Bake a Lightmap";
        ready = "Chapter 17 CPU-baked porch lightmap and UV2 ready";
    } else if (options.chapter == Chapter::DayCycle) {
        title = "TinyWorld ch18 - Day and Night";
        ready = "Chapter 18 shared day-night sky, reflections and solar shadows ready";
    }
    if (options.chapter == Chapter::TerrainVisibility) {
        title = "TinyWorld ch19 - Terrain LOD and Frustum Culling";
        ready = "Chapter 19 stitched LOD and camera terrain culling ready";
    }
    if (options.chapter == Chapter::TerrainStreaming) {
        title = "TinyWorld ch20 - Stream Terrain Chunks";
        ready = "Chapter 20 background terrain, road and plant streaming ready";
    }
    if (options.chapter == Chapter::Capstone) {
        title = "TinyWorld ch21 - Forest to Cape";
        ready = "Chapter 21 expanded coast tour ready";
    }
    return {title, ready};
}

}  // namespace tinyworld
