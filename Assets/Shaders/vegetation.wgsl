struct VegetationOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) normal: vec3<f32>,
    @location(1) color: vec3<f32>,
    @location(3) cardUv: vec2<f32>,     // 卡片局部坐标，用于草叶覆盖测试
    @location(4) worldPosition: vec3<f32>, // 植被顶点的世界坐标，用于雾和接收距离
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
    return VegetationOutput(draw.viewProjection * draw.renderModel * world, vegetationRotate(normal, facingShade.xy),
                            color * facingShade.z, uv, world.xyz);
}

@vertex
fn vsVegetationDepth(@location(0) position: vec3<f32>, @location(4) positionScale: vec4<f32>,
                     @location(5) facingShade: vec4<f32>) -> @builtin(position) vec4<f32> {
    let world = positionScale.xyz + vegetationRotate(position, facingShade.xy) * positionScale.w;
    return shadowDepthMatrix * vec4<f32>(world, 1.0);
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
    light.worldPosition = in.worldPosition;
    return foggedWorld(shade(light, sunVisibility(light)), in.worldPosition);
}

@fragment
fn fsVegetation(input: VegetationOutput) -> @location(0) vec4<f32> {
    return outputColor(vec4<f32>(shadeVegetation(input), 1.0));
}
