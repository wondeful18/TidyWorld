//————————————————————————————————————————————————————————————————————————————————
// 这些字段与 C++ DrawUniform 逐项对应。
//————————————————————————————————————————————————————————————————————————————————
struct DrawUniform {
    viewProjection: mat4x4<f32>,  // 从世界到裁剪空间
    model: mat4x4<f32>,           // 从模型到世界空间
    normalToWorld: mat4x4<f32>,   // 将法线正确变换到世界空间
    towardSun: vec4<f32>,         // 从物体指向太阳的单位方向
    sunRadiance: vec4<f32>,       // 线性光色与强度的乘积
    lightViewProjection: mat4x4<f32>,  // 从世界到太阳裁剪坐标
    shadowControls: vec4<f32>,    // 阴影开关、PCF 开关、深度偏移、保留槽
    cameraAndReflection: vec4<f32>, // xyz 为相机世界位置，w 为车身反射开关
    material: vec4<f32>,            // 类别与对应参数，地形的 w 为世界边长
}

@group(0) @binding(0) var<uniform> draw: DrawUniform;
@group(1) @binding(0) var sunDepth: texture_depth_2d;
@group(1) @binding(1) var sunDepthSampler: sampler_comparison;
@group(2) @binding(0) var environmentCube: texture_cube<f32>;
@group(2) @binding(1) var environmentSampler: sampler;
@group(3) @binding(0) var surfaceColor: texture_2d_array<f32>;
@group(3) @binding(1) var surfaceRough: texture_2d_array<f32>;
@group(3) @binding(2) var surfaceSampler: sampler;
@group(3) @binding(3) var terrainSplat: texture_2d<f32>;

//————————————————————————————————————————————————————————————————————————————————
// 在顶点和片元阶段之间传递世界法线与线性底色。
//————————————————————————————————————————————————————————————————————————————————
struct VertexOutput {
    @builtin(position) position: vec4<f32>,  // WebGPU 裁剪坐标
    @location(0) normal: vec3<f32>,          // 插值后的世界法线
    @location(1) color: vec3<f32>,           // 线性材质底色
    @location(2) lightClip: vec4<f32>,       // 第三章插值后用于阴影查询的太阳裁剪坐标
    @location(3) worldPosition: vec3<f32>,   // 第四章用于求观察方向的世界位置
    @location(4) reflectivity: f32,         // 车漆和玻璃有反射，灯和轮胎为 0
}

//————————————————————————————————————————————————————————————————————————————————
// 顶点位置依次应用模型、视图和投影矩阵，法线不参与平移。
//————————————————————————————————————————————————————————————————————————————————
fn worldVertex(position: vec3<f32>, normal: vec3<f32>, color: vec3<f32>) -> VertexOutput {
    var output: VertexOutput;
    output.position = draw.viewProjection * draw.model * vec4<f32>(position, 1.0);
    output.normal = (draw.normalToWorld * vec4<f32>(normal, 0.0)).xyz;
    output.color = color;
    return output;
}

//————————————————————————————————————————————————————————————————————————————————
// 第二章沿用相机视角的顶点入口。
//————————————————————————————————————————————————————————————————————————————————
@vertex
fn vsWorld(@location(0) position: vec3<f32>, @location(1) normal: vec3<f32>,
           @location(2) color: vec3<f32>) -> VertexOutput {
    return worldVertex(position, normal, color);
}

//————————————————————————————————————————————————————————————————————————————————
// 让深度图写入与接收面采样使用完全相同的模型和太阳矩阵。
//————————————————————————————————————————————————————————————————————————————————
fn sunClipPosition(position: vec3<f32>) -> vec4<f32> {
    return draw.lightViewProjection * draw.model * vec4<f32>(position, 1.0);
}

//————————————————————————————————————————————————————————————————————————————————
// 太阳通道只需要位置，GPU 将可见表面的深度写入附件。
//————————————————————————————————————————————————————————————————————————————————
@vertex
fn vsSunDepth(@location(0) position: vec3<f32>) -> @builtin(position) vec4<f32> {
    return sunClipPosition(position);
}

