//————————————————————————————————————————————————————————————————————————————————
// 对应 C++ FrameAtmosphere。两个通道在显示编码之前用相同的输入。
//————————————————————————————————————————————————————————————————————————————————
struct FrameAtmosphere {
    colorDensity: vec4<f32>,  // 线性雾色 RGB，W 为每米衰减系数
    distances: vec4<f32>,     // 雾起点、阴影淡出起点、阴影终点与填充
}

//————————————————————————————————————————————————————————————————————————————————
// 将已经算好光照和材质的线性颜色沿观看距离混入低空色。
//————————————————————————————————————————————————————————————————————————————————
fn applyDistanceFog(linear: vec3<f32>, eyeDistance: f32, air: FrameAtmosphere) -> vec3<f32> {
    if (air.colorDensity.w <= 0.0) {
        return linear;
    }
    let travelInFog = max(eyeDistance - air.distances.x, 0.0);
    let transmittance = exp(-air.colorDensity.w * travelInFog);
    return mix(air.colorDensity.rgb, linear, transmittance);
}
