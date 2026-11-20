//————————————————————————————————————————————————————————————————————————————————
// 这些字段与 C++ DrawUniform 逐项对应。
//————————————————————————————————————————————————————————————————————————————————
struct DrawUniform {
    viewProjection: mat4x4<f32>,  // 从世界到裁剪空间
    model: mat4x4<f32>,           // 从模型到世界空间
    normalToWorld: mat4x4<f32>,   // 将法线正确变换到世界空间
    towardSun: vec4<f32>,         // 从物体指向太阳的单位方向
    sunRadiance: vec4<f32>,       // 线性光色与强度的乘积
}

@group(0) @binding(0) var<uniform> draw: DrawUniform;

//————————————————————————————————————————————————————————————————————————————————
// 在顶点和片元阶段之间传递世界法线与线性底色。
//————————————————————————————————————————————————————————————————————————————————
struct VertexOutput {
    @builtin(position) position: vec4<f32>,  // WebGPU 裁剪坐标
    @location(0) normal: vec3<f32>,          // 插值后的世界法线
    @location(1) color: vec3<f32>,           // 线性材质底色
}

//————————————————————————————————————————————————————————————————————————————————
// 顶点位置依次应用模型、视图和投影矩阵，法线不参与平移。
//————————————————————————————————————————————————————————————————————————————————
@vertex
fn vsWorld(@location(0) position: vec3<f32>, @location(1) normal: vec3<f32>,
           @location(2) color: vec3<f32>) -> VertexOutput {
    var output: VertexOutput;
    output.position = draw.viewProjection * draw.model * vec4<f32>(position, 1.0);
    output.normal = (draw.normalToWorld * vec4<f32>(normal, 0.0)).xyz;
    output.color = color;
    return output;
}

//————————————————————————————————————————————————————————————————————————————————
// Lambert 点积给出朝向太阳的亮度，环境项保留背光面的轮廓。
//————————————————————————————————————————————————————————————————————————————————
fn shade(input: VertexOutput) -> vec3<f32> {
    let facingSun = max(dot(normalize(input.normal), draw.towardSun.xyz), 0.0);
    let ambient = vec3<f32>(0.30, 0.34, 0.38);
    return clamp(input.color * (ambient + facingSun * draw.sunRadiance.xyz), vec3<f32>(0.0), vec3<f32>(1.0));
}

//————————————————————————————————————————————————————————————————————————————————
// sRGB 目标负责硬件编码，片元阶段返回线性颜色。
//————————————————————————————————————————————————————————————————————————————————
@fragment
fn fsLinear(input: VertexOutput) -> @location(0) vec4<f32> {
    return vec4<f32>(shade(input), 1.0);
}

//————————————————————————————————————————————————————————————————————————————————
// 给保存显示值的 Unorm 目标做一次 sRGB 转换。
//————————————————————————————————————————————————————————————————————————————————
@fragment
fn fsDisplay(input: VertexOutput) -> @location(0) vec4<f32> {
    let linear = shade(input);
    let encoded = select(12.92 * linear, 1.055 * pow(linear, vec3<f32>(1.0 / 2.4)) - 0.055,
                         linear > vec3<f32>(0.0031308));
    return vec4<f32>(encoded, 1.0);
}