//————————————————————————————————————————————————————————————————————————————————
// 颜色通道同时传递太阳裁剪坐标，供片元阶段计算深度纹理的采样位置。
//————————————————————————————————————————————————————————————————————————————————
@vertex
fn vsShadowedWorld(@location(0) position: vec3<f32>, @location(1) normal: vec3<f32>,
                   @location(2) color: vec3<f32>) -> VertexOutput {
    var output = worldVertex(position, normal, color);
    output.lightClip = sunClipPosition(position);
    return output;
}

//————————————————————————————————————————————————————————————————————————————————
// 车身反射还需要片元世界位置与顶点上的反射权重。
//————————————————————————————————————————————————————————————————————————————————
@vertex
fn vsEnvironmentWorld(@location(0) position: vec3<f32>, @location(1) normal: vec3<f32>,
                      @location(2) color: vec3<f32>, @location(3) reflectivity: f32) -> VertexOutput {
    var output = worldVertex(position, normal, color);
    output.lightClip = sunClipPosition(position);
    output.worldPosition = (draw.model * vec4<f32>(position, 1.0)).xyz;
    output.reflectivity = reflectivity;
    return output;
}

//————————————————————————————————————————————————————————————————————————————————
// 比较接收面深度和太阳看到的第一层表面。PCF 对九次比较结果取平均。
//————————————————————————————————————————————————————————————————————————————————
fn sunVisibility(input: VertexOutput) -> f32 {
    if (draw.shadowControls.x < 0.5) {
        return 1.0;
    }

    let ndc = input.lightClip.xyz / input.lightClip.w;
    // WebGPU 投影的 Z 已在 0–1，纹理 V 从上到下，需翻转 Y。
    let uv = ndc.xy * vec2<f32>(0.5, -0.5) + vec2<f32>(0.5);
    if (any(uv < vec2<f32>(0.0)) || any(uv > vec2<f32>(1.0)) || ndc.z < 0.0 || ndc.z > 1.0) {
        return 1.0;
    }

    let facingSun = max(dot(normalize(input.normal), draw.towardSun.xyz), 0.0);
    // 倾斜面上相邻采样点的深度差较大，因此增加接收深度偏移。
    let reference = ndc.z - draw.shadowControls.z * (1.0 + 2.0 * (1.0 - facingSun));
    if (draw.shadowControls.y < 0.5) {
        return textureSampleCompareLevel(sunDepth, sunDepthSampler, uv, reference);
    }

    let texel = 1.0 / vec2<f32>(textureDimensions(sunDepth));
    var lit = 0.0;
    for (var y = -1; y <= 1; y += 1) {
        for (var x = -1; x <= 1; x += 1) {
            let offset = vec2<f32>(f32(x), f32(y)) * texel;
            lit += textureSampleCompareLevel(sunDepth, sunDepthSampler, uv + offset, reference);
        }
    }
    return lit / 9.0;
}

//————————————————————————————————————————————————————————————————————————————————
// 阴影可见性只衰减太阳直射光，环境项保留背光面的轮廓。
//————————————————————————————————————————————————————————————————————————————————
fn shade(input: VertexOutput, visibility: f32) -> vec3<f32> {
    let facingSun = max(dot(normalize(input.normal), draw.towardSun.xyz), 0.0);
    let ambient = vec3<f32>(0.30, 0.34, 0.38);
    let direct = (facingSun * visibility) * draw.sunRadiance.xyz;
    return clamp(input.color * (ambient + direct), vec3<f32>(0.0), vec3<f32>(1.0));
}

//————————————————————————————————————————————————————————————————————————————————
// sRGB 目标负责硬件编码，片元阶段返回线性颜色。
//————————————————————————————————————————————————————————————————————————————————
@fragment
fn fsLinear(input: VertexOutput) -> @location(0) vec4<f32> {
    return vec4<f32>(shade(input, 1.0), 1.0);
}

//————————————————————————————————————————————————————————————————————————————————
// 将线性光照编码为 Unorm 颜色目标使用的 sRGB 显示值。
//————————————————————————————————————————————————————————————————————————————————
fn encodeDisplay(linear: vec3<f32>) -> vec4<f32> {
    let encoded = select(12.92 * linear, 1.055 * pow(linear, vec3<f32>(1.0 / 2.4)) - 0.055,
                         linear > vec3<f32>(0.0031308));
    return vec4<f32>(encoded, 1.0);
}

