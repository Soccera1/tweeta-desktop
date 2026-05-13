const c = @import("c.zig").c;
const cstr = @import("cstr.zig");
const constants = @import("constants.zig");
const types = @import("types.zig");

const FALSE: c.gboolean = 0;
const TRUE: c.gboolean = 1;
const client_header = "Tweeta Desktop; 1.0.0";
const user_agent = "TweetaDesktop/1.0.0";
const file_user_agent = "libcurl-agent/1.0";

extern var g_auth_token: [*c]c.gchar;
extern var g_globals_mutex: c.GMutex;
extern fn check_and_solve_challenge(response_json: [*c]const c.gchar) [*c]c.gchar;

fn lit(comptime value: [:0]const u8) [*c]const c.gchar {
    return @ptrCast(value.ptr);
}

fn logMsg(level: c.GLogLevelFlags, comptime fmt: [:0]const u8, args: anytype) void {
    @call(.auto, c.g_log, .{ @as([*c]const c.gchar, null), level, lit(fmt) } ++ args);
}

fn cLen(value: [*c]const c.gchar) usize {
    return cstr.len(value);
}

fn concat2(left: [*c]const c.gchar, right: [*c]const c.gchar) [*c]c.gchar {
    const left_len = cLen(left);
    const right_len = cLen(right);
    const out: [*c]c.gchar = @ptrCast(c.g_malloc(left_len + right_len + 1));
    if (out == null) return null;
    if (left_len > 0) @memcpy(out[0..left_len], left[0..left_len]);
    if (right_len > 0) @memcpy(out[left_len .. left_len + right_len], right[0..right_len]);
    out[left_len + right_len] = 0;
    return out;
}

fn concatBearer(token: [*c]const c.gchar) [*c]c.gchar {
    return c.g_strdup_printf("Authorization: Bearer %s", token);
}

fn writeMemoryCallback(contents: ?*anyopaque, size: usize, nmemb: usize, userp: ?*anyopaque) callconv(.c) usize {
    const real_size = size * nmemb;
    const mem: [*c]types.MemoryStruct = @ptrCast(@alignCast(userp));
    const ptr: [*c]u8 = @ptrCast(c.g_realloc(mem.*.memory, mem.*.size + real_size + 1));
    if (ptr == null) {
        logMsg(c.G_LOG_LEVEL_CRITICAL, "not enough memory (g_realloc returned NULL)", .{});
        return 0;
    }

    mem.*.memory = @ptrCast(ptr);
    const src: [*]const u8 = @ptrCast(contents.?);
    @memcpy(ptr[mem.*.size .. mem.*.size + real_size], src[0..real_size]);
    mem.*.size += real_size;
    ptr[mem.*.size] = 0;
    return real_size;
}

fn getRuntimeApiBaseUrl() [*c]const c.gchar {
    const env = c.g_getenv("TWEETA_API_BASE_URL");
    return if (env != null and env[0] != 0) env else constants.API_BASE_URL;
}

fn getRuntimeBaseDomain() [*c]const c.gchar {
    const env = c.g_getenv("TWEETA_BASE_DOMAIN");
    return if (env != null and env[0] != 0) env else constants.BASE_DOMAIN;
}

fn concatBaseSuffix(base: [*c]const c.gchar, url: [*c]const c.gchar, prefix: [*c]const c.gchar) [*c]c.gchar {
    return concat2(base, url + cstr.len(prefix));
}

fn rewriteUrlForRuntimeBase(url: [*c]const c.gchar) [*c]c.gchar {
    if (url == null) return null;

    const runtime_api_base = getRuntimeApiBaseUrl();
    if (c.g_str_has_prefix(url, constants.API_BASE_URL) != FALSE and c.g_strcmp0(runtime_api_base, constants.API_BASE_URL) != 0) {
        return concatBaseSuffix(runtime_api_base, url, constants.API_BASE_URL);
    }

    const runtime_base_domain = getRuntimeBaseDomain();
    if (c.g_str_has_prefix(url, constants.BASE_DOMAIN) != FALSE and c.g_strcmp0(runtime_base_domain, constants.BASE_DOMAIN) != 0) {
        return concatBaseSuffix(runtime_base_domain, url, constants.BASE_DOMAIN);
    }

    return c.g_strdup(url);
}

