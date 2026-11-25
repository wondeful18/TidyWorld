@group(3) @binding(0) var surfaceColor: texture_2d_array<f32>;
@group(3) @binding(1) var surfaceRough: texture_2d_array<f32>;
@group(3) @binding(2) var surfaceSampler: sampler;
@group(3) @binding(3) var terrainSplat: texture_2d<f32>;

const NO_TERRAIN_SURFACE = 0u;
const TERRAIN_WEIGHTS = 2u;
const TERRAIN_ROUGHNESS = 3u;
const NO_ROAD = 0u;
const DIRT_ROAD = 2u;
const PAVED_ROAD = 3u;

//————————————————————————————————————————————————————————————————————————————————
// 从世界高度和法线倾斜度得到三层权重。沟底和小径保留碎石。
//————————————————————————————————————————————————————————————————————————————————
fn surfaceWeights(p: vec3<f32>, normal: vec3<f32>) -> vec3<f32> {
    let slope = 1.0 - clamp(normal.y, 0.0, 1.0);
    let rock = max(smoothstep(0.085, 0.26, slope), 0.82 * smoothstep(2.0, 4.6, p.y));
    let gravel = max(smoothstep(0.025, 0.14, slope), 0.65 * (1.0 - smoothstep(-1.65, -0.75, p.y)));
    let trailX = 1.45 * sin(0.24 * p.z) + 0.55 * sin(0.37 * p.z + 0.6);
    let trail = (1.0 - smoothstep(0.52, 1.08, abs(p.x - trailX))) * draw.surface.showTrail;
    let hills = vec3<f32>((1.0 - gravel) * (1.0 - rock), gravel * (1.0 - rock), rock);
    return mix(hills, vec3<f32>(0.0, 1.0, 0.0), trail);
}

//————————————————————————————————————————————————————————————————————————————————
// 沿岸低地按坡度混合沙与岩，离开岸坡后沿用第八章的自动权重。
//————————————————————————————————————————————————————————————————————————————————
fn terrainWeights(p: vec3<f32>, normal: vec3<f32>) -> vec4<f32> {
    let base = vec4<f32>(surfaceWeights(p, normal), 0.0);
    if (draw.coast.x < 0.5) {
        return base;
    }
    let coastalX = smoothstep(draw.coast.z, draw.coast.w - 3.0, p.x);
    let relativeHeight = p.y - draw.coast.y;
    let bank = (1.0 - smoothstep(0.32, 2.2, relativeHeight)) * coastalX;
    let slope = 1.0 - clamp(normal.y, 0.0, 1.0);
    let rock = smoothstep(0.025, 0.22, slope);
    let beach = vec4<f32>(0.03, 0.10, 0.02, 0.85);
    let bluff = vec4<f32>(0.02, 0.06, 0.90, 0.02);
    return mix(base, mix(beach, bluff, rock), bank);
}

//————————————————————————————————————————————————————————————————————————————————
// 第八章材质地表以同一世界 UV 采样各层，粗糙度读线性红通道。
//————————————————————————————————————————————————————————————————————————————————
fn shadeMaterials(input: VertexOutput) -> vec3<f32> {
    if (draw.surface.roadSurface != NO_ROAD) {
        let roadUV = input.worldPosition.xz * 1.15;
        let base = textureSample(surfaceColor, surfaceSampler, roadUV, 1).rgb;
        let rough = textureSample(surfaceRough, surfaceSampler, roadUV, 1).r;
        if (draw.surface.roadSurface == DIRT_ROAD) {
            let soil = textureSample(surfaceColor, surfaceSampler, roadUV * 0.72, 3).rgb;
            return shadePbr(input, soil * vec3<f32>(0.86, 0.71, 0.61), 0.0, 0.93);
        }
        if (draw.surface.roadSurface == PAVED_ROAD) {
            return shadePbr(input, vec3<f32>(0.048, 0.057, 0.064) + base * 0.19, 0.0, 0.65);
        }
        return shadePbr(input, base * vec3<f32>(1.36, 1.17, 0.92), 0.0, rough);
    }
    if (draw.surface.useSampleMaterial != 0u) {
        return shadePbr(input, input.color, draw.surface.sampleMetallic, draw.surface.sampleRoughness);
    }
    if draw.surface.terrainMode == NO_TERRAIN_SURFACE { return shadeEnvironment(input); }
    let automatic = terrainWeights(input.worldPosition, normalize(input.normal));
    let halfTexel = 0.5 / vec2<f32>(textureDimensions(terrainSplat));
    let weightUV = clamp((input.worldPosition.xz - draw.surface.splatOrigin) / draw.surface.splatExtent, halfTexel, vec2<f32>(1.0) - halfTexel);
    let painted = textureSample(terrainSplat, surfaceSampler, weightUV);
    let weights = painted + automatic * max(0.0, 1.0 - dot(painted, vec4<f32>(1.0)));
    if (draw.surface.terrainMode == TERRAIN_WEIGHTS) {
        return weights.yxz + weights.w * vec3<f32>(0.8, 0.53, 0.13);
    }
    let uv = input.worldPosition.xz * 0.62;
    var base = vec3<f32>(0.0);
    var rough = 0.0;
    for (var layer = 0; layer < 4; layer += 1) {
        base += weights[layer] * textureSample(surfaceColor, surfaceSampler, uv, layer).rgb;
        rough += weights[layer] * textureSample(surfaceRough, surfaceSampler, uv, layer).r;
    }
    if (draw.surface.terrainMode == TERRAIN_ROUGHNESS) {
        return vec3<f32>(rough);
    }
    return shadePbr(input, base, 0.0, rough);
}

@fragment
fn fsMaterials(input: VertexOutput) -> @location(0) vec4<f32> {
    return outputColor(vec4<f32>(foggedWorld(shadeMaterials(input), input.worldPosition), 1.0));
}