//————————————————————————————————————————————————————————————————————————————————
// 第二章的 Unorm 目标用软件编码，光照可见性保持为 1。
//————————————————————————————————————————————————————————————————————————————————
@fragment
fn fsDisplay(input: VertexOutput) -> @location(0) vec4<f32> {
    return encodeDisplay(shade(input, 1.0));
}

//————————————————————————————————————————————————————————————————————————————————
// 第三章向 sRGB 目标写入含阴影的线性颜色，由硬件负责编码。
//————————————————————————————————————————————————————————————————————————————————
@fragment
fn fsShadowedLinear(input: VertexOutput) -> @location(0) vec4<f32> {
    return vec4<f32>(shade(input, sunVisibility(input)), 1.0);
}

//————————————————————————————————————————————————————————————————————————————————
// 第三章向 Unorm 目标写入已编码的阴影结果。
//————————————————————————————————————————————————————————————————————————————————
@fragment
fn fsShadowedDisplay(input: VertexOutput) -> @location(0) vec4<f32> {
    return encodeDisplay(shade(input, sunVisibility(input)));
}

//————————————————————————————————————————————————————————————————————————————————
// 在已含 Lambert 和阴影的线性底色上混入远处环境，掠射角度反射更明显。
//————————————————————————————————————————————————————————————————————————————————
fn shadeEnvironment(input: VertexOutput) -> vec3<f32> {
    let diffuse = shade(input, sunVisibility(input));
    if (draw.cameraAndReflection.w < 0.5 || input.reflectivity <= 0.0) {
        return diffuse;
    }
    let normal = normalize(input.normal);
    let incident = normalize(input.worldPosition - draw.cameraAndReflection.xyz);
    let reflected = reflect(incident, normal);
    let environment = textureSampleLevel(environmentCube, environmentSampler, reflected, 0.0).rgb;
    let grazing = 1.0 - max(dot(-incident, normal), 0.0);
    let weight = input.reflectivity * (0.72 + 0.22 * grazing * grazing);
    return mix(diffuse, environment, weight);
}

//————————————————————————————————————————————————————————————————————————————————
// sRGB 目标由硬件编码场景线性值。
//————————————————————————————————————————————————————————————————————————————————
@fragment
fn fsEnvironmentLinear(input: VertexOutput) -> @location(0) vec4<f32> {
    return vec4<f32>(shadeEnvironment(input), 1.0);
}

//————————————————————————————————————————————————————————————————————————————————
// 普通 Unorm 目标使用场景着色器的显示编码。
//————————————————————————————————————————————————————————————————————————————————
@fragment
fn fsEnvironmentDisplay(input: VertexOutput) -> @location(0) vec4<f32> {
    return encodeDisplay(shadeEnvironment(input));
}

//————————————————————————————————————————————————————————————————————————————————
// 从世界高度和法线倾斜度得到三层权重。沟底和小径保留碎石。
//————————————————————————————————————————————————————————————————————————————————
fn surfaceWeights(p: vec3<f32>, normal: vec3<f32>) -> vec3<f32> {
    let slope = 1.0 - clamp(normal.y, 0.0, 1.0);
    let rock = max(smoothstep(0.085, 0.26, slope), 0.82 * smoothstep(2.0, 4.6, p.y));
    let gravel = max(smoothstep(0.025, 0.14, slope), 0.65 * (1.0 - smoothstep(-1.65, -0.75, p.y)));
    let trailX = 1.45 * sin(0.24 * p.z) + 0.55 * sin(0.37 * p.z + 0.6);
    let trail = (1.0 - smoothstep(0.52, 1.08, abs(p.x - trailX))) * draw.material.z;
    let hills = vec3<f32>((1.0 - gravel) * (1.0 - rock), gravel * (1.0 - rock), rock);
    return mix(hills, vec3<f32>(0.0, 1.0, 0.0), trail);
}

