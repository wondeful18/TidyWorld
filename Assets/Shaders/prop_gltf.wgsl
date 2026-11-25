@group(3) @binding(4) var gltfBase: texture_2d<f32>;
@group(3) @binding(5) var gltfMetalRough: texture_2d<f32>;
@group(3) @binding(6) var gltfSampler: sampler;

//————————————————————————————————————————————————————————————————————————————————
// glTF 的线性底色、金属度和粗糙度系数占两个 16 字节槽。
//————————————————————————————————————————————————————————————————————————————————
struct GltfFactors {
    baseAndMetal: vec4<f32>,    // RGB 是底色，A 是金属度乘数
    roughnessAndNormal: vec4<f32>, // 粗糙度乘数、法线强度、有无法线图、对齐
}
@group(3) @binding(7) var<uniform> gltfFactors: GltfFactors;
@group(3) @binding(8) var bakedLightmap: texture_2d<f32>;
@group(3) @binding(9) var gltfNormal: texture_2d<f32>;

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
// 用相邻像素的世界位置和 UV 变化恢复表面切线。镜像 UV 要保留副切线的符号。
//————————————————————————————————————————————————————————————————————————————————
fn materialNormal(input: VertexOutput) -> vec3<f32> {
    let n = normalize(input.normal);
    let dx = dpdx(input.worldPosition);
    let dy = dpdy(input.worldPosition);
    let uvDx = dpdx(input.uv);
    let uvDy = dpdy(input.uv);
    let uvArea = uvDx.x * uvDy.y - uvDx.y * uvDy.x;
    let tangentRaw = (dx * uvDy.y - dy * uvDx.y) * sign(uvArea);
    let bitangentRaw = (dy * uvDx.x - dx * uvDy.x) * sign(uvArea);
    let tangentPlane = tangentRaw - n * dot(n, tangentRaw);
    if draw.lighting.w < 0.5 || gltfFactors.roughnessAndNormal.z == 0.0 ||
       uvArea == 0.0 || dot(tangentPlane, tangentPlane) <= 0.0 {
        return n;
    }
    let t = normalize(tangentPlane);
    let b = cross(n, t) * select(-1.0, 1.0, dot(cross(n, t), bitangentRaw) >= 0.0);
    let encoded = textureSampleGrad(gltfNormal, gltfSampler, input.uv, uvDx, uvDy).rgb;
    let decoded = encoded * 2.0 - 1.0;
    let local = vec3<f32>(decoded.xy * gltfFactors.roughnessAndNormal.y, decoded.z);
    let mapped = mat3x3<f32>(t, b, n) * local;
    if dot(mapped, mapped) <= 0.0 { return n; }
    return normalize(mapped);
}

// 金属度读 B，粗糙度读 G，底色由 sRGB 纹理采样时自动转为线性。
fn shadeGltf(input: VertexOutput) -> vec3<f32> {
    let base = textureSample(gltfBase, gltfSampler, input.uv).rgb * gltfFactors.baseAndMetal.rgb;
    let metalRough = textureSample(gltfMetalRough, gltfSampler, input.uv);
    let metal = metalRough.b * gltfFactors.baseAndMetal.a;
    let direct = shadePbrNormal(input, base, metal, metalRough.g * gltfFactors.roughnessAndNormal.x, materialNormal(input));
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
