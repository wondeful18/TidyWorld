//————————————————————————————————————————————————————————————————————————————————
// 第二章沿用相机视角的顶点入口。
//————————————————————————————————————————————————————————————————————————————————
@vertex
fn vsWorld(@location(0) position: vec3<f32>, @location(1) normal: vec3<f32>,
           @location(2) color: vec3<f32>) -> VertexOutput {
    return worldVertex(position, normal, color);
}

//————————————————————————————————————————————————————————————————————————————————
// 颜色通道同时传递太阳裁剪坐标，供片元阶段计算深度纹理的采样位置。
//————————————————————————————————————————————————————————————————————————————————
@vertex
fn vsShadowedWorld(@location(0) position: vec3<f32>, @location(1) normal: vec3<f32>,
                   @location(2) color: vec3<f32>) -> VertexOutput {
    var output = worldVertex(position, normal, color);
    return output;
}

@fragment
fn fsWorld(input: VertexOutput) -> @location(0) vec4<f32> {
    return outputColor(vec4<f32>(shade(input, 1.0), 1.0));
}

@fragment
fn fsShadowed(input: VertexOutput) -> @location(0) vec4<f32> {
    return outputColor(vec4<f32>(shade(input, sunVisibility(input)), 1.0));
}
