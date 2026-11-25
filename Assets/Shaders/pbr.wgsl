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
    let ambient = vec3<f32>(0.37, 0.42, 0.43) * (base * (1.0 - metal) + f0 * 0.65) * draw.lighting.x;
    return clamp(direct + ambient, vec3<f32>(0.0), vec3<f32>(1.0));
}
