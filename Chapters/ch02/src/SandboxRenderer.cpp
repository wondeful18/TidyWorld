#include "Chapters/ch02/src/SandboxRenderer.h"

#include "Engine/Renderer/Color.h"
#include "Engine/Renderer/RenderCoordinates.h"
#include "Engine/Renderer/RenderPass.h"
#include "Engine/Renderer/ShaderModule.h"
#include "Engine/Renderer/WebGpuContext.h"
#include "Engine/Scene/WalkerMesh.h"

namespace tinyworld {

//————————————————————————————————————————————————————————————————————————————————
// 第 02 章上传小屋和小车，第 03 章多用一次深度通道，第 04 章加天空贴图。
//————————————————————————————————————————————————————————————————————————————————

SandboxRenderer::SandboxRenderer(WebGpuContext& gpu, WGPUTextureFormat format, const WorldScene& scene,
                                 const std::filesystem::path& root)
    : mGpu(gpu), mLayout(BuildObjectLayout(gpu)), mClear{0.89, 0.925, 0.941, 1.0} {
    const auto shader = LoadWgslModule(gpu, root / "shaders" / "sandbox.wgsl",
        {"output_color.wgsl", "fog.wgsl", "world.wgsl", "shadow.wgsl"});
    if (scene.shadows) mShadowMap.emplace(gpu, shader.get(), mLayout.get());
    if (scene.environment) mSky.emplace(gpu, format, kDepthFormat, root, false);
    mPipeline = BuildPipeline(gpu, format, shader.get(), mLayout.get(),
                              mShadowMap ? mShadowMap->SamplingLayout() : nullptr,
                              mSky ? mSky->Environment().Layout() : nullptr, nullptr);

    for (std::size_t i = 0; i < mObjects.size(); ++i) {
        CarSurfaceRanges surfaces;
        const auto source = i == WorldScene::kCarIndex ? MakeRoamingCarMesh(&surfaces)
                                                       : LoadSceneMesh(root / scene.objects[i].asset);
        mObjects[i] = UploadMesh(gpu, WithReflectivity(source, surfaces), mLayout.get(), scene.objects[i].name);
    }
    if (scene.spawn) mWalker.emplace(UploadMesh(gpu, WithReflectivity(MakeWalkerMesh()), mLayout.get(), "Walker"));
    if (IsSrgbFormat(format)) {
        mClear.r = DisplayToLinear(mClear.r);
        mClear.g = DisplayToLinear(mClear.g);
        mClear.b = DisplayToLinear(mClear.b);
    }
}

//————————————————————————————————————————————————————————————————————————————————
// 先绘制可选的太阳深度，再用相机绘制同一批物体与天空。
//————————————————————————————————————————————————————————————————————————————————

void SandboxRenderer::Encode(WGPUCommandEncoder encoder, WGPUTextureView target, PixelSize size,
                              const WorldScene& scene) {
    mDepth.Resize(mGpu, size);
    const auto& camera = scene.ActiveCamera();
    const auto projection = camera.Projection(static_cast<float>(size.width) / size.height, true);
    const auto view = camera.View();
    const RenderCoordinates coordinates(camera, projection, {});
    const auto towardSun = scene.sun.TowardLight();
    const DrawUniformWriter uniforms{mGpu.Queue(), coordinates,
        DrawUniform{.viewProjection = coordinates.ViewProjection(), .towardSun = {towardSun, 0.0f},
                    .sunRadiance = {scene.sun.color * scene.sun.intensity, 0.0f},
                    .cameraAndReflection = {camera.Position(), scene.environment && scene.environment->carReflection ? 1.0f : 0.0f},
                    .lighting = {1.0f, 0.0f, 0.0f, 0.0f}}};

    const bool shadowed = mShadowMap && scene.shadows->enabled;
    glm::vec4 shadowControls{0.0f};
    if (shadowed) {
        SunShadowFrame frame;
        frame.matrices[0] = SunShadowMap::ViewProjection(towardSun, scene.sun.azimuth);
        frame.eyeAndCount.w = 1.0f;
        mShadowMap->Upload(mGpu.Queue(), frame);
        shadowControls = {1.0f, scene.shadows->pcf ? 1.0f : 0.0f, scene.shadows->depthBias, 0.0f};
    }
    const auto count = scene.IsDriving() || scene.spawn ? mObjects.size() : WorldScene::kCarIndex;
    const bool showWalker = mWalker && !scene.IsDriving();
    for (std::size_t i = 0; i < count; ++i) {
        uniforms.Set(mObjects[i], scene.objects[i].transform.Matrix(), shadowControls);
    }
    if (showWalker) uniforms.Set(*mWalker, scene.WalkerPose().Matrix(), shadowControls);
    const auto drawObjects = [&](WGPURenderPassEncoder pass) {
        for (std::size_t i = 0; i < count; ++i) DrawMesh(pass, mObjects[i]);
        if (showWalker) DrawMesh(pass, *mWalker);
    };

    if (shadowed) {
        auto pass = mShadowMap->Begin(encoder);
        drawObjects(pass.get());
        wgpuRenderPassEncoderEnd(pass.get());
    }

    auto pass = BeginColorPass(encoder, target, mClear, "Sandbox color pass", mDepth.View(), WGPUStoreOp_Discard);
    wgpuRenderPassEncoderSetPipeline(pass.get(), mPipeline.get());
    if (mShadowMap) wgpuRenderPassEncoderSetBindGroup(pass.get(), 1, mShadowMap->SamplingBinding(), 0, nullptr);
    if (mSky) wgpuRenderPassEncoderSetBindGroup(pass.get(), 2, mSky->Environment().Binding(), 0, nullptr);
    drawObjects(pass.get());
    if (mSky) mSky->Draw(pass.get(), projection, view, scene.environment->sky, glm::vec4{0.0f}, 0.0f);
    wgpuRenderPassEncoderEnd(pass.get());
}

}  // namespace tinyworld
