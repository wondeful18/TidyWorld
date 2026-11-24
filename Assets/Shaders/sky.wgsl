//————————————————————————————————————————————————————————————————————————————————
// 保存仅含相机旋转的视图投影，以及背景投影方式。
//————————————————————————————————————————————————————————————————————————————————
struct SkyUniform {
    rotationProjection: mat4x4<f32>, // 方向到裁剪空间，不含相机平移
    options: vec4<f32>,             // x 为全景开关，y 为夜景比例
    horizon: vec4<f32>,             // 海岸天际线的线性 RGB，w 为开关
}

@group(0) @binding(0) var<uniform> sky: SkyUniform;
@group(1) @binding(0) var skyCube: texture_cube<f32>;
@group(1) @binding(1) var cubeSampler: sampler;
@group(1) @binding(2) var panorama: texture_2d<f32>;
@group(1) @binding(3) var panoSampler: sampler;
@group(1) @binding(4) var nightCube: texture_cube<f32>;
@group(1) @binding(5) var nightPanorama: texture_2d<f32>;

//————————————————————————————————————————————————————————————————————————————————
// 传给片元的方向与世界坐标系对齐，插值后再归一化。
//————————————————————————————————————————————————————————————————————————————————
struct SkyOutput {
    @builtin(position) position: vec4<f32>, // 置于远平面的裁剪位置
    @location(0) direction: vec3<f32>,     // 从中心射向天空的方向
}

//————————————————————————————————————————————————————————————————————————————————
// 设置 Z = W，透视除法后的深度为 1。
//————————————————————————————————————————————————————————————————————————————————
@vertex
fn vsSky(@location(0) point: vec3<f32>) -> SkyOutput {
    let clip = sky.rotationProjection * vec4<f32>(point, 1.0);
    return SkyOutput(clip.xyww, point);
}

//————————————————————————————————————————————————————————————————————————————————
// 从单位方向求经纬角。U 在东西接缝循环，V 从上到下。
//————————————————————————————————————————————————————————————————————————————————
fn panoramaUv(direction: vec3<f32>) -> vec2<f32> {
    const pi = 3.141592653589793;
    let longitude = atan2(direction.x, direction.z);
    let latitude = asin(clamp(direction.y, -1.0, 1.0));
    return vec2<f32>(0.5 + longitude / (2.0 * pi), 0.5 - latitude / pi);
}

//————————————————————————————————————————————————————————————————————————————————
// 以世界方向直接采样立方体，或先换算成全景图的二维坐标。
//————————————————————————————————————————————————————————————————————————————————
fn sampleSky(direction: vec3<f32>) -> vec4<f32> {
    var sampled: vec4<f32>;
    if (sky.options.x > 0.5) {
        let uv = panoramaUv(direction);
        sampled = textureSampleLevel(panorama, panoSampler, uv, 0.0);
        if sky.options.y > 0.0 {
            sampled = mix(sampled, textureSampleLevel(nightPanorama, panoSampler, uv, 0.0), sky.options.y);
        }
    } else {
        sampled = textureSampleLevel(skyCube, cubeSampler, direction, 0.0);
        if sky.options.y > 0.0 {
            sampled = mix(sampled, textureSampleLevel(nightCube, cubeSampler, direction, 0.0), sky.options.y);
        }
    }
    if (sky.horizon.w > 0.5) {
        // 海平面上方保留照片的云层，靠近地平线逐渐衔接远海的颜色。
        let skyWeight = smoothstep(0.025, 0.25, direction.y);
        sampled = vec4<f32>(mix(sky.horizon.rgb, sampled.rgb, skyWeight), sampled.a);
    }
    return sampled;
}

//————————————————————————————————————————————————————————————————————————————————
// sRGB 目标自动编码，天空着色器只返回线性采样结果。
//————————————————————————————————————————————————————————————————————————————————
@fragment
fn fsSkyLinear(input: SkyOutput) -> @location(0) vec4<f32> {
    return sampleSky(normalize(input.direction));
}

//————————————————————————————————————————————————————————————————————————————————
// 普通 Unorm 目标需要将线性采样值编码一次。
//————————————————————————————————————————————————————————————————————————————————
@fragment
fn fsSkyDisplay(input: SkyOutput) -> @location(0) vec4<f32> {
    let linear = sampleSky(normalize(input.direction)).rgb;
    let encoded = select(12.92 * linear, 1.055 * pow(linear, vec3<f32>(1.0 / 2.4)) - 0.055,
                         linear > vec3<f32>(0.0031308));
    return vec4<f32>(encoded, 1.0);
}
