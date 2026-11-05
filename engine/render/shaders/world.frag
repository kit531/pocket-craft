#version 450
layout(constant_id = 0) const int MODE = 0;  // 0 = opaque pass (alpha test, e.g. leaves), 1 = translucent pass (blend)

layout(set = 0, binding = 0) uniform sampler2D uAtlas;

layout(push_constant) uniform PC {
    mat4 mvp;
    vec4 fog;      // start, end, time, daylight
    vec4 fogColor;
    vec4 origin;
} pc;

layout(location = 0) in vec2 vLocalUv;
layout(location = 1) flat in uint vTile;
layout(location = 2) in float vShade;
layout(location = 3) in float vDist;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 dx = dFdx(vLocalUv) * (1.0 / 16.0);
    vec2 dy = dFdy(vLocalUv) * (1.0 / 16.0);
    vec2 luv = vLocalUv;
    if (vTile == 5u) luv = fract(luv + vec2(pc.fog.z * 0.035, pc.fog.z * 0.02));  // water drift; the tile is seamless
    luv = clamp(luv, vec2(0.002), vec2(0.998));
    // Gradients come from the unwrapped uv so fract() does not select a wrong mip at the seam.
    vec2 tileXY = vec2(float(vTile & 15u), float(vTile >> 4));
    vec4 c = textureGrad(uAtlas, (tileXY + luv) * (1.0 / 16.0), dx, dy);
    if (MODE == 0) {
        if (c.a < 0.5) discard;
        c.a = 1.0;
    } else {
        if (c.a < 0.02) discard;
    }
    c.rgb *= vShade;
    float f = clamp((vDist - pc.fog.x) / max(pc.fog.y - pc.fog.x, 0.001), 0.0, 1.0);
    c.rgb = mix(c.rgb, pc.fogColor.rgb, f);
    outColor = c;
}