fn getAuthTokenSafe() [*c]c.gchar {
    c.g_mutex_lock(&g_globals_mutex);
    const token = if (g_auth_token != null) c.g_strdup(g_auth_token) else null;
    logMsg(c.G_LOG_LEVEL_DEBUG, "get_auth_token_safe: token=%s (length=%d)", .{ if (token != null) token else lit("(null)"), if (token != null) @as(c_int, @intCast(cstr.len(token))) else 0 });
    c.g_mutex_unlock(&g_globals_mutex);
    return token;
}

fn dupEffectiveAuthToken(auth_token_override: [*c]const c.gchar) [*c]c.gchar {
    if (auth_token_override != null and auth_token_override[0] != 0) {
        return c.g_strdup(auth_token_override);
    }
    return getAuthTokenSafe();
}

export fn fetch_url_internal(
    url: [*c]const c.gchar,
    chunk: [*c]types.MemoryStruct,
    post_data: [*c]const c.gchar,
    method: [*c]const c.gchar,
    response_code: [*c]c_long,
) c.gboolean {
    return fetchUrlInternalWithAuth(url, chunk, post_data, method, response_code, null);
}

export fn fetch_url_internal_with_auth(
    url: [*c]const c.gchar,
    chunk: [*c]types.MemoryStruct,
    post_data: [*c]const c.gchar,
    method: [*c]const c.gchar,
    response_code: [*c]c_long,
    auth_token_override: [*c]const c.gchar,
) c.gboolean {
    return fetchUrlInternalWithAuth(url, chunk, post_data, method, response_code, auth_token_override);
}

fn fetchUrlInternalWithAuth(
    url: [*c]const c.gchar,
    chunk: [*c]types.MemoryStruct,
    post_data: [*c]const c.gchar,
    method: [*c]const c.gchar,
    response_code: [*c]c_long,
    auth_token_override: [*c]const c.gchar,
) c.gboolean {
    if (url == null) {
        logMsg(c.G_LOG_LEVEL_CRITICAL, "fetch_url_internal: URL is NULL", .{});
        return FALSE;
    }

    if (chunk.*.memory != null) c.g_free(chunk.*.memory);
    chunk.*.memory = @ptrCast(c.g_malloc(1));
    chunk.*.size = 0;
    chunk.*.memory[0] = 0;

    const curl_handle = c.curl_easy_init() orelse {
        logMsg(c.G_LOG_LEVEL_CRITICAL, "curl_easy_init() failed", .{});
        c.g_free(chunk.*.memory);
        chunk.*.memory = null;
        return FALSE;
    };

    const request_url = rewriteUrlForRuntimeBase(url);
    _ = c.curl_easy_setopt(curl_handle, c.CURLOPT_URL, request_url);
    _ = c.curl_easy_setopt(curl_handle, c.CURLOPT_WRITEFUNCTION, writeMemoryCallback);
    _ = c.curl_easy_setopt(curl_handle, c.CURLOPT_WRITEDATA, chunk);
    _ = c.curl_easy_setopt(curl_handle, c.CURLOPT_USERAGENT, lit(user_agent));
    _ = c.curl_easy_setopt(curl_handle, c.CURLOPT_SSL_VERIFYPEER, @as(c_long, 1));
    _ = c.curl_easy_setopt(curl_handle, c.CURLOPT_SSL_VERIFYHOST, @as(c_long, 2));
    _ = c.curl_easy_setopt(curl_handle, c.CURLOPT_TIMEOUT, @as(c_long, 30));
    _ = c.curl_easy_setopt(curl_handle, c.CURLOPT_CONNECTTIMEOUT, @as(c_long, 10));
    _ = c.curl_easy_setopt(curl_handle, c.CURLOPT_FOLLOWLOCATION, @as(c_long, 1));

    var headers: [*c]c.struct_curl_slist = null;
    headers = c.curl_slist_append(headers, "Content-Type: application/json");
    headers = c.curl_slist_append(headers, "Accept: application/json");
    headers = c.curl_slist_append(headers, "X-Tweetapus-Client: " ++ client_header);
    const auth_token = dupEffectiveAuthToken(auth_token_override);
    logMsg(c.G_LOG_LEVEL_DEBUG, "fetch_url_internal: url=%s, request_url=%s, method=%s, has_auth_token=%d", .{ url, request_url, if (method != null) method else lit("(null)"), if (auth_token != null) TRUE else FALSE });
    if (auth_token != null) {
        const auth_header = concatBearer(auth_token);
        headers = c.curl_slist_append(headers, auth_header);
        c.g_free(auth_header);
    }
    c.g_free(auth_token);
    _ = c.curl_easy_setopt(curl_handle, c.CURLOPT_HTTPHEADER, headers);

    if (method != null) _ = c.curl_easy_setopt(curl_handle, c.CURLOPT_CUSTOMREQUEST, method);
    if (post_data != null) _ = c.curl_easy_setopt(curl_handle, c.CURLOPT_POSTFIELDS, post_data);

    const res = c.curl_easy_perform(curl_handle);
    if (res == c.CURLE_OK) {
        _ = c.curl_easy_getinfo(curl_handle, c.CURLINFO_RESPONSE_CODE, response_code);
    }

    c.curl_slist_free_all(headers);
    if (res != c.CURLE_OK) {
        logMsg(c.G_LOG_LEVEL_CRITICAL, "curl_easy_perform() failed: %s", .{c.curl_easy_strerror(res)});
        c.g_free(chunk.*.memory);
        chunk.*.memory = null;
        chunk.*.size = 0;
        c.curl_easy_cleanup(curl_handle);
        c.g_free(request_url);
        return FALSE;
    }

    c.curl_easy_cleanup(curl_handle);
    c.g_free(request_url);
    return TRUE;
}

