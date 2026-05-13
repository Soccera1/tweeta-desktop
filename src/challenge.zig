const std = @import("std");
const c = @import("c.zig").c;
const cstr = @import("cstr.zig");
const constants = @import("constants.zig");
const types = @import("types.zig");

const FALSE: c.gboolean = 0;
const TRUE: c.gboolean = 1;

extern fn fetch_url_internal(
    url: [*c]const c.gchar,
    chunk: [*c]types.MemoryStruct,
    post_data: [*c]const c.gchar,
    method: [*c]const c.gchar,
    response_code: [*c]c_long,
) c.gboolean;

fn lit(comptime value: [:0]const u8) [*c]const c.gchar {
    return @ptrCast(value.ptr);
}

fn logMsg(level: c.GLogLevelFlags, comptime fmt: [:0]const u8, args: anytype) void {
    @call(.auto, c.g_log, .{ @as([*c]const c.gchar, null), level, lit(fmt) } ++ args);
}

fn capHash(input: [*c]const c.gchar) u32 {
    var hash: u32 = 2166136261;
    var i: usize = 0;
    while (input[i] != 0) : (i += 1) {
        hash ^= @as(u8, @bitCast(input[i]));
        hash +%= (hash << 1) +% (hash << 4) +% (hash << 7) +% (hash << 8) +% (hash << 24);
    }
    return hash;
}

fn appendHex(out: [*c]c.gchar, offset: *usize, limit: usize, value: u32) void {
    const digits = "0123456789abcdef";
    var shift: i32 = 28;
    while (shift >= 0 and offset.* < limit) : (shift -= 4) {
        const nibble = @as(u4, @intCast((value >> @intCast(shift)) & 0xf));
        out[offset.*] = digits[nibble];
        offset.* += 1;
    }
}

fn capPrngGen(seed: [*c]const c.gchar, len_int: c_long) [*c]c.gchar {
    const len: usize = if (len_int > 0) @intCast(len_int) else 0;
    const out: [*c]c.gchar = @ptrCast(c.g_malloc(len + 1));
    if (out == null) {
        return null;
    }

    var state = capHash(seed);
    var offset: usize = 0;
    while (offset < len) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        appendHex(out, &offset, len, state);
    }
    out[len] = 0;
    return out;
}

fn hexNibble(ch: c.gchar) u8 {
    const b: u8 = @bitCast(ch);
    if (b >= '0' and b <= '9') return b - '0';
    if (b >= 'a' and b <= 'f') return b - 'a' + 10;
    if (b >= 'A' and b <= 'F') return b - 'A' + 10;
    return 0;
}

fn solvePow(salt: [*c]const c.gchar, target_hex: [*c]const c.gchar) u64 {
    var target_len = cstr.len(target_hex) / 2;
    if (target_len > 32) {
        target_len = 32;
    }

    var target: [32]u8 = undefined;
    for (0..target_len) |i| {
        target[i] = (hexNibble(target_hex[2 * i]) << 4) | hexNibble(target_hex[2 * i + 1]);
    }

    var nonce: u64 = 0;
    const max_iterations = 10_000_000;
    var nonce_buf: [32]u8 = undefined;

    while (nonce < max_iterations) : (nonce += 1) {
        const checksum = c.g_checksum_new(c.G_CHECKSUM_SHA256);
        const nonce_text = std.fmt.bufPrintZ(&nonce_buf, "{}", .{nonce}) catch return 0;
        c.g_checksum_update(checksum, @ptrCast(salt), @intCast(cstr.len(salt)));
        c.g_checksum_update(checksum, @ptrCast(nonce_text.ptr), @intCast(nonce_text.len));

        var digest: [32]u8 = undefined;
        var digest_len: c.gsize = digest.len;
        c.g_checksum_get_digest(checksum, &digest, &digest_len);
        c.g_checksum_free(checksum);

        var found = true;
        for (0..target_len) |i| {
            if (digest[i] != target[i]) {
                found = false;
                break;
            }
        }
        if (found) {
            return nonce;
        }
    }

    logMsg(c.G_LOG_LEVEL_WARNING, "solve_pow: Max iterations reached, challenge too difficult", .{});
    return 0;
}

fn appendInt(seed: [*c]const c.gchar, index: c_long, suffix: []const u8) [*c]c.gchar {
    const seed_len = cstr.len(seed);
    var index_buf: [32]u8 = undefined;
    const index_text = std.fmt.bufPrint(&index_buf, "{}", .{index}) catch return null;
    const total_len = seed_len + index_text.len + suffix.len;
    const out: [*c]c.gchar = @ptrCast(c.g_malloc(total_len + 1));
    if (out == null) {
        return null;
    }

    @memcpy(out[0..seed_len], seed[0..seed_len]);
    @memcpy(out[seed_len .. seed_len + index_text.len], index_text);
    @memcpy(out[seed_len + index_text.len .. total_len], suffix);
    out[total_len] = 0;
    return out;
}

