// 地形、道路、材质球与烘焙各自拥有明确的字段。
struct SurfaceParameters {
    terrainMode: u32,
    showTrail: f32,
    roadSurface: u32,
    useSampleMaterial: u32,
    splatOrigin: vec2<f32>,
    splatExtent: f32,
    bakedIrradiance: f32,
    sampleMetallic: f32,
    sampleRoughness: f32,
    pad: vec2<f32>,
}

//————————————————————————————————————————————————————————————————————————————————
// 这些字段与 C++ DrawUniform 逐项对应。
//————————————————————————————————————————————————————————————————————————————————
struct DrawUniform {
    viewProjection: mat4x4<f32>,  // 从世界到裁剪空间
    renderModel: mat4x4<f32>,     // 光栅化用绝对或相对平移
    model: mat4x4<f32>,           // 从模型到世界空间
    normalToWorld: mat4x4<f32>,   // 将法线正确变换到世界空间
    towardSun: vec4<f32>,         // 从物体指向太阳的单位方向
    sunRadiance: vec4<f32>,       // 线性光色与强度的乘积
    shadowControls: vec4<f32>,    // 阴影开关、PCF 开关、深度偏移、保留槽
    cameraAndReflection: vec4<f32>, // xyz 为相机世界位置，w 为车身反射开关
    surface: SurfaceParameters,            // 类别与对应参数，地形的 w 为世界边长
    coast: vec4<f32>,               // 海岸开关、水位 Y、岸坡过渡的 X 起点和终点
    air: FrameAtmosphere,           // 与水面共用的雾色、衰减和世界尺度
    lighting: vec4<f32>,             // 环境亮度和日夜混合
}

@group(0) @binding(0) var<uniform> draw: DrawUniform;

@group(2) @binding(0) var environmentCube: texture_cube<f32>;
@group(2) @binding(1) var environmentSampler: sampler;
@group(2) @binding(4) var nightCube: texture_cube<f32>;

struct VertexOutput {
    @builtin(position) position: vec4<f32>,  // WebGPU 裁剪坐标
    @location(0) normal: vec3<f32>,          // 插值后的世界法线
    @location(1) color: vec3<f32>,           // 线性材质底色
    @location(3) worldPosition: vec3<f32>,   // 第四章用于求观察方向的世界位置
    @location(6) lightmapUv: vec2<f32>,     // glTF TEXCOORD_1
    @location(5) uv: vec2<f32>,             // glTF 原始纹理坐标
    @location(4) reflectivity: f32,         // 车漆和玻璃有反射，灯和轮胎为 0
}

//————————————————————————————————————————————————————————————————————————————————
// 顶点位置依次应用模型、视图和投影矩阵，法线不参与平移。
//————————————————————————————————————————————————————————————————————————————————
fn worldVertex(position: vec3<f32>, normal: vec3<f32>, color: vec3<f32>) -> VertexOutput {
    var output: VertexOutput;
    output.position = draw.viewProjection * draw.renderModel * vec4<f32>(position, 1.0);
    output.normal = (draw.normalToWorld * vec4<f32>(normal, 0.0)).xyz;
    output.color = color;
    output.worldPosition = (draw.model * vec4<f32>(position, 1.0)).xyz;
    return output;
}

//————————————————————————————————————————————————————————————————————————————————
// 车身反射还需要片元世界位置与顶点上的反射权重。
//————————————————————————————————————————————————————————————————————————————————
@vertex
fn vsEnvironmentWorld(@location(0) position: vec3<f32>, @location(1) normal: vec3<f32>,
                      @location(2) color: vec3<f32>, @location(3) reflectivity: f32) -> VertexOutput {
    var output = worldVertex(position, normal, color);
    output.reflectivity = reflectivity;
    return output;
}

//————————————————————————————————————————————————————————————————————————————————
// 阴影可见性只衰减太阳直射光，环境项保留背光面的轮廓。
//————————————————————————————————————————————————————————————————————————————————
fn shade(input: VertexOutput, visibility: f32) -> vec3<f32> {
    let facingSun = max(dot(normalize(input.normal), draw.towardSun.xyz), 0.0);
    let ambient = vec3<f32>(0.30, 0.34, 0.38) * draw.lighting.x;
    let direct = (facingSun * visibility) * draw.sunRadiance.xyz;
    return clamp(input.color * (ambient + direct), vec3<f32>(0.0), vec3<f32>(1.0));
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
    var environment = textureSampleLevel(environmentCube, environmentSampler, reflected, 0.0).rgb;
    if draw.lighting.y > 0.0 {
        let night = textureSampleLevel(nightCube, environmentSampler, reflected, 0.0).rgb;
        environment = mix(environment, night, draw.lighting.y);
    }
    let grazing = 1.0 - max(dot(-incident, normal), 0.0);
    let weight = input.reflectivity * (0.72 + 0.22 * grazing * grazing);
    return mix(diffuse, environment, weight);
}

//————————————————————————————————————————————————————————————————————————————————
// 将同一帧的线性雾色用于地表、道路、房屋和植被。各入口随后才编码显示值。
//————————————————————————————————————————————————————————————————————————————————
fn foggedWorld(linear: vec3<f32>, worldPosition: vec3<f32>) -> vec3<f32> {
    let eyeDistance = distance(draw.cameraAndReflection.xyz, worldPosition);
    return applyDistanceFog(linear, eyeDistance, draw.air);
}

@fragment
fn fsEnvironment(input: VertexOutput) -> @location(0) vec4<f32> {
    return outputColor(vec4<f32>(foggedWorld(shadeEnvironment(input), input.worldPosition), 1.0));
}