export fn fetch_url(
    url: [*c]const c.gchar,
    chunk: [*c]types.MemoryStruct,
    post_data: [*c]const c.gchar,
    method: [*c]const c.gchar,
) c.gboolean {
    return fetch_url_with_auth_token(url, chunk, post_data, method, null);
}

fn addCapTokenToPayload(post_data: [*c]const c.gchar, cap_token: [*c]const c.gchar, default_to_object: bool) [*c]c.gchar {
    if (post_data != null) {
        const parser = c.json_parser_new();
        defer c.g_object_unref(parser);
        if (c.json_parser_load_from_data(parser, post_data, -1, null) != FALSE) {
            const root = c.json_parser_get_root(parser);
            if (root != null and c.JSON_NODE_HOLDS_OBJECT(root)) {
                const obj = c.json_node_get_object(root);
                c.json_object_set_string_member(obj, "capToken", cap_token);
                const gen = c.json_generator_new();
                c.json_generator_set_root(gen, root);
                const data = c.json_generator_to_data(gen, null);
                c.g_object_unref(gen);
                return data;
            }
        }
    } else if (default_to_object) {
        const builder = c.json_builder_new();
        _ = c.json_builder_begin_object(builder);
        _ = c.json_builder_set_member_name(builder, "capToken");
        _ = c.json_builder_add_string_value(builder, cap_token);
        _ = c.json_builder_end_object(builder);
        const gen = c.json_generator_new();
        c.json_generator_set_root(gen, c.json_builder_get_root(builder));
        const data = c.json_generator_to_data(gen, null);
        c.g_object_unref(gen);
        c.g_object_unref(builder);
        return data;
    }
    return null;
}

fn responseNeedsCap(response_body: [*c]const c.gchar, response_code: c_long) bool {
    var needs_cap = false;
    const parser = c.json_parser_new();
    if (c.json_parser_load_from_data(parser, response_body, -1, null) != FALSE) {
        const root = c.json_parser_get_root(parser);
        if (root != null and c.JSON_NODE_HOLDS_OBJECT(root)) {
            const obj = c.json_node_get_object(root);
            if (c.json_object_has_member(obj, "error") != FALSE) {
                const error_msg = c.json_object_get_string_member(obj, "error");
                if (c.g_str_has_prefix(error_msg, "Challenge token is required") != FALSE or
                    c.g_str_has_prefix(error_msg, "Rate limit exceeded") != FALSE or
                    response_code == 429)
                {
                    needs_cap = true;
                }
            }
        }
    }
    c.g_object_unref(parser);
    return needs_cap;
}

