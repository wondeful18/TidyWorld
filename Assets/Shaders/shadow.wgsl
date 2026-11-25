@group(1) @binding(0) var sunDepth: texture_depth_2d_array;
@group(1) @binding(1) var sunDepthSampler: sampler_comparison;

struct SunShadowFrame {
    matrices: array<mat4x4<f32>, 4>,
    splits: vec4<f32>,
    eyeAndCount: vec4<f32>,
    forward: vec4<f32>,
}
@group(1) @binding(2) var<uniform> shadowFrame: SunShadowFrame;
@group(1) @binding(3) var<uniform> shadowDepthMatrix: mat4x4<f32>;

//————————————————————————————————————————————————————————————————————————————————
// 太阳通道只需要位置，GPU 将可见表面的深度写入附件。
//————————————————————————————————————————————————————————————————————————————————
@vertex
fn vsSunDepth(@location(0) position: vec3<f32>) -> @builtin(position) vec4<f32> {
    return shadowDepthMatrix * draw.model * vec4<f32>(position, 1.0);
}

//————————————————————————————————————————————————————————————————————————————————
// 比较接收面深度和太阳看到的第一层表面。PCF 对九次比较结果取平均。
//————————————————————————————————————————————————————————————————————————————————
fn sunVisibility(input: VertexOutput) -> f32 {
    if (draw.shadowControls.x < 0.5) {
        return 1.0;
    }

    // 在相机接收距离的末段把阴影淡向全亮，范围外无需读取深度图。
    var fade = 0.0;
    if (draw.air.distances.z > 0.0) {
        let eyeDistance = distance(draw.cameraAndReflection.xyz, input.worldPosition);
        fade = smoothstep(draw.air.distances.y, draw.air.distances.z, eyeDistance);
        if (fade >= 1.0) {
            return 1.0;
        }
    }
    var layer = 0;
    if shadowFrame.eyeAndCount.w > 1.0 {
        let depth = dot(input.worldPosition - shadowFrame.eyeAndCount.xyz, shadowFrame.forward.xyz);
        layer = select(0, 1, depth > shadowFrame.splits.x) + select(0, 1, depth > shadowFrame.splits.y) +
                select(0, 1, depth > shadowFrame.splits.z);
        if depth > shadowFrame.splits.w { return 1.0; }
    }
    let lightClip = shadowFrame.matrices[layer] * vec4<f32>(input.worldPosition, 1.0);
    let ndc = lightClip.xyz / lightClip.w;
    // WebGPU 投影的 Z 已在 0–1，纹理 V 从上到下，需翻转 Y。
    let uv = ndc.xy * vec2<f32>(0.5, -0.5) + vec2<f32>(0.5);
    if (any(uv < vec2<f32>(0.0)) || any(uv > vec2<f32>(1.0)) || ndc.z < 0.0 || ndc.z > 1.0) {
        return 1.0;
    }

    let facingSun = max(dot(normalize(input.normal), draw.towardSun.xyz), 0.0);
    // 倾斜面上相邻采样点的深度差较大，因此增加接收深度偏移。
    let reference = ndc.z - draw.shadowControls.z * (1.0 + 2.0 * (1.0 - facingSun));
    if (draw.shadowControls.y < 0.5) {
        let sample = textureSampleCompareLevel(sunDepth, sunDepthSampler, uv, layer, reference);
        return mix(sample, 1.0, fade);
    }

    let texel = 1.0 / vec2<f32>(textureDimensions(sunDepth));
    var lit = 0.0;
    for (var y = -1; y <= 1; y += 1) {
        for (var x = -1; x <= 1; x += 1) {
            let offset = vec2<f32>(f32(x), f32(y)) * texel;
            lit += textureSampleCompareLevel(sunDepth, sunDepthSampler, uv + offset, layer, reference);
        }
    }
    return mix(lit / 9.0, 1.0, fade);
}
