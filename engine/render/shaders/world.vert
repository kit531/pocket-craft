#version 450
// Unpacks the 12-byte MeshVertex (docs/ARCHITECTURE.md). Positions are camera-relative.
layout(location = 0) in uvec4 inPosTile;  // x, y, z (1/16 block), tileFace
layout(location = 1) in uvec2 inLightAo;  // light (sky<<4 | block), ao

layout(push_constant) uniform PC {
    mat4 mvp;         // preRotate * proj * rotationOnly(view)
    vec4 fog;         // start, end, time, daylight
    vec4 fogColor;
    vec4 origin;      // section origin - camera (xyz)
} pc;

layout(location = 0) out vec2 vLocalUv;
layout(location = 1) flat out uint vTile;
layout(location = 2) out float vShade;
layout(location = 3) out float vDist;

const vec2 kCorner[4] = vec2[4](vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1));
// +X, -X, +Y, -Y, +Z, -Z
const float kFaceShade[6] = float[6](0.80, 0.80, 1.00, 0.50, 0.65, 0.65);
const float kAoShade[4] = float[4](1.0, 0.80, 0.62, 0.45);

void main() {
    vec3 rel = pc.origin.xyz + vec3(inPosTile.xyz) * (1.0 / 16.0);
    gl_Position = pc.mvp * vec4(rel, 1.0);

    uint tf = inPosTile.w;
    vTile = tf & 0xFFu;
    vLocalUv = kCorner[(tf >> 8) & 3u];
    uint face = min((tf >> 10) & 7u, 5u);

    float sky = float(inLightAo.x >> 4) * (1.0 / 15.0) * pc.fog.w;
    float blk = float(inLightAo.x & 15u) * (1.0 / 15.0);
    float l = max(sky, blk);
    float light = 0.06 + 0.94 * l * sqrt(l);
    vShade = light * kFaceShade[face] * kAoShade[inLightAo.y & 3u];
    vDist = length(rel);
}
