const std   = @import("std");
const sg    = @import("sokol").gfx;
const sapp  = @import("sokol").app;
const sgapp = @import("sokol").app_gfx_glue;
const shd   = @import("shaders/color.glsl.zig");
const math  = @import("math.zig");
const vec2 = math.Vec2;
const vec3 = math.Vec3;
const mat4 = math.Mat4;

const Array = std.ArrayList;
var gpa = std.heap.GeneralPurposeAllocator(.{}){};
var global_allocator = &gpa.allocator;

const ImageEntry = struct {
    path: []const u8
};

const g_ImageList = [_]ImageEntry {
    .{ .path = "data/bouffe.png" },
};

// image "hash"
const ImageID = struct {
    u: u32,

    fn fromPath(comptime path: []const u8) ImageID
    {
        comptime var id: ImageID = .{ .u = 1 };

        for(g_ImageList) |entry| {
            if(StringEquals(entry.path, path)) {
                return id;
            }

            id.u += 1;
        }

        unreachable;
    }
};

fn StringEquals(str1: []const u8, str2: []const u8) bool 
{
    if(str1.len != str2.len) return false;

    comptime var i = 0;
    while(i < str1.len) {
        if(str1[i] != str2[i]) return false;
        i += 1;
    }
    return true;
}

const Camera = struct {
    pos: vec2 = vec2.zero(),
    zoom: f32 = 1.0,
};

const RenderCommandSprite = struct {
    pos: vec2,
    scale: vec2 = vec2.new(1, 1),
    rot: f32 = 0,
    color: u32 = 0xFFFFFFFF,
    imgID: ImageID,
};

const Renderer = struct {
    cam: Camera = .{},
    queueSprite: Array(RenderCommandSprite),
};

const Input = struct {
    zoom: f32 = 1.0
};

const Game = struct {
    input: Input = .{},
};

var rdr: Renderer = undefined;
var game: Game = .{};

const state = struct {
    var bind: sg.Bindings = .{};
    var pip: sg.Pipeline = .{};
    var pass_action: sg.PassAction = .{};
};

export fn init() void
{
    rdr = .{
        .queueSprite = Array(RenderCommandSprite).init(global_allocator)
    };

    sg.setup(.{
        .context = sgapp.context()
    });
    
     // a vertex buffer
    const vertices = [_]f32 {
        // positions         colors
        -0.5,  0.5, 0.5,     1.0, 0.0, 0.0, 1.0,
         0.5,  0.5, 0.5,     0.0, 1.0, 0.0, 1.0,
         0.5, -0.5, 0.5,     0.0, 0.0, 1.0, 1.0,
        -0.5, -0.5, 0.5,     1.0, 1.0, 0.0, 1.0
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

    rdr.queueSprite.append(.{
        .pos = .{ .x = 0.0, .y =  0.0 },
        .imgID = comptime ImageID.fromPath("data/bouffe.png"),
    }) catch unreachable;
}

export fn frame() void
{
    // zoom over many frames (smooth zoom)
    // TODO: make this better
    if(rdr.cam.zoom != game.input.zoom) {
        const delta = game.input.zoom - rdr.cam.zoom;
        if(std.math.fabs(delta) < 0.000001) {
            rdr.cam.zoom = game.input.zoom;
        }
        else {
            rdr.cam.zoom += delta / 10.0;
        }
    }

    const hw = sapp.widthf() / 2.0;
    const hh = sapp.heightf() / 2.0;

    const left = (-hw + rdr.cam.pos.x) * 1.0/rdr.cam.zoom;
    const right = (hw + rdr.cam.pos.x) * 1.0/rdr.cam.zoom;
    const bottom = (hh + rdr.cam.pos.y) * 1.0/rdr.cam.zoom;
    const top = (-hh + rdr.cam.pos.y) * 1.0/rdr.cam.zoom;

    const vs_params = .{
        mat4.ortho(left, right, bottom, top, -10.0, 10.0)
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
        if(event.key_code == .F1) {
            //reset Camera
            game.input.zoom = 1.0;
        }
    }
    else if(event.type == .MOUSE_SCROLL) {
        if(event.scroll_y > 0.0) {
            game.input.zoom *= 1.0 + event.scroll_y * 0.1;
        }
        else {
            game.input.zoom /= 1.0 + -event.scroll_y * 0.1;
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