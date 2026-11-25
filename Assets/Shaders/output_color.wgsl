// sRGB 附件由硬件编码。直绘使用普通 Unorm 表面时，在管线创建时开启软件编码。
override encodeForUnorm: bool = false;

fn outputColor(color: vec4<f32>) -> vec4<f32> {
    if !encodeForUnorm { return color; }
    let linear = clamp(color.rgb, vec3<f32>(0.0), vec3<f32>(1.0));
    let display = select(12.92 * linear, 1.055 * pow(linear, vec3<f32>(1.0 / 2.4)) - 0.055,
                         linear > vec3<f32>(0.0031308));
    return vec4<f32>(display, color.a);
}
