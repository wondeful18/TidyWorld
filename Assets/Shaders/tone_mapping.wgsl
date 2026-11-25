@group(0) @binding(0) var hdrScene: texture_2d<f32>;
@group(0) @binding(1) var<uniform> exposureAndMode: vec4<f32>;

@vertex
fn vsDisplay(@builtin(vertex_index) vertex: u32) -> @builtin(position) vec4<f32> {
    let corners = array(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
    return vec4<f32>(corners[vertex], 0.0, 1.0);
}

@fragment
fn fsDisplay(@builtin(position) pixel: vec4<f32>) -> @location(0) vec4<f32> {
    let scene = textureLoad(hdrScene, vec2<i32>(pixel.xy), 0);
    var display = clamp(scene.rgb, vec3<f32>(0.0), vec3<f32>(1.0));
    if exposureAndMode.y != 0.0 {
        let exposed = max(scene.rgb, vec3<f32>(0.0)) * exposureAndMode.x;
        display = exposed / (1.0 + exposed); // 逐通道 Reinhard 曲线
    }
    return outputColor(vec4<f32>(display, scene.a));
}
