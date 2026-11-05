#version 450
// Crosshair (triangles, NDC) and selection outline (lines, camera-relative).
layout(location = 0) in vec3 inPos;
layout(push_constant) uniform PC { mat4 mvp; vec4 color; } pc;
void main() { gl_Position = pc.mvp * vec4(inPos, 1.0); }
