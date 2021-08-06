const std   = @import("std");
const sg    = @import("sokol").gfx;
const sapp  = @import("sokol").app;
const sgapp = @import("sokol").app_gfx_glue;
const shd   = @import("shaders/color.glsl.zig");
const math  = @import("math.zig");
const vec2 = math.Vec2;
const vec3 = math.Vec3;
const mat4 = math.Mat4;

const Camera = struct {
    pos: vec2 = vec2.zero(),
    zoom: f32 = 0,
};

const Renderer = struct {
    cam: Camera,
};


var rdr: Renderer;

const state = struct {
    var bind: sg.Bindings = .{};
    var pip: sg.Pipeline = .{};
    var pass_action: sg.PassAction = .{};
};

export fn init() void
{
    sg.setup(.{
        .context = sgapp.context()
    });
    
     // a vertex buffer
    const vertices = [_]f32 {
        // positions         colors
        -50.0,  50.0, 50.0,     1.0, 0.0, 0.0, 1.0,
         50.0,  50.0, 50.0,     0.0, 1.0, 0.0, 1.0,
         50.0, -50.0, 50.0,     0.0, 0.0, 1.0, 1.0,
        -50.0, -50.0, 50.0,     1.0, 1.0, 0.0, 1.0
    };
    state.bind.vertex_buffers[0] = sg.makeBuffer(.{
        .data = sg.asRange(vertices)
    });

    // an index buffer
    const indices = [_] u16 { 0, 1, 2,  0, 2, 3 };
    state.bind.index_buffer = sg.makeBuffer(.{
        .type = .INDEXBUFFER,
        .data = sg.asRange(indices)
    });

    // a shader and pipeline state object
    var pip_desc: sg.PipelineDesc = .{
        .index_type = .UINT16,
        .shader = sg.makeShader(shd.colorShaderDesc(sg.queryBackend())),
        .depth = .{
            .compare = .LESS_EQUAL,
            .write_enabled = true,
        },
        .cull_mode = .NONE
    };
    pip_desc.layout.attrs[shd.ATTR_vs_position].format = .FLOAT3;
    pip_desc.layout.attrs[shd.ATTR_vs_color0].format = .FLOAT4;
    state.pip = sg.makePipeline(pip_desc);

    // clear to grey
    state.pass_action.colors[0] = .{ .action=.CLEAR, .value=.{ .r=0.2, .g=0.2, .b=0.2, .a=1 } };
}

export fn frame() void
{
    const hw = sapp.widthf() / 2.0;
    const hh = sapp.heightf() / 2.0;
    const vs_params = .{
        mat4.ortho(-hw, hw, hh, -hh, -10.0, 10.0)
    };

    sg.beginDefaultPass(state.pass_action, sapp.width(), sapp.height());
    sg.applyPipeline(state.pip);
    sg.applyBindings(state.bind);
    sg.applyUniforms(.VS, shd.SLOT_vs_params, sg.asRange(vs_params));
    sg.draw(0, 6, 1);
    sg.endPass();
    sg.commit();
}

export fn cleanup() void
{
    sg.shutdown();
}

export fn input(ev: ?*const sapp.Event) void
{
    const event = ev.?;
    if(event.type == .KEY_DOWN) {
        if(event.key_code == .ESCAPE) {
            sapp.quit();
        }
    }
}

pub fn main() void
{
    sapp.run(.{
        .init_cb = init,
        .frame_cb = frame,
        .cleanup_cb = cleanup,
        .event_cb = input,
        .width = 1920,
        .height = 1080,
        .window_title = "Life"
    });
}