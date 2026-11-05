#version 450
// Camera-facing break particle quads. Positions are camera-relative, built on the CPU every frame.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUv;     // atlas uv (0..1) of this corner
layout(location = 2) in float inLight; // sky light 0..1

layout(push_constant) uniform PC {
    mat4 mvp;
    vec4 fog;      // start, end, time, daylight
    vec4 fogColor;
    vec4 origin;
    vec4 cam;
} pc;

layout(location = 0) out vec2 vUv;
layout(location = 1) out float vShade;
layout(location = 2) out float vDist;

void main() {
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    vUv = inUv;
    float l = inLight * pc.fog.w;
    vShade = (0.06 + 0.94 * l * sqrt(l)) * 0.85;
    vDist = length(inPos);
}
