//————————————————————————————————————————————————————————————————————————————————
// 相机矩阵、观察位置、时间和对照设置。布局与 C++ 的 WaterUniform 一致。
//————————————————————————————————————————————————————————————————————————————————
struct WaterUniform {
    viewProjection: mat4x4<f32>,         // 绘制平面使用的相机投影
    inverseViewProjection: mat4x4<f32>,  // 从采样深度重建世界位置
    cameraAndTime: vec4<f32>,            // 观察位置与流动弧度相位
    controls: vec4<f32>,                 // 深度观察、反射开关与对齐填充
}

@group(0) @binding(0) var<uniform> water: WaterUniform;
@group(0) @binding(1) var opaqueDepth: texture_depth_2d;
@group(1) @binding(0) var environmentCube: texture_cube<f32>;
@group(1) @binding(1) var environmentSampler: sampler;

//————————————————————————————————————————————————————————————————————————————————
// 片元阶段使用像素位置查场景深度，使用世界位置生成波纹与反射。
//————————————————————————————————————————————————————————————————————————————————
struct WaterVertex {
    @builtin(position) position: vec4<f32>,  // 片元阶段的 xy 为 framebuffer 像素坐标
    @location(0) world: vec3<f32>,           // 水面插值后的世界坐标
}

//————————————————————————————————————————————————————————————————————————————————
// 水面的几何保持水平，波纹只扰动用于反射的法线。
//————————————————————————————————————————————————————————————————————————————————
@vertex
fn vsWater(@location(0) position: vec3<f32>) -> WaterVertex {
    return WaterVertex(water.viewProjection * vec4<f32>(position, 1.0), position);
}

//————————————————————————————————————————————————————————————————————————————————
// 用同像素的不透明深度遮挡水，反投影后用世界高度差衰减颜色。
//————————————————————————————————————————————————————————————————————————————————
fn shadeWater(input: WaterVertex) -> vec4<f32> {
    let dimensions = textureDimensions(opaqueDepth);
    let pixel = min(vec2<i32>(input.position.xy), vec2<i32>(dimensions) - vec2<i32>(1));
    let sceneDepth = textureLoad(opaqueDepth, pixel, 0);
    if (sceneDepth <= input.position.z) {
        discard;
    }

    // WebGPU 深度已经是 0–1，像素 Y 向下，裁剪空间 Y 向上。
    let ndc = input.position.xy / vec2<f32>(dimensions) * vec2<f32>(2.0, -2.0) + vec2<f32>(-1.0, 1.0);
    let bottom = water.inverseViewProjection * vec4<f32>(ndc, sceneDepth, 1.0);
    let depthM = select(clamp(input.world.y - bottom.y / bottom.w, 0.0, 4.0), 4.0,
                        sceneDepth >= 0.999999);
    if (water.controls.x > 0.5) {
        let shade = clamp(depthM / 2.0, 0.0, 1.0);
        return vec4<f32>(vec3<f32>(shade), 1.0);
    }

    // 湖心的法线缓慢起伏，窄溪把波纹沿 +X 推向湖面。
    let offset = (input.world.x - 10.0) / 5.8;
    let lake = exp(-offset * offset);
    let phase = water.cameraAndTime.w;
    let along = input.world.x * 3.9 + input.world.z * 1.7;
    let longWave = mix(sin(along - 19.0 * phase), sin(along - 4.0 * phase), lake);
    let cross = input.world.z * 6.8 + input.world.x * 0.83 + 0.5 * longWave;
    let crossWave = mix(cos(cross - 4.0 * phase), cos(cross - phase), lake);
    let normal = normalize(vec3<f32>(0.055 * longWave, 1.0, 0.047 * crossWave));
    let toEye = normalize(water.cameraAndTime.xyz - input.world);
    let reflectDir = reflect(-toEye, normal);
    let reflected = textureSampleLevel(environmentCube, environmentSampler, reflectDir, 0.0).rgb;
    let fresnel = 0.025 + 0.65 * pow(1.0 - clamp(dot(toEye, normal), 0.0, 1.0), 3.7);
    let attenuation = 1.0 - exp(-depthM * 1.7);
    let shallow = vec3<f32>(0.11, 0.49, 0.42);
    let deep = vec3<f32>(0.017, 0.16, 0.21);
    var color = mix(mix(shallow, deep, attenuation), reflected, fresnel * water.controls.y);
    let foamWave = 2.0 * cross + 1.3 * longWave;
    let ripple = 0.5 + 0.5 * mix(sin(foamWave - 8.0 * phase), sin(foamWave - 2.0 * phase), lake);
    let foam = (1.0 - smoothstep(0.035, 0.18, depthM)) * smoothstep(0.50, 0.92, ripple);
    color = mix(color, vec3<f32>(0.72, 0.88, 0.87), foam * 0.80);
    let alpha = clamp(0.30 + 0.62 * attenuation + 0.55 * foam + fresnel * water.controls.y, 0.0, 1.0);
    return vec4<f32>(color, alpha);
}

//————————————————————————————————————————————————————————————————————————————————
// sRGB 附件使用线性源颜色进行混合。
//————————————————————————————————————————————————————————————————————————————————
@fragment
fn fsWaterLinear(input: WaterVertex) -> @location(0) vec4<f32> {
    return shadeWater(input);
}

//————————————————————————————————————————————————————————————————————————————————
// 编辑器的普通 Unorm 附件保存已编码的显示值，与不透明颜色使用同一约定。
//————————————————————————————————————————————————————————————————————————————————
@fragment
fn fsWaterDisplay(input: WaterVertex) -> @location(0) vec4<f32> {
    let linear = shadeWater(input);
    let encoded = select(12.92 * linear.rgb, 1.055 * pow(linear.rgb, vec3<f32>(1.0 / 2.4)) - 0.055,
                         linear.rgb > vec3<f32>(0.0031308));
    return vec4<f32>(encoded, linear.a);
}