//————————————————————————————————————————————————————————————————————————————————
// 金属度分配漫反射与镜面颜色，GGX 用粗糙度控制高光分布。
//————————————————————————————————————————————————————————————————————————————————
fn shadePbr(input: VertexOutput, base: vec3<f32>, metal: f32, rough: f32) -> vec3<f32> {
    let pi = 3.14159265;
    let n = normalize(input.normal);
    let l = draw.towardSun.xyz;
    let v = normalize(draw.cameraAndReflection.xyz - input.worldPosition);
    let halfVector = l + v;
    let h = halfVector * inverseSqrt(max(dot(halfVector, halfVector), 0.000001));
    let nL = max(dot(n, l), 0.0);
    let nV = max(dot(n, v), 0.001);
    let nH = max(dot(n, h), 0.0);
    let vH = clamp(dot(v, h), 0.0, 1.0);

    let alpha = pow(clamp(rough, 0.12, 1.0), 2.0);
    let a2 = alpha * alpha;
    let dTerm = 1.0 + nH * nH * (a2 - 1.0);
    let distribution = a2 / (pi * dTerm * dTerm);
    let lambdaV = nL * sqrt(a2 + (1.0 - a2) * nV * nV);
    let lambdaL = nV * sqrt(a2 + (1.0 - a2) * nL * nL);
    let visibility = 0.5 / max(lambdaV + lambdaL, 0.0001);
    let f0 = mix(vec3<f32>(0.04), base, metal);
    let fresnel = f0 + (1.0 - f0) * pow(1.0 - vH, 5.0);
    let specular = distribution * visibility * fresnel;
    let diffuse = (1.0 - fresnel) * (1.0 - metal) * base / pi;
    let direct = (diffuse + specular) * draw.sunRadiance.xyz * (3.1 * nL * sunVisibility(input));
    let ambient = vec3<f32>(0.37, 0.42, 0.43) * (base * (1.0 - metal) + f0 * 0.65);
    return clamp(direct + ambient, vec3<f32>(0.0), vec3<f32>(1.0));
}

//————————————————————————————————————————————————————————————————————————————————
// 第八章材质地表以同一世界 UV 采样各层，粗糙度读线性红通道。
//————————————————————————————————————————————————————————————————————————————————
fn shadeMaterials(input: VertexOutput) -> vec3<f32> {
    if (draw.material.x < 0.5) {
        return shadeEnvironment(input);
    }
    if (draw.material.x > 2.5) {
        let roadUV = input.worldPosition.xz * 1.15;
        let base = textureSample(surfaceColor, surfaceSampler, roadUV, 1).rgb;
        let rough = textureSample(surfaceRough, surfaceSampler, roadUV, 1).r;
        if (draw.material.y == 1.0) {
            let soil = textureSample(surfaceColor, surfaceSampler, roadUV * 0.72, 3).rgb;
            return shadePbr(input, soil * vec3<f32>(0.86, 0.71, 0.61), 0.0, 0.93);
        }
        if (draw.material.y == 2.0) {
            return shadePbr(input, vec3<f32>(0.048, 0.057, 0.064) + base * 0.19, 0.0, 0.65);
        }
        return shadePbr(input, base * vec3<f32>(1.36, 1.17, 0.92), 0.0, rough);
    }
    if (draw.material.x > 1.5) {
        return shadePbr(input, input.color, draw.material.y, draw.material.z);
    }
    let automatic = vec4<f32>(surfaceWeights(input.worldPosition, normalize(input.normal)), 0.0);
    let halfTexel = 0.5 / vec2<f32>(textureDimensions(terrainSplat));
    let weightUV = clamp(input.worldPosition.xz / draw.material.w + 0.5, halfTexel, vec2<f32>(1.0) - halfTexel);
    let painted = textureSample(terrainSplat, surfaceSampler, weightUV);
    let weights = painted + automatic * max(0.0, 1.0 - dot(painted, vec4<f32>(1.0)));
    if (draw.material.y == 1.0) {
        return weights.yxz + weights.w * vec3<f32>(0.8, 0.53, 0.13);
    }
    let uv = input.worldPosition.xz * 0.62;
    var base = vec3<f32>(0.0);
    var rough = 0.0;
    for (var layer = 0; layer < 4; layer += 1) {
        base += weights[layer] * textureSample(surfaceColor, surfaceSampler, uv, layer).rgb;
        rough += weights[layer] * textureSample(surfaceRough, surfaceSampler, uv, layer).r;
    }
    if (draw.material.y == 2.0) {
        return vec3<f32>(rough);
    }
    return shadePbr(input, base, 0.0, rough);
}

