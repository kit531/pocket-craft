#version 450
layout(set = 0, binding = 0) uniform sampler2D uAtlas;

layout(push_constant) uniform PC {
    mat4 mvp;
    vec4 fog;
    vec4 fogColor;
    vec4 origin;
    vec4 cam;
} pc;

layout(location = 0) in vec2 vUv;
layout(location = 1) in float vShade;
layout(location = 2) in float vDist;
layout(location = 0) out vec4 outColor;

void main() {
    vec4 c = textureLod(uAtlas, vUv, 0.0);  // tiny quads: nearest-ish detail is wanted, no mip blur
    if (c.a < 0.5) discard;
    c.rgb *= vShade;
    float f = clamp((vDist - pc.fog.x) / max(pc.fog.y - pc.fog.x, 0.001), 0.0, 1.0);
    outColor = vec4(mix(c.rgb, pc.fogColor.rgb, f), 1.0);
}
