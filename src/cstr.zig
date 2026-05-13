const std = @import("std");
const c = @import("c.zig").c;

pub fn len(value: [*c]const c.gchar) usize {
    if (value == null) return 0;
    return std.mem.len(@as([*:0]const u8, @ptrCast(value)));
}

pub fn bytes(value: [*c]const c.gchar) []const u8 {
    if (value == null) return "";
    return std.mem.span(@as([*:0]const u8, @ptrCast(value)));
}

pub fn eql(left: [*c]const c.gchar, right: [*c]const c.gchar) bool {
    return std.mem.eql(u8, bytes(left), bytes(right));
}