export fn fetch_url_with_auth_token(
    url: [*c]const c.gchar,
    chunk: [*c]types.MemoryStruct,
    post_data: [*c]const c.gchar,
    method: [*c]const c.gchar,
    auth_token_override: [*c]const c.gchar,
) c.gboolean {
    var response_code: c_long = 0;
    chunk.*.memory = null;
    chunk.*.size = 0;

    if (fetchUrlInternalWithAuth(url, chunk, post_data, method, &response_code, auth_token_override) == FALSE) {
        return FALSE;
    }

    var cap_token = check_and_solve_challenge(chunk.*.memory);
    if (cap_token != null) {
        logMsg(c.G_LOG_LEVEL_MESSAGE, "Challenge detected and solved. Retrying request with capToken.", .{});
        const new_post_data = addCapTokenToPayload(post_data, cap_token, true);
        const success = fetchUrlInternalWithAuth(url, chunk, new_post_data, if (method != null) method else "POST", &response_code, auth_token_override);
        c.g_free(new_post_data);
        c.g_free(cap_token);
        return success;
    }

    if ((response_code == 429 or response_code == 403 or response_code == 400) and responseNeedsCap(chunk.*.memory, response_code)) {
        logMsg(c.G_LOG_LEVEL_MESSAGE, "Challenge token required. Fetching new challenge.", .{});
        var challenge_chunk: types.MemoryStruct = .{ .memory = null, .size = 0 };
        if (fetchUrlInternalWithAuth(constants.CAP_CHALLENGE_URL, &challenge_chunk, "{}", "POST", &response_code, auth_token_override) != FALSE) {
            cap_token = check_and_solve_challenge(challenge_chunk.memory);
            c.g_free(challenge_chunk.memory);

            if (cap_token != null) {
                logMsg(c.G_LOG_LEVEL_MESSAGE, "Fetched and solved new challenge. Retrying original request.", .{});
                if (response_code == 429) {
                    var bypass_chunk: types.MemoryStruct = .{ .memory = null, .size = 0 };
                    const bypass_data = c.g_strdup_printf("{\"capToken\": \"%s\"}", cap_token);
                    _ = fetchUrlInternalWithAuth(constants.CAP_RATE_LIMIT_BYPASS_URL, &bypass_chunk, bypass_data, "POST", &response_code, auth_token_override);
                    c.g_free(bypass_data);
                    c.g_free(bypass_chunk.memory);
                }

                const new_post_data = addCapTokenToPayload(post_data, cap_token, false);
                const success = fetchUrlInternalWithAuth(url, chunk, new_post_data, method, &response_code, auth_token_override);
                c.g_free(new_post_data);
                c.g_free(cap_token);
                return success;
            }
        }
    }

    return TRUE;
}

export fn fetch_url_with_file(
    url: [*c]const c.gchar,
    chunk: [*c]types.MemoryStruct,
    file_path: [*c]const c.gchar,
    field_name: [*c]const c.gchar,
) c.gboolean {
    return fetch_url_with_file_auth_token(url, chunk, file_path, field_name, null);
}

