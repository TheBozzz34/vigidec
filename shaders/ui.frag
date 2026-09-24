#version 450

// The microui atlas is a single-channel coverage texture; solid rectangles
// sample its fully opaque "white" cell.
layout(set = 0, binding = 0) uniform sampler2D u_atlas;

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec4 in_color;

layout(location = 0) out vec4 out_color;

void main() {
    out_color = vec4(in_color.rgb, in_color.a * texture(u_atlas, in_uv).r);
}