fn solveChallengeInternal(obj: ?*c.JsonObject, token: [*c]const c.gchar) ?*c.JsonArray {
    const solutions = c.json_array_new();

    if (c.json_object_has_member(obj, "c") != FALSE and
        c.json_object_has_member(obj, "s") != FALSE and
        c.json_object_has_member(obj, "d") != FALSE)
    {
        const count = c.json_object_get_int_member(obj, "c");
        const salt_len = c.json_object_get_int_member(obj, "s");
        const difficulty = c.json_object_get_int_member(obj, "d");

        var i: c_long = 1;
        while (i <= count) : (i += 1) {
            const seed_salt = appendInt(token, i, "");
            const seed_target = appendInt(token, i, "d");
            const salt = capPrngGen(seed_salt, salt_len);
            const target = capPrngGen(seed_target, difficulty);

            const nonce = solvePow(salt, target);
            c.json_array_add_int_element(solutions, @intCast(nonce));

            c.g_free(salt);
            c.g_free(target);
            c.g_free(seed_salt);
            c.g_free(seed_target);
        }
    }

    return solutions;
}

export fn solve_challenge_internal(obj: ?*c.JsonObject, token: [*c]const c.gchar) ?*c.JsonArray {
    return solveChallengeInternal(obj, token);
}

export fn solve_challenge(challenge_json: [*c]const c.gchar, token: [*c]const c.gchar) [*c]c.gchar {
    const parser = c.json_parser_new();
    if (c.json_parser_load_from_data(parser, challenge_json, -1, null) == FALSE) {
        c.g_object_unref(parser);
        return null;
    }

    const root = c.json_parser_get_root(parser);
    if (root == null or !c.JSON_NODE_HOLDS_OBJECT(root)) {
        c.g_object_unref(parser);
        return null;
    }

    const solutions = solveChallengeInternal(c.json_node_get_object(root), token);

    const gen = c.json_generator_new();
    const solutions_node = c.json_node_new(c.JSON_NODE_ARRAY);
    c.json_node_set_array(solutions_node, solutions);
    c.json_generator_set_root(gen, solutions_node);
    const res = c.json_generator_to_data(gen, null);

    c.g_object_unref(gen);
    c.g_object_unref(parser);

    return res;
}

export fn check_and_solve_challenge(response_json: [*c]const c.gchar) [*c]c.gchar {
    const parser = c.json_parser_new();
    if (c.json_parser_load_from_data(parser, response_json, -1, null) == FALSE) {
        c.g_object_unref(parser);
        return null;
    }

    const root = c.json_parser_get_root(parser);
    if (root == null or !c.JSON_NODE_HOLDS_OBJECT(root)) {
        c.g_object_unref(parser);
        return null;
    }
    const obj = c.json_node_get_object(root);

    var redeemed_token: [*c]c.gchar = null;

    if (c.json_object_has_member(obj, "challenge") != FALSE and c.json_object_has_member(obj, "token") != FALSE) {
        const token = c.json_object_get_string_member(obj, "token");
        const challenge_obj = c.json_object_get_object_member(obj, "challenge");
        const solutions = solveChallengeInternal(challenge_obj, token);

        const builder = c.json_builder_new();
        _ = c.json_builder_begin_object(builder);
        _ = c.json_builder_set_member_name(builder, "token");
        _ = c.json_builder_add_string_value(builder, token);
        _ = c.json_builder_set_member_name(builder, "solutions");
        _ = c.json_builder_add_value(builder, c.json_node_init_array(c.json_node_alloc(), solutions));
        _ = c.json_builder_end_object(builder);

        const gen = c.json_generator_new();
        c.json_generator_set_root(gen, c.json_builder_get_root(builder));
        const post_data = c.json_generator_to_data(gen, null);

        var chunk: types.MemoryStruct = .{ .memory = null, .size = 0 };
        var response_code: c_long = 0;
        if (fetch_url_internal(constants.CAP_REDEEM_URL, &chunk, post_data, "POST", &response_code) != FALSE) {
            const redeem_parser = c.json_parser_new();
            if (c.json_parser_load_from_data(redeem_parser, chunk.memory, -1, null) != FALSE) {
                const r_root = c.json_parser_get_root(redeem_parser);
                if (r_root != null and c.JSON_NODE_HOLDS_OBJECT(r_root)) {
                    const r_obj = c.json_node_get_object(r_root);
                    if (c.json_object_has_member(r_obj, "success") != FALSE and c.json_object_get_boolean_member(r_obj, "success") != FALSE) {
                        redeemed_token = c.g_strdup(c.json_object_get_string_member(r_obj, "token"));
                    }
                }
            }
            c.g_object_unref(redeem_parser);
            c.g_free(chunk.memory);
        }

        c.g_free(post_data);
        c.g_object_unref(gen);
        c.g_object_unref(builder);
    }

    c.g_object_unref(parser);
    return redeemed_token;
}