export fn fetch_url_with_file_auth_token(
    url: [*c]const c.gchar,
    chunk: [*c]types.MemoryStruct,
    file_path: [*c]const c.gchar,
    field_name: [*c]const c.gchar,
    auth_token_override: [*c]const c.gchar,
) c.gboolean {
    if (url == null or file_path == null) {
        logMsg(c.G_LOG_LEVEL_CRITICAL, "fetch_url_with_file: URL or file_path is NULL", .{});
        return FALSE;
    }
    logMsg(c.G_LOG_LEVEL_DEBUG, "fetch_url_with_file: url=%s, file_path=%s, field_name=%s", .{ url, file_path, if (field_name != null) field_name else lit("(null)") });

    if (chunk.*.memory != null) c.g_free(chunk.*.memory);
    chunk.*.memory = @ptrCast(c.g_malloc(1));
    chunk.*.size = 0;
    chunk.*.memory[0] = 0;

    const curl_handle = c.curl_easy_init() orelse {
        logMsg(c.G_LOG_LEVEL_CRITICAL, "curl_easy_init() failed", .{});
        c.g_free(chunk.*.memory);
        chunk.*.memory = null;
        return FALSE;
    };

    const mime = c.curl_mime_init(curl_handle) orelse {
        logMsg(c.G_LOG_LEVEL_CRITICAL, "curl_mime_init() failed", .{});
        c.g_free(chunk.*.memory);
        chunk.*.memory = null;
        c.curl_easy_cleanup(curl_handle);
        return FALSE;
    };

    const part = c.curl_mime_addpart(mime);
    _ = c.curl_mime_name(part, if (field_name != null) field_name else "file");
    _ = c.curl_mime_filedata(part, file_path);
    logMsg(c.G_LOG_LEVEL_DEBUG, "fetch_url_with_file: added mime part for file=%s", .{file_path});

    _ = c.curl_easy_setopt(curl_handle, c.CURLOPT_URL, url);
    _ = c.curl_easy_setopt(curl_handle, c.CURLOPT_WRITEFUNCTION, writeMemoryCallback);
    _ = c.curl_easy_setopt(curl_handle, c.CURLOPT_WRITEDATA, chunk);
    _ = c.curl_easy_setopt(curl_handle, c.CURLOPT_USERAGENT, lit(file_user_agent));
    _ = c.curl_easy_setopt(curl_handle, c.CURLOPT_SSL_VERIFYPEER, @as(c_long, 1));
    _ = c.curl_easy_setopt(curl_handle, c.CURLOPT_SSL_VERIFYHOST, @as(c_long, 2));
    _ = c.curl_easy_setopt(curl_handle, c.CURLOPT_TIMEOUT, @as(c_long, 60));
    _ = c.curl_easy_setopt(curl_handle, c.CURLOPT_CONNECTTIMEOUT, @as(c_long, 10));
    _ = c.curl_easy_setopt(curl_handle, c.CURLOPT_FOLLOWLOCATION, @as(c_long, 1));
    _ = c.curl_easy_setopt(curl_handle, c.CURLOPT_MIMEPOST, mime);

    var headers: [*c]c.struct_curl_slist = null;
    const auth_token = dupEffectiveAuthToken(auth_token_override);
    if (auth_token != null) {
        const auth_header = concatBearer(auth_token);
        headers = c.curl_slist_append(headers, auth_header);
        c.g_free(auth_header);
    }
    c.g_free(auth_token);
    _ = c.curl_easy_setopt(curl_handle, c.CURLOPT_HTTPHEADER, headers);

    logMsg(c.G_LOG_LEVEL_DEBUG, "fetch_url_with_file: performing curl request", .{});
    const res = c.curl_easy_perform(curl_handle);
    logMsg(c.G_LOG_LEVEL_DEBUG, "fetch_url_with_file: curl perform result=%d (%s)", .{ res, c.curl_easy_strerror(res) });
    c.curl_slist_free_all(headers);
    c.curl_mime_free(mime);

    if (res != c.CURLE_OK) {
        logMsg(c.G_LOG_LEVEL_CRITICAL, "curl_easy_perform() failed: %s", .{c.curl_easy_strerror(res)});
        c.g_free(chunk.*.memory);
        chunk.*.memory = null;
        chunk.*.size = 0;
        c.curl_easy_cleanup(curl_handle);
        return FALSE;
    }

    logMsg(c.G_LOG_LEVEL_DEBUG, "fetch_url_with_file: request succeeded, response_size=%zu", .{chunk.*.size});
    if (chunk.*.memory != null and chunk.*.size > 0) {
        logMsg(c.G_LOG_LEVEL_DEBUG, "fetch_url_with_file: response=%s", .{chunk.*.memory});
    }
    c.curl_easy_cleanup(curl_handle);
    return TRUE;
}
