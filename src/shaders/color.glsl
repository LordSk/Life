@ctype mat4 @import("../math.zig").Mat4

// vertex
@vs vs
uniform vs_params {
    mat4 mvp;
};

in vec3 position;
in vec4 color0;

out vec4 color;

void main() {
    gl_Position = mvp * vec4(position.xy, 0.0, 1.0);
    color = color0;
}
@end

// fragment
@fs fs
in vec4 color;
out vec4 frag_color;

void main() {
    frag_color = color;
}
@end

// program
@program color vs fs