//————————————————————————————————————————————————————————————————————————————————
// 延续附件各自的显示编码约定，着色全程用线性数值。
//————————————————————————————————————————————————————————————————————————————————
@fragment
fn fsMaterialsLinear(input: VertexOutput) -> @location(0) vec4<f32> {
    return vec4<f32>(shadeMaterials(input), 1.0);
}

//————————————————————————————————————————————————————————————————————————————————
// 颜色目标为普通 Unorm 时，对同一光照结果进行软件编码。
//————————————————————————————————————————————————————————————————————————————————
@fragment
fn fsMaterialsDisplay(input: VertexOutput) -> @location(0) vec4<f32> {
    return encodeDisplay(shadeMaterials(input));
}

struct VegetationOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) normal: vec3<f32>,
    @location(1) color: vec3<f32>,
    @location(2) lightClip: vec4<f32>,
    @location(3) cardUv: vec2<f32>,
}

//————————————————————————————————————————————————————————————————————————————————
// 实例记录保存 Y 轴旋转的 cos 和 sin，法线沿用同一旋转。
//————————————————————————————————————————————————————————————————————————————————
fn vegetationRotate(v: vec3<f32>, facing: vec2<f32>) -> vec3<f32> {
    return vec3<f32>(facing.x * v.x + facing.y * v.z, v.y, facing.x * v.z - facing.y * v.x);
}

@vertex
fn vsVegetation(@location(0) position: vec3<f32>, @location(1) normal: vec3<f32>,
                @location(2) color: vec3<f32>, @location(3) uv: vec2<f32>,
                @location(4) positionScale: vec4<f32>, @location(5) facingShade: vec4<f32>) -> VegetationOutput {
    let world = vec4<f32>(positionScale.xyz + vegetationRotate(position, facingShade.xy) * positionScale.w, 1.0);
    return VegetationOutput(draw.viewProjection * world, vegetationRotate(normal, facingShade.xy),
                            color * facingShade.z, draw.lightViewProjection * world, uv);
}

@vertex
fn vsVegetationDepth(@location(0) position: vec3<f32>, @location(4) positionScale: vec4<f32>,
                     @location(5) facingShade: vec4<f32>) -> @builtin(position) vec4<f32> {
    let world = positionScale.xyz + vegetationRotate(position, facingShade.xy) * positionScale.w;
    return draw.lightViewProjection * vec4<f32>(world, 1.0);
}

//————————————————————————————————————————————————————————————————————————————————
// 算出卡片上某片叶子的覆盖率，沿高度从略宽的根部渐细到叶尖。
//————————————————————————————————————————————————————————————————————————————————
fn grassBlade(uv: vec2<f32>, root: f32, tip: f32, height: f32, width: f32) -> f32 {
    let y = clamp(uv.y / height, 0.0, 1.0);
    let center = mix(root, tip, y * y);
    let halfWidth = width * (1.0 - y * 0.93);
    return (1.0 - smoothstep(halfWidth * 0.55, halfWidth, abs(uv.x - center))) * (1.0 - step(height, uv.y));
}

fn shadeVegetation(in: VegetationOutput) -> vec3<f32> {
    if (in.cardUv.x >= 0.0) {
        let uv = in.cardUv;
        let alpha = max(max(grassBlade(uv, 0.42, 0.13, 0.68, 0.083),
                            grassBlade(uv, 0.49, 0.40, 0.98, 0.106)),
                        max(grassBlade(uv, 0.56, 0.86, 0.82, 0.099),
                            grassBlade(uv, 0.57, 0.96, 0.55, 0.058)));
        if (alpha < 0.50) {
            discard;
        }
    }
    var light: VertexOutput;
    light.normal = in.normal;
    light.color = in.color;
    light.lightClip = in.lightClip;
    return shade(light, sunVisibility(light));
}

@fragment
fn fsVegetationLinear(in: VegetationOutput) -> @location(0) vec4<f32> {
    return vec4<f32>(shadeVegetation(in), 1.0);
}

@fragment
fn fsVegetationDisplay(in: VegetationOutput) -> @location(0) vec4<f32> {
    return encodeDisplay(shadeVegetation(in));
}
