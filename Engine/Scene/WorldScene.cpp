#include "Engine/Scene/WorldScene.h"

#include <glm/trigonometric.hpp>

#include <cmath>
#include <utility>

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 把可编辑的方位与仰角转为三维单位方向。
//————————————————————————————————————————————————————————————————————————————————
glm::vec3 DirectionalSun::TowardLight() const {
    const float yaw = glm::radians(azimuth);
    const float pitch = glm::radians(elevation);
    return {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
}

//————————————————————————————————————————————————————————————————————————————————
// 预设从默认布局出发，分别演示遮挡、日照和小车漫游。
//————————————————————————————————————————————————————————————————————————————————
void WorldScene::ApplyPreset(WorldPreset preset) {
    const auto shadowSettings = shadows;
    const auto environmentSettings = environment;
    const bool canWalk = spawn.has_value();
    auto heightField = std::move(terrain);
    const bool borders = showChunkBorders;
    const bool lodAvailable = canPreviewLod;
    const bool lod = previewLod;
    const bool sculpting = canSculpt;
    const auto materialView = materials;
    auto paintMap = std::move(paint);
    auto roadLayout = std::move(road);
    const bool placeProps = canPlaceProps;
    const bool gltfProps = canUseGltfProps;
    auto placedProps = std::move(props);
    const auto vegetationSettings = vegetation;
    const auto waterSettings = water;
    const auto atmosphereSettings = atmosphere;
    const auto lightmapSetting = lightmapEnabled;
    *this = WorldScene{};
    // 场景预设用于对照布局及太阳方向，沿用当前阴影设置。
    shadows = shadowSettings;
    environment = environmentSettings;
    terrain = std::move(heightField);
    showChunkBorders = borders;
    canPreviewLod = lodAvailable;
    previewLod = lod;
    canSculpt = sculpting;
    materials = materialView;
    paint = std::move(paintMap);
    road = std::move(roadLayout);
    canPlaceProps = placeProps;
    canUseGltfProps = gltfProps;
    props = std::move(placedProps);
    vegetation = vegetationSettings;
    water = waterSettings;
    atmosphere = atmosphereSettings;
    lightmapEnabled = lightmapSetting;
    if (materials) {
        materials->spheres = false;
    }
    if (canWalk) {
        spawn.emplace();
    }
    if (environment) {
        // 让天空演示在初始视角里也能看到远处地平线。
        camera.elevation = 7.0f;
    }
    if (terrain) {
        spawn = WalkSpawn{{TerrainTrailX(10.4f), 10.4f}, 180.0f};
        camera = {{0.0f, 0.5f, 0.0f}, 18.0f, 36.0f, 56.0f};
        objects[1].transform.position = {4.2f, 0.0f, 1.55f};
        objects[2].transform.position = {-3.85f, 0.0f, 3.6f};
        objects[kCarIndex].transform.position = {-2.3f, 0.0f, 6.1f};
    }
    switch (preset) {
    case WorldPreset::Default:
        break;
    case WorldPreset::Overlap:
        objects[1].transform.position = {0.27f, 0.0f, 2.82f};
        break;
    case WorldPreset::Evening:
        sun = {121.0f, 18.0f, 1.12f, {1.0f, 0.73f, 0.48f}};
        break;
    case WorldPreset::Drive:
        SetDriving(true);
        break;
    case WorldPreset::Walk:
        SetWalking(true);
        break;
    case WorldPreset::Porch:
        camera = kPorchCamera;
        break;
    case WorldPreset::Shore:
        camera = kShoreCamera;
        break;
    case WorldPreset::Materials:
        if (materials) {
            materials->spheres = true;
        }
        break;
    }
    if (terrain) {
        for (std::size_t i = kGroundIndex + 1; i < objects.size(); ++i) {
            auto& pos = objects[i].transform.position;
            pos.y = terrain->HeightAt({pos.x, pos.z});
        }
        // 小屋地基略嵌入草坡，避免地面从较低的一侧露出整条缝。
        objects[2].transform.position.y -= 0.16f;
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 地标继续接触原地的地面，房屋下沉量和编辑器设置的高度偏移得以保留。
//————————————————————————————————————————————————————————————————————————————————
void WorldScene::SculptTerrain(glm::vec2 center, const TerrainBrush& brush, float seconds) {
    if (!canSculpt || !terrain || IsWalking()) {
        return;
    }
    const auto before = *terrain;
    if (terrain->ApplyBrush(center, brush, seconds)) {
        UpdateGroundedObjects(before);
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 恢复快照时按高度差移动现有布景，保留其他编辑。
//————————————————————————————————————————————————————————————————————————————————
void WorldScene::RestoreTerrain(const HeightField::Samples& samples) {
    const auto before = *terrain;
    terrain->ReplaceSamples(samples);
    UpdateGroundedObjects(before);
}

//————————————————————————————————————————————————————————————————————————————————
// 地形变化后保持原有地标和手摆物体各自的离地高度。
//————————————————————————————————————————————————————————————————————————————————
void WorldScene::UpdateGroundedObjects(const HeightField& previous) {
    const auto conform = [&](glm::vec3& p) {
        const glm::vec2 xz{p.x, p.z};
        p.y += terrain->HeightAt(xz) - previous.HeightAt(xz);
    };
    for (std::size_t i = kGroundIndex + 1; i < objects.size(); ++i) {
        conform(objects[i].transform.position);
    }
    for (auto& prop : props) {
        conform(prop.transform.position);
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 只有进入漫游时才初始化小车，退出后恢复使用编辑器相机。
//————————————————————————————————————————————————————————————————————————————————
void WorldScene::SetDriving(bool active) {
    if (active && !mCar && !terrain) {
        SetWalking(false);
        auto& body = objects[kCarIndex].transform;
        if (spawn) {
            mParkedCar = body;
        }
        mCar.emplace(body, environment ? 8.0f : 24.0f, mParkedCar.value_or(RoamingCar::kDefaultStart));
    } else if (!active) {
        mCar.reset();
        if (mParkedCar) {
            objects[kCarIndex].transform = *mParkedCar;
            mParkedCar.reset();
        }
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 漫游开启时把控制器结果写到与渲染器共享的车身变换。
//————————————————————————————————————————————————————————————————————————————————
void WorldScene::UpdateDrive(CarInput input, float deltaSeconds) {
    if (mCar) {
        mCar->Update(objects[kCarIndex].transform, input, deltaSeconds);
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 两种游览共用视口输入，同一时刻只启用一种控制器。
//————————————————————————————————————————————————————————————————————————————————
void WorldScene::SetWalking(bool active) {
    if (active && spawn && !mWalker) {
        if (materials) {
            materials->spheres = false;
        }
        SetDriving(false);
        mWalker.emplace(*spawn, objects[kGroundIndex].transform, Terrain(), props);
    } else if (!active) {
        mWalker.reset();
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 使用当前草台变换和出生点推进步行。
//————————————————————————————————————————————————————————————————————————————————
void WorldScene::UpdateWalk(WalkInput input, double deltaSeconds) {
    if (mWalker) {
        mWalker->Update(*spawn, objects[kGroundIndex].transform, input, deltaSeconds, Terrain(), props);
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 预览与步行沿用同一个脚底坐标转换，保证进入时从预览位置出发。
//————————————————————————————————————————————————————————————————————————————————
Transform WorldScene::WalkerPose() const {
    return mWalker ? mWalker->Pose() : spawn->WorldPose(objects[kGroundIndex].transform, Terrain(), props);
}

//————————————————————————————————————————————————————————————————————————————————
// 游览结束后直接使用原编辑相机。
//————————————————————————————————————————————————————————————————————————————————
const OrbitCamera& WorldScene::ActiveCamera() const {
    if (mWalker) {
        return mWalker->Camera();
    }
    if (materials && materials->spheres) {
        // 从南侧观察沿 X 排列的四颗球，固定机位便于比较高光。
        static constexpr OrbitCamera comparison{{0.0f, -0.15f, 8.0f}, 0.0f, 9.0f, 11.4f};
        return comparison;
    }
    return mCar ? mCar->Camera() : camera;
}

}  // namespace tinyworld
