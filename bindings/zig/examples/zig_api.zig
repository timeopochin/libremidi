const std = @import("std");
const lm = @import("libremidi");

const EnumeratedPorts = extern struct {
    in_ports: [256]?*lm.midi.In.Port = @splat(null),
    out_ports: [256]?*lm.midi.Out.Port = @splat(null),
    in_port_count: usize = 0,
    out_port_count: usize = 0,
};

pub fn print_identifier(name: []const u8, ident: lm.PortInformation.Identifier) void {
    std.debug.print("  {s}: ", .{name});
    const tagged = ident.asTagged();
    switch (tagged) {
        .none => std.debug.print("none\n", .{}),
        .uuid => |uuid| {
            std.debug.print("uuid: ", .{});
            for (uuid, 0..) |b, i| {
                std.debug.print("{}", .{b});
                if (i != uuid.len - 1) std.debug.print("-", .{});
            }
            std.debug.print("\n", .{});
        },
        .string => |s| std.debug.print("{s}\n", .{s}),
        .uint64 => |v| std.debug.print("{d}\n", .{v}),
    }
}

pub fn print_port_info(info: lm.PortInformation) void {
    print_identifier("container_identification", info.container_identifier);
    print_identifier("device_identification", info.device_identifier);

    std.debug.print("  client_handle: {d}\n", .{info.client_handle});
    std.debug.print("  port_handle:   {d}\n", .{info.port_handle});
    std.debug.print("  manufacturer:  {s}\n", .{info.manufacturer});
    std.debug.print("  device_name:   {s}\n", .{info.device_name});
    std.debug.print("  port_name:     {s}\n", .{info.port_name});
    std.debug.print("  display_name:  {s}\n", .{info.display_name});

    std.debug.print("  port type: {}\n", .{info.type});
}

pub fn main() !void {
    std.debug.print("Hello from libremidi Zig(ified) API example!\n", .{});
    std.debug.print("libremidi version: {s}\n\n", .{lm.getVersion()});

    var e: EnumeratedPorts = .{};

    const observer: *lm.Observer = try .init(&.{
        .track_hardware = true,
        .track_virtual = true,
        .track_any = true,
        .input_added = .{
            .context = &e,
            .callback = on_input_port_found,
        },
        .output_added = .{
            .context = &e,
            .callback = on_output_port_found,
        },
    }, &.{
        .conf_type = .observer,
        .api = .alsa_seq,
    });
    defer free_observer(observer, &e);

    try enumerate_ports(observer, &e);

    const midi_in: *lm.midi.In = try .init(&.{
        .version = .midi1,
        .port = .{ .input = e.in_ports[0] },
        .msg_callback = .{ .on_midi1_message = .{ .callback = on_midi1_message } },
    }, &.{
        .conf_type = .input,
        .api = .alsa_seq,
    });
    defer midi_in.deinit();

    const midi_out: *lm.midi.Out = try .init(&.{
        .version = .midi1,
        .virtual_port = true,
        .port_name = "my-app",
    }, &.{
        .conf_type = .output,
        .api = .alsa_seq,
    });
    defer midi_out.deinit();

    for (0..99) |_| std.time.sleep(1e9); // sleep 1s, 100 times
}

fn free_observer(observer: *lm.Observer, e: *EnumeratedPorts) void {
    for (e.in_ports) |maybe_port| if (maybe_port) |port| port.deinit();
    for (e.out_ports) |maybe_port| if (maybe_port) |port| port.deinit();

    observer.deinit();
}

export fn on_input_port_found(ctx: ?*anyopaque, port: *lm.midi.In.Port) callconv(.C) void {
    std.debug.print("input: {s}\n", .{port.getName() catch ""});
    const info_maybe = port.getInformation() catch null;
    if (info_maybe) |info| {
        print_port_info(info);
    }

    var e: *EnumeratedPorts = @ptrCast(@alignCast(ctx));
    e.in_ports[e.in_port_count] = port.clone() catch null;
    e.in_port_count += 1;
}

export fn on_output_port_found(ctx: ?*anyopaque, port: *lm.midi.Out.Port) callconv(.C) void {
    std.debug.print("output: {s}\n", .{port.getName() catch ""});
    const info_maybe = port.getInformation() catch null;
    if (info_maybe) |info| {
        print_port_info(info);
    }

    var e: *EnumeratedPorts = @ptrCast(@alignCast(ctx));
    e.out_ports[e.out_port_count] = port.clone() catch null;
    e.out_port_count += 1;
}

fn on_midi1_message(ctx: ?*anyopaque, ts: lm.Timestamp, msg: [*]const lm.midi.v1.Symbol, len: usize) callconv(.C) void {
    _ = ctx;
    _ = ts;
    _ = len;

    std.debug.print("0x{x:02} 0x{x:02} 0x{x:02}\n", .{ msg[0], msg[1], msg[2] });
}

fn on_midi2_message(ctx: ?*anyopaque, ts: lm.Timestamp, msg: [*]const lm.midi.v2.Symbol, len: usize) callconv(.C) void {
    _ = ctx;
    _ = ts;
    _ = len;

    std.debug.print("0x{x:02} 0x{x:02} 0x{x:02}\n", .{ msg[0], msg[1], msg[2] });
}

fn enumerate_ports(observer: *lm.Observer, e: *EnumeratedPorts) !void {
    try observer.enumerateInputPorts(e, on_input_port_found);
    try observer.enumerateOutputPorts(e, on_output_port_found);
}
