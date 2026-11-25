#include "Engine/Renderer/WorldRenderer.h"

#include "Engine/Renderer/Color.h"
#include "Engine/Renderer/FrameAtmosphere.h"
#include "Engine/Renderer/RenderPass.h"
#include "Engine/Renderer/RenderCoordinates.h"
#include "Engine/Renderer/ShaderModule.h"
#include "Engine/Renderer/WebGpuContext.h"
#include "Engine/Scene/DayCycle.h"
#include "Engine/Scene/RobotMesh.h"
#include "Engine/Scene/RoamingCarMesh.h"
#include "Engine/Scene/SceneMesh.h"

#include <glm/matrix.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <utility>
#include <stdexcept>
#include <vector>

namespace tinyworld {
//————————————————————————————————————————————————————————————————————————————————
// 每个模型上传一次，并单独绑定一个 uniform 以支持编辑器变换。
//————————————————————————————————————————————————————————————————————————————————
WorldRenderer::WorldRenderer(WebGpuContext& gpu, WGPUTextureFormat format,
                             const WorldScene& scene, const std::filesystem::path& root)
    : mGpu(gpu) {
    const auto shaders = root / "shaders";
    if (scene.lightingLab) {
        mToneMapping.emplace(gpu, format, shaders);
        format = ToneMapping::kSceneFormat;
    }
    const auto shader = LoadWgslModule(gpu, shaders / "world.wgsl",
        {"output_color.wgsl", "fog.wgsl", "shadow.wgsl", "pbr.wgsl"});
    mObjectLayout = BuildObjectLayout(gpu);
    const auto layout = mObjectLayout.get();
    const auto vegetationShader = scene.vegetation
        ? LoadWgslModule(gpu, shaders / "vegetation.wgsl",
            {"output_color.wgsl", "fog.wgsl", "world.wgsl", "shadow.wgsl"})
        : ShaderHandle{};
    mShadowMap.emplace(gpu, vegetationShader ? vegetationShader.get() : shader.get(), layout,
                       scene.vegetation.has_value(), scene.capstone);
    mSky.emplace(gpu, format, kDepthFormat, root, scene.dayCycle.has_value());
    if (scene.water) {
        mWater.emplace(gpu, format, mSky->Environment(), root / "shaders" / "water.wgsl", scene.water->coast);
    }
    if (scene.materials) {
        mMaterials.emplace(gpu, root);
        const auto sphere = WithReflectivity(MakeMaterialSphere());
        for (auto& draw : mMaterialSpheres) {
            draw = UploadMesh(mGpu, sphere, layout, "Material comparison sphere");
        }
    }
    const auto terrainShader = scene.materials
        ? LoadWgslModule(gpu, shaders / "terrain.wgsl",
            {"output_color.wgsl", "fog.wgsl", "world.wgsl", "shadow.wgsl", "pbr.wgsl"})
        : ShaderHandle{};
    mPipeline = BuildPipeline(gpu, format, terrainShader ? terrainShader.get() : shader.get(), layout,
                              mShadowMap->SamplingLayout(),
                              mSky->Environment().Layout(),
                              mMaterials ? mMaterials->Layout() : nullptr);
    if (scene.vegetation) {
        mVegetation.emplace(gpu);
        mVegetationUniform = MakeDrawBinding(mGpu, layout);
        mVegetationPipeline = BuildPipeline(gpu, format, vegetationShader.get(), layout,
                                            mShadowMap->SamplingLayout(), nullptr, nullptr, true);
    }

    mClearColor = {0.89, 0.925, 0.941, 1.0};
    if (mToneMapping || IsSrgbFormat(format)) {
        mClearColor.r = DisplayToLinear(mClearColor.r);
        mClearColor.g = DisplayToLinear(mClearColor.g);
        mClearColor.b = DisplayToLinear(mClearColor.b);
    }

    mProps.emplace(gpu, format, scene, root, layout, mShadowMap->SamplingLayout(), mSky->Environment().Layout());
    mRobot = UploadIndexedMesh(mGpu, WithReflectivity(MakeRobotMesh()), layout, "Walker");
    if (scene.lightingLab) {
        mRobotLamp = UploadIndexedMesh(mGpu, WithReflectivity(MakeRobotLamp()), layout, "Walker lamp");
    }
    for (std::size_t i = 0; i < mRobotLimbs.size(); ++i) {
        mRobotLimbs[i] = UploadIndexedMesh(mGpu, WithReflectivity(MakeRobotLimb(static_cast<RobotLimb>(i))), layout, "Robot limb");
    }
    mTerrain.emplace(gpu, layout, scene, mMaterials ? &*mMaterials : nullptr,
                      mVegetation ? &*mVegetation : nullptr);
}

//————————————————————————————————————————————————————————————————————————————————
// 同一帧内物体变换只上传一次。先写入太阳深度，再切换到相机读取阴影。
//————————————————————————————————————————————————————————————————————————————————
void WorldRenderer::Encode(WGPUCommandEncoder encoder, WGPUTextureView target, PixelSize size,
                           const WorldScene& scene) {
    mSceneDraws = 0;
    mDepth.Resize(mGpu, size);
    auto sceneTarget = mToneMapping ? mToneMapping->SceneTarget(size) : target;

    mTerrain->Sync(scene);
    if (mWater) mWater->SyncExtent(scene.water->coast, mTerrain->Grid().HalfExtent());
    auto camera = scene.ActiveCamera();
    if (scene.precision.offsetWorld) camera.target.x += scene.precision.cameraNudge;
    const bool hasCoast = scene.water && scene.water->coast;
    const bool exploration = mTerrain->Grid() == TerrainGrid::Exploration();
    const auto projection = camera.Projection(static_cast<float>(size.width) / size.height, true,
                                              exploration ? 450.0f : hasCoast ? WaterSettings::kCoastFar : 80.0f);
    const glm::vec4 coast{hasCoast ? 1.0f : 0.0f, WaterSettings::kLevel,
                         WaterSettings::kCoastStart, WaterSettings::kCoastEnd};
    const auto view = camera.View();
    const glm::mat4 vp = projection * view;
    const RenderCoordinates coordinates(camera, projection, scene.precision);
    const auto eye = camera.Position();
    const auto light = scene.dayCycle ? EvaluateDaylight(scene.dayCycle->hour) : Daylight{scene.sun};
    const bool toneMap = scene.lightingLab && scene.lightingLab->toneMap;
    const glm::vec4 lighting{light.ambient, light.nightMix, toneMap ? 1.0f : 0.0f, scene.normalMaps ? 1.0f : 0.0f};
    const glm::vec4 horizon = hasCoast
        ? glm::vec4(mSky->Environment().HorizonColor(glm::normalize(camera.target - eye), light.nightMix), 1.0f)
        : glm::vec4{0.0f};
    FrameAtmosphere air;
    if (scene.atmosphere) {
        const auto& settings = *scene.atmosphere;
        air.colorDensity = {glm::vec3(horizon), settings.fogEnabled ? settings.fogDensity : 0.0f};
        air.distances = {settings.fogStart, settings.shadowDistance * AtmosphereSettings::kShadowFadeRatio,
                         settings.shadowDistance, 0.0f};
    }
    const bool reflectCar = scene.environment->carReflection;
    const glm::vec4 cameraAndReflection(eye, reflectCar ? 1.0f : 0.0f);
    const auto towardSun = light.sun.TowardLight();
    const glm::vec4 direction(towardSun, 0.0f);
    const glm::vec4 sun(light.sun.color * light.sun.intensity, 0.0f);
    SunShadowFrame shadowFrame;
    glm::vec4 shadowControls{0.0f};
    // 当夜间直射已经为零，本帧不需要更新太阳深度图，接收端也不再查询它。
    const bool renderShadow = scene.shadows->enabled &&
                              (!scene.dayCycle || light.sun.intensity > 0.0f);
    if (renderShadow) {
        constexpr float shadowScale = HeightField::kHalfExtent * (3.0f / 16.0f);
        if (exploration && scene.precision.cascades) {
            shadowFrame = SunShadowMap::FitCascades(eye, glm::normalize(camera.target - eye),
                                                    projection, towardSun, light.sun.azimuth);
        } else {
            shadowFrame.matrices[0] = exploration
                ? SunShadowMap::FitCascades(eye, glm::normalize(camera.target - eye), projection,
                                            towardSun, light.sun.azimuth).matrices.back()
                : SunShadowMap::ViewProjection(towardSun, light.sun.azimuth, shadowScale);
            shadowFrame.eyeAndCount.w = 1.0f;
        }
        mShadowMap->Upload(mGpu.Queue(), shadowFrame);
        shadowControls = {1.0f, scene.shadows->pcf ? 1.0f : 0.0f, scene.shadows->depthBias, 0.0f};
    }

    const bool showSpheres = scene.materials && scene.materials->spheres && !scene.storage;
    const bool showVegetation = mVegetation && scene.vegetation && scene.vegetation->visible && !showSpheres;
    const bool showRobot = scene.spawn && !showSpheres && (!scene.storage ||
        scene.terrain->TryHeight(scene.IsWalking() ? glm::vec2{scene.WalkerPose().position.x, scene.WalkerPose().position.z}
                                                   : scene.spawn->groundXZ).has_value());
    DrawUniform frame{.viewProjection = coordinates.ViewProjection(), .towardSun = direction,
                      .sunRadiance = sun, .cameraAndReflection = cameraAndReflection,
                      .coast = coast, .air = air, .lighting = lighting};
    const auto robot = showRobot ? scene.WalkerPose().Matrix() : glm::mat4{1.0f};
    const auto gait = scene.WalkerGait();
    const bool showLamp = showRobot && scene.lightingLab && scene.lightingLab->lamp != WalkerLamp::Off;
    glm::mat4 lampModel{1.0f};
    if (showLamp) {
        const auto hand = robot * RobotLimbPose(RobotLimb::RightArm, gait);
        lampModel = glm::translate(hand, glm::vec3{-0.055f, -0.18f, 0.26f});
        const auto forward = glm::normalize(glm::vec3(robot * glm::vec4{0.0f, -0.52f, 1.0f, 0.0f}));
        const bool spot = scene.lightingLab->lamp == WalkerLamp::Spot;
        frame.lampPositionAndPower = {glm::vec3(lampModel[3]), 8.0f};
        frame.lampForwardAndCone = {forward, spot ? 0.8191520f : -1.0f}; // cos(35°)
    }
    const DrawUniformWriter uniforms{mGpu.Queue(), coordinates, frame};
    if (showLamp) uniforms.Set(*mRobotLamp, lampModel, shadowControls, SurfaceParameters{.emission = 3.0f});
    if (showRobot) {
        uniforms.Set(mRobot, robot, shadowControls);
        for (std::size_t i = 0; i < mRobotLimbs.size(); ++i) {
            uniforms.Set(mRobotLimbs[i], robot * RobotLimbPose(static_cast<RobotLimb>(i), gait), shadowControls);
        }
    }
    if (!showSpheres) mProps->Prepare(scene, *mTerrain, uniforms, shadowControls);
    mTerrain->Prepare(scene, vp, eye, uniforms, shadowControls, showSpheres);
    if (showSpheres) {
        constexpr float radius = 0.83f;
        for (std::size_t i = 0; i < mMaterialSpheres.size(); ++i) {
            const float x = -3.24f + 2.16f * static_cast<float>(i);
            constexpr float z = 8.0f;
            const float y = scene.terrain->HeightAt({x, z}) + radius;
            glm::mat4 model{radius};
            model[3] = {x, y, z, 1.0f};
            const SurfaceParameters settings{.useSampleMaterial = 1, .sampleMetallic = i < 2 ? 0.0f : 1.0f, .sampleRoughness = i % 2 == 0 ? 0.24f : 0.62f};
            uniforms.Set(mMaterialSpheres[i], model, shadowControls, settings);
        }
    }
    mVegetationStats = {};
    if (mVegetation) {
        mVegetationStats.instances = mVegetation->Counts();
    }
    if (showVegetation) {
        uniforms.Set(mVegetationUniform, glm::mat4{1.0f}, shadowControls);
    }
    for (unsigned layer = 0; layer < static_cast<unsigned>(shadowFrame.eyeAndCount.w); ++layer) {
        auto depthPass = mShadowMap->Begin(encoder, layer);
        const auto casterFrustum = MakeTerrainFrustum(shadowFrame.matrices[layer]);
        DrawObjects(depthPass.get(), showRobot, showSpheres, true, exploration ? &casterFrustum : nullptr);
        if (!showSpheres) mSceneDraws += mProps->Draw(depthPass.get(), PropRenderer::Pass::Shadows);
        if (showVegetation && layer == 0) {
            wgpuRenderPassEncoderSetPipeline(depthPass.get(), mShadowMap->VegetationPipeline());
            wgpuRenderPassEncoderSetBindGroup(depthPass.get(), 0, mVegetationUniform.binding.get(), 0, nullptr);
            mVegetationStats.shadowDraws = mTerrain->DrawVegetation(depthPass.get(), scene.vegetation->instanced, true);
            mSceneDraws += mVegetationStats.shadowDraws;
        }
        wgpuRenderPassEncoderEnd(depthPass.get());
    }

    const bool showWater = mWater && scene.water && scene.water->view != WaterView::Off && !showSpheres;
    auto pass = BeginColorPass(encoder, sceneTarget, mClearColor, "World color pass", mDepth.View(),
                               showWater ? WGPUStoreOp_Store : WGPUStoreOp_Discard);
    wgpuRenderPassEncoderSetPipeline(pass.get(), mPipeline.get());
    wgpuRenderPassEncoderSetBindGroup(pass.get(), 1, mShadowMap->SamplingBinding(), 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(pass.get(), 2, mSky->Environment().Binding(), 0, nullptr);
    if (mMaterials) {
        wgpuRenderPassEncoderSetBindGroup(pass.get(), 3, mMaterials->Binding(), 0, nullptr);
    }
    DrawObjects(pass.get(), showRobot, showSpheres, false);
    if (showLamp) DrawMesh(pass.get(), *mRobotLamp);
    if (!showSpheres) mSceneDraws += mProps->Draw(pass.get(), PropRenderer::Pass::BuiltinColor);
    mSceneDraws += mTerrain->DrawOverlays(pass.get());
    if (!showSpheres) mSceneDraws += mProps->Draw(pass.get(), PropRenderer::Pass::GltfColor);
    if (showVegetation) {
        wgpuRenderPassEncoderSetPipeline(pass.get(), mVegetationPipeline.get());
        wgpuRenderPassEncoderSetBindGroup(pass.get(), 0, mVegetationUniform.binding.get(), 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(pass.get(), 1, mShadowMap->SamplingBinding(), 0, nullptr);
        mVegetationStats.colorDraws = mTerrain->DrawVegetation(pass.get(), scene.vegetation->instanced, false);
        mSceneDraws += mVegetationStats.colorDraws;
    }
    mSky->Draw(pass.get(), projection, view, scene.environment->sky, horizon, light.nightMix);
    ++mSceneDraws;
    wgpuRenderPassEncoderEnd(pass.get());
    if (showWater) {
        mWater->Encode(encoder, sceneTarget, mDepth.View(), coordinates.ViewProjection(), glm::vec3(coordinates.Model(glm::mat4{1.0f})[3]),
                       eye, *scene.water, glm::vec3(horizon), air, lighting);
        ++mSceneDraws;
    }
    if (mToneMapping) {
        mToneMapping->Encode(encoder, target, toneMap, scene.lightingLab->exposureEv);
        ++mSceneDraws;
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 两个通道读取同一份顶点与物体变换，保证角色和车辆也能投影。
//————————————————————————————————————————————————————————————————————————————————
void WorldRenderer::DrawObjects(WGPURenderPassEncoder pass, bool showRobot,
                                bool showSpheres, bool shadows, const TerrainFrustum* shadowFrustum) {
    mSceneDraws += mTerrain->Draw(pass, shadows, shadowFrustum);
    if (showSpheres) for (const auto& sphere : mMaterialSpheres) DrawMesh(pass, sphere);
    if (showRobot) {
        DrawMesh(pass, mRobot);
        for (const auto& limb : mRobotLimbs) DrawMesh(pass, limb);
    }
}

void WorldRenderer::DrawMesh(WGPURenderPassEncoder pass, const Drawable& mesh, WGPUBindGroup instance) {
    tinyworld::DrawMesh(pass, mesh, instance);
    ++mSceneDraws;
}

}  // namespace tinyworld
