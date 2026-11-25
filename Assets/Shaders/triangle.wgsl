//————————————————————————————————————————————————————————————————————————————————
// 将裁剪空间位置交给光栅器，并让 RGB 随三角形插值。
//————————————————————————————————————————————————————————————————————————————————
struct VertexOutput {
    @builtin(position) position: vec4<f32>,  // WebGPU 齐次裁剪坐标
    @location(0) color: vec3<f32>,            // 光栅化之后的插值颜色
}

//————————————————————————————————————————————————————————————————————————————————
// 直接传递顶点位置与颜色，本章还没有相机矩阵。
//————————————————————————————————————————————————————————————————————————————————
@vertex
fn vsMain(@location(0) position: vec2<f32>, @location(1) color: vec3<f32>) -> VertexOutput {
    var output: VertexOutput;
    output.position = vec4<f32>(position, 0.0, 1.0);
    output.color = color;
    return output;
}

//————————————————————————————————————————————————————————————————————————————————
// 插值颜色使用显示值。写入 sRGB 附件之前转换成线性颜色。
//————————————————————————————————————————————————————————————————————————————————
override targetIsSrgb: bool = false;

@fragment
fn fsMain(input: VertexOutput) -> @location(0) vec4<f32> {
    if !targetIsSrgb { return vec4<f32>(input.color, 1.0); }
    let srgb = input.color;
    let linear = select(srgb / 12.92, pow((srgb + 0.055) / 1.055, vec3<f32>(2.4)), srgb > vec3<f32>(0.04045));
    return vec4<f32>(linear, 1.0);
}
