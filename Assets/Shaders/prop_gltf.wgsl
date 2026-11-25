@group(3) @binding(4) var gltfBase: texture_2d<f32>;
@group(3) @binding(5) var gltfMetalRough: texture_2d<f32>;
@group(3) @binding(6) var gltfSampler: sampler;

//————————————————————————————————————————————————————————————————————————————————
// glTF 的线性底色、金属度和粗糙度系数占两个 16 字节槽。
//————————————————————————————————————————————————————————————————————————————————
struct GltfFactors {
    baseAndMetal: vec4<f32>,    // RGB 是底色，A 是金属度乘数
    roughness: vec4<f32>,      // X 是粗糙度乘数，其余用于对齐
}
@group(3) @binding(7) var<uniform> gltfFactors: GltfFactors;
@group(3) @binding(8) var bakedLightmap: texture_2d<f32>;

//————————————————————————————————————————————————————————————————————————————————
// glTF 顶点沿用世界和太阳变换，并传递 glTF 的两套纹理坐标。
//————————————————————————————————————————————————————————————————————————————————
@vertex
fn vsGltf(@location(0) position: vec3<f32>, @location(1) normal: vec3<f32>,
          @location(2) color: vec3<f32>, @location(4) uv: vec2<f32>,
          @location(5) lightmapUv: vec2<f32>) -> VertexOutput {
    var output = worldVertex(position, normal, color);
    output.uv = uv;
    output.lightmapUv = lightmapUv;
    return output;
}

//————————————————————————————————————————————————————————————————————————————————
// 金属度读 B，粗糙度读 G，底色由 sRGB 纹理采样时自动转为线性。
//————————————————————————————————————————————————————————————————————————————————
fn shadeGltf(input: VertexOutput) -> vec3<f32> {
    let base = textureSample(gltfBase, gltfSampler, input.uv).rgb * gltfFactors.baseAndMetal.rgb;
    let metalRough = textureSample(gltfMetalRough, gltfSampler, input.uv);
    let metal = metalRough.b * gltfFactors.baseAndMetal.a;
    let direct = shadePbr(input, base, metal, metalRough.g * gltfFactors.roughness.x);
    if draw.surface.bakedIrradiance == 0.0 {
        return direct;
    }
    // 图集为线性辐照度，只补充点灯的漫反射，雾化和显示编码仍在后面。
    let irradiance = textureSampleLevel(bakedLightmap, gltfSampler, input.lightmapUv, 0.0).rgb * draw.surface.bakedIrradiance;
    return direct + irradiance * base * (1.0 - metal);
}

@fragment
fn fsGltf(input: VertexOutput) -> @location(0) vec4<f32> {
    return outputColor(vec4<f32>(foggedWorld(shadeGltf(input), input.worldPosition), 1.0));
}
