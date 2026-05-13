const std = @import("std");
const config = @import("config");
const constants = @import("constants.zig");
const types = @import("types.zig");

const c = if (config.use_fido2) @cImport({
    @cInclude("glib.h");
    @cInclude("json-glib/json-glib.h");
    @cInclude("fido.h");
    @cInclude("openssl/sha.h");
}) else @cImport({
    @cInclude("glib.h");
});

const FALSE: c.gboolean = 0;
const TRUE: c.gboolean = 1;

extern fn fetch_url(url: [*c]const c.gchar, chunk: [*c]types.MemoryStruct, post_data: [*c]const c.gchar, method: [*c]const c.gchar) c.gboolean;

fn lit(comptime value: [:0]const u8) [*c]const c.gchar {
    return @ptrCast(value.ptr);
}

fn cLen(value: [*c]const c.gchar) usize {
    if (value == null) return 0;
    return std.mem.len(@as([*:0]const u8, @ptrCast(value)));
}

export fn webauthn_fido2_is_enabled() c.gboolean {
    return if (config.use_fido2) TRUE else FALSE;
}

export fn webauthn_fido2_login(response_json_out: [*c][*c]c.gchar, error_out: [*c][*c]c.gchar) c.gboolean {
    if (!config.use_fido2) {
        setError(error_out, "Native passkey support was not enabled at build time.");
        return FALSE;
    }
    return fido2Login(response_json_out, error_out);
}

export fn webauthn_fido2_register(username: [*c]const c.gchar, response_json_out: [*c][*c]c.gchar, error_out: [*c][*c]c.gchar) c.gboolean {
    if (!config.use_fido2) {
        setError(error_out, "Native passkey support was not enabled at build time.");
        return FALSE;
    }
    return fido2Register(username, response_json_out, error_out);
}

fn setError(error_out: [*c][*c]c.gchar, comptime message: [:0]const u8) void {
    if (error_out != null) {
        error_out.* = c.g_strdup(lit(message));
    }
}

const ByteBuf = struct {
    data: [*c]c.guchar = null,
    len: c.gsize = 0,
};

fn byteBufClear(buf: ?*ByteBuf) void {
    const ptr = buf orelse return;
    c.g_free(ptr.data);
    ptr.data = null;
    ptr.len = 0;
}

fn extractErrorMessage(json_data: [*c]const c.gchar) [*c]c.gchar {
    const parser = c.json_parser_new();
    defer c.g_object_unref(parser);
    var message: [*c]c.gchar = null;

    if (json_data != null and c.json_parser_load_from_data(parser, json_data, -1, null) != FALSE) {
        const root = c.json_parser_get_root(parser);
        if (root != null and c.JSON_NODE_HOLDS_OBJECT(root)) {
            const obj = c.json_node_get_object(root);
            if (c.json_object_has_member(obj, "error") != FALSE and c.json_node_is_null(c.json_object_get_member(obj, "error")) == FALSE) {
                message = c.g_strdup(c.json_object_get_string_member(obj, "error"));
            }
        }
    }

    return message;
}

fn base64urlEncode(data: [*c]const c.guchar, len: c.gsize) [*c]c.gchar {
    const b64 = c.g_base64_encode(data, len);
    var i: usize = 0;
    while (b64[i] != 0) : (i += 1) {
        if (b64[i] == '+') {
            b64[i] = '-';
        } else if (b64[i] == '/') {
            b64[i] = '_';
        } else if (b64[i] == '=') {
            b64[i] = 0;
            break;
        }
    }
    return b64;
}

fn base64urlDecode(value: [*c]const c.gchar, out: *ByteBuf) c.gboolean {
    if (value == null) return FALSE;

    var b64 = c.g_strdup(value);
    var i: usize = 0;
    while (b64[i] != 0) : (i += 1) {
        if (b64[i] == '-') {
            b64[i] = '+';
        } else if (b64[i] == '_') {
            b64[i] = '/';
        }
    }

    const len = cLen(b64);
    if (len % 4 != 0) {
        const padded = c.g_strnfill(len + (4 - (len % 4)), '=');
        @memcpy(padded[0..len], b64[0..len]);
        c.g_free(b64);
        b64 = padded;
    }

    out.data = c.g_base64_decode(b64, &out.len);
    c.g_free(b64);
    return if (out.data != null) TRUE else FALSE;
}

fn runtimeOrigin() [*c]const c.gchar {
    const env = c.g_getenv("TWEETA_BASE_DOMAIN");
    return if (env != null and env[0] != 0) env else constants.BASE_DOMAIN;
}

fn buildClientDataJson(kind: [*c]const c.gchar, challenge: [*c]const c.gchar) [*c]c.gchar {
    const builder = c.json_builder_new();
    const gen = c.json_generator_new();
    defer c.g_object_unref(gen);
    defer c.g_object_unref(builder);

    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_set_member_name(builder, "type");
    _ = c.json_builder_add_string_value(builder, kind);
    _ = c.json_builder_set_member_name(builder, "challenge");
    _ = c.json_builder_add_string_value(builder, challenge);
    _ = c.json_builder_set_member_name(builder, "origin");
    _ = c.json_builder_add_string_value(builder, runtimeOrigin());
    _ = c.json_builder_set_member_name(builder, "crossOrigin");
    _ = c.json_builder_add_boolean_value(builder, FALSE);
    _ = c.json_builder_end_object(builder);

    const root = c.json_builder_get_root(builder);
    defer c.json_node_free(root);
    c.json_generator_set_root(gen, root);
    return c.json_generator_to_data(gen, null);
}

fn sha256Bytes(data: [*c]const c.gchar, out: [*c]c.guchar) void {
    _ = c.SHA256(@ptrCast(data), cLen(data), out);
}

fn parseResponseObject(parser_out: *?*c.JsonParser, json_data: [*c]const c.gchar, error_out: [*c][*c]c.gchar) ?*c.JsonObject {
    const parser = c.json_parser_new();
    var err: [*c]c.GError = null;

    if (c.json_parser_load_from_data(parser, json_data, -1, &err) == FALSE) {
        if (error_out != null) {
            error_out.* = c.g_strdup(if (err != null) err.*.message else lit("Invalid server response."));
        }
        if (err != null) c.g_error_free(err);
        c.g_object_unref(parser);
        return null;
    }

    const root = c.json_parser_get_root(parser);
    if (root == null or !c.JSON_NODE_HOLDS_OBJECT(root)) {
        setError(error_out, "Invalid server response.");
        c.g_object_unref(parser);
        return null;
    }

    parser_out.* = parser;
    return c.json_node_get_object(root);
}

fn openFirstFidoDevice(dev_out: *?*c.fido_dev_t, error_out: [*c][*c]c.gchar) c.gboolean {
    const max_devices: usize = 16;
    var infos = c.fido_dev_info_new(max_devices);
    var count: usize = 0;

    var rc = c.fido_dev_info_manifest(infos, max_devices, &count);
    if (rc != c.FIDO_OK or count == 0) {
        if (error_out != null) {
            error_out.* = c.g_strdup(if (count == 0) lit("No FIDO2 security key was found.") else c.fido_strerr(rc));
        }
        c.fido_dev_info_free(&infos, max_devices);
        return FALSE;
    }

    var i: usize = 0;
    while (i < count) : (i += 1) {
        const info = c.fido_dev_info_ptr(infos, i);
        const path = c.fido_dev_info_path(info);
        var dev = c.fido_dev_new();
        rc = c.fido_dev_open(dev, path);
        if (rc == c.FIDO_OK) {
            dev_out.* = dev;
            c.fido_dev_info_free(&infos, max_devices);
            return TRUE;
        }
        c.fido_dev_free(&dev);
    }

    setError(error_out, "Could not open a FIDO2 security key.");
    c.fido_dev_info_free(&infos, max_devices);
    return FALSE;
}

fn cborAppendTypeLen(out: [*c]c.GByteArray, major: c.guint8, len: c.gsize) void {
    if (len < 24) {
        var b: c.guint8 = @intCast((major << 5) | len);
        _ = c.g_byte_array_append(out, &b, 1);
    } else if (len <= 0xff) {
        var b = [_]c.guint8{ @intCast((major << 5) | 24), @intCast(len) };
        _ = c.g_byte_array_append(out, &b, b.len);
    } else if (len <= 0xffff) {
        var b = [_]c.guint8{ @intCast((major << 5) | 25), @intCast(len >> 8), @intCast(len) };
        _ = c.g_byte_array_append(out, &b, b.len);
    } else {
        var b = [_]c.guint8{ @intCast((major << 5) | 26), @intCast(len >> 24), @intCast(len >> 16), @intCast(len >> 8), @intCast(len) };
        _ = c.g_byte_array_append(out, &b, b.len);
    }
}

fn cborAppendText(out: [*c]c.GByteArray, text: [*c]const c.gchar) void {
    const len = cLen(text);
    cborAppendTypeLen(out, 3, len);
    if (len != 0) _ = c.g_byte_array_append(out, @ptrCast(text), @intCast(len));
}

fn cborAppendBytes(out: [*c]c.GByteArray, bytes: [*c]const c.guchar, len: c.gsize) void {
    cborAppendTypeLen(out, 2, len);
    if (len != 0) _ = c.g_byte_array_append(out, bytes, @intCast(len));
}

fn buildAttestationObject(cred: ?*c.fido_cred_t, out: *ByteBuf) c.gboolean {
    const fmt = c.fido_cred_fmt(cred);
    const authdata = c.fido_cred_authdata_raw_ptr(cred);
    const authdata_len = c.fido_cred_authdata_raw_len(cred);
    const attstmt = c.fido_cred_attstmt_ptr(cred);
    const attstmt_len = c.fido_cred_attstmt_len(cred);
    if (fmt == null or authdata == null or authdata_len == 0 or attstmt == null or attstmt_len == 0) return FALSE;

    const bytes = c.g_byte_array_new();
    var map3: c.guint8 = 0xa3;
    _ = c.g_byte_array_append(bytes, &map3, 1);
    cborAppendText(bytes, "fmt");
    cborAppendText(bytes, fmt);
    cborAppendText(bytes, "authData");
    cborAppendBytes(bytes, authdata, authdata_len);
    cborAppendText(bytes, "attStmt");
    _ = c.g_byte_array_append(bytes, attstmt, @intCast(attstmt_len));

    out.len = bytes.*.len;
    out.data = c.g_byte_array_free(bytes, FALSE);
    return TRUE;
}

fn jsonBuilderAddB64Member(builder: ?*c.JsonBuilder, name: [*c]const c.gchar, data: [*c]const c.guchar, len: c.gsize) void {
    const encoded = base64urlEncode(data, len);
    defer c.g_free(encoded);
    _ = c.json_builder_set_member_name(builder, name);
    _ = c.json_builder_add_string_value(builder, encoded);
}

fn buildAuthenticationCredentialJson(assertion: ?*c.fido_assert_t, client_data_json: [*c]const c.gchar) [*c]c.gchar {
    const builder = c.json_builder_new();
    const gen = c.json_generator_new();
    defer c.g_object_unref(gen);
    defer c.g_object_unref(builder);
    const raw_id = c.fido_assert_id_ptr(assertion, 0);
    const raw_id_len = c.fido_assert_id_len(assertion, 0);
    const user_id = c.fido_assert_user_id_ptr(assertion, 0);
    const user_id_len = c.fido_assert_user_id_len(assertion, 0);

    _ = c.json_builder_begin_object(builder);
    jsonBuilderAddB64Member(builder, "id", raw_id, raw_id_len);
    jsonBuilderAddB64Member(builder, "rawId", raw_id, raw_id_len);
    _ = c.json_builder_set_member_name(builder, "type");
    _ = c.json_builder_add_string_value(builder, "public-key");
    _ = c.json_builder_set_member_name(builder, "response");
    _ = c.json_builder_begin_object(builder);
    jsonBuilderAddB64Member(builder, "clientDataJSON", @ptrCast(client_data_json), cLen(client_data_json));
    jsonBuilderAddB64Member(builder, "authenticatorData", c.fido_assert_authdata_raw_ptr(assertion, 0), c.fido_assert_authdata_raw_len(assertion, 0));
    jsonBuilderAddB64Member(builder, "signature", c.fido_assert_sig_ptr(assertion, 0), c.fido_assert_sig_len(assertion, 0));
    _ = c.json_builder_set_member_name(builder, "userHandle");
    if (user_id != null and user_id_len > 0) {
        const encoded = base64urlEncode(user_id, user_id_len);
        defer c.g_free(encoded);
        _ = c.json_builder_add_string_value(builder, encoded);
    } else {
        _ = c.json_builder_add_null_value(builder);
    }
    _ = c.json_builder_end_object(builder);
    _ = c.json_builder_set_member_name(builder, "clientExtensionResults");
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_end_object(builder);
    _ = c.json_builder_end_object(builder);

    const root = c.json_builder_get_root(builder);
    defer c.json_node_free(root);
    c.json_generator_set_root(gen, root);
    return c.json_generator_to_data(gen, null);
}

fn buildRegistrationCredentialJson(cred: ?*c.fido_cred_t, client_data_json: [*c]const c.gchar, attestation_object: *ByteBuf) [*c]c.gchar {
    const builder = c.json_builder_new();
    const gen = c.json_generator_new();
    defer c.g_object_unref(gen);
    defer c.g_object_unref(builder);
    const raw_id = c.fido_cred_id_ptr(cred);
    const raw_id_len = c.fido_cred_id_len(cred);

    _ = c.json_builder_begin_object(builder);
    jsonBuilderAddB64Member(builder, "id", raw_id, raw_id_len);
    jsonBuilderAddB64Member(builder, "rawId", raw_id, raw_id_len);
    _ = c.json_builder_set_member_name(builder, "type");
    _ = c.json_builder_add_string_value(builder, "public-key");
    _ = c.json_builder_set_member_name(builder, "response");
    _ = c.json_builder_begin_object(builder);
    jsonBuilderAddB64Member(builder, "clientDataJSON", @ptrCast(client_data_json), cLen(client_data_json));
    jsonBuilderAddB64Member(builder, "attestationObject", attestation_object.data, attestation_object.len);
    _ = c.json_builder_set_member_name(builder, "transports");
    _ = c.json_builder_begin_array(builder);
    _ = c.json_builder_add_string_value(builder, "usb");
    _ = c.json_builder_end_array(builder);
    _ = c.json_builder_end_object(builder);
    _ = c.json_builder_set_member_name(builder, "clientExtensionResults");
    _ = c.json_builder_begin_object(builder);
    _ = c.json_builder_end_object(builder);
    _ = c.json_builder_end_object(builder);

    const root = c.json_builder_get_root(builder);
    defer c.json_node_free(root);
    c.json_generator_set_root(gen, root);
    return c.json_generator_to_data(gen, null);
}

fn fido2Login(response_json_out: [*c][*c]c.gchar, error_out: [*c][*c]c.gchar) c.gboolean {
    var options_chunk = std.mem.zeroes(types.MemoryStruct);
    var verify_chunk = std.mem.zeroes(types.MemoryStruct);
    var parser: ?*c.JsonParser = null;
    var client_data_json: [*c]c.gchar = null;
    var credential_json: [*c]c.gchar = null;
    var payload: [*c]c.gchar = null;
    var client_hash: [c.SHA256_DIGEST_LENGTH]c.guchar = undefined;
    var dev: ?*c.fido_dev_t = null;
    var assertion: ?*c.fido_assert_t = null;
    var success = FALSE;

    c.fido_init(0);
    if (fetch_url(constants.AUTH_PASSKEY_GENERATE_AUTHENTICATION_URL, &options_chunk, "{}", "POST") == FALSE) {
        setError(error_out, "Could not start passkey authentication.");
        return FALSE;
    }
    defer c.g_free(options_chunk.memory);
    defer c.g_free(verify_chunk.memory);
    defer if (parser != null) c.g_object_unref(parser);
    defer c.g_free(client_data_json);
    defer c.g_free(credential_json);
    defer c.g_free(payload);
    defer if (assertion != null) c.fido_assert_free(&assertion);
    defer if (dev != null) {
        _ = c.fido_dev_close(dev);
        c.fido_dev_free(&dev);
    };

    const root = parseResponseObject(&parser, options_chunk.memory, error_out);
    if (root == null) return FALSE;
    var server_error = extractErrorMessage(options_chunk.memory);
    if (server_error != null) {
        if (error_out != null) error_out.* = server_error else c.g_free(server_error);
        return FALSE;
    }

    const options = c.json_object_get_object_member(root, "options");
    const expected_challenge = c.json_object_get_string_member(root, "expectedChallenge");
    const challenge = c.json_object_get_string_member(options, "challenge");
    const rp_id = c.json_object_get_string_member(options, "rpId");
    if (challenge == null or rp_id == null or expected_challenge == null) {
        setError(error_out, "Authentication options were incomplete.");
        return FALSE;
    }

    client_data_json = buildClientDataJson("webauthn.get", challenge);
    sha256Bytes(client_data_json, &client_hash);
    if (openFirstFidoDevice(&dev, error_out) == FALSE) return FALSE;

    assertion = c.fido_assert_new();
    var rc = c.fido_assert_set_clientdata_hash(assertion, &client_hash, client_hash.len);
    if (rc == c.FIDO_OK) rc = c.fido_assert_set_rp(assertion, rp_id);
    if (rc == c.FIDO_OK) rc = c.fido_assert_set_up(assertion, c.FIDO_OPT_TRUE);
    if (rc == c.FIDO_OK) rc = c.fido_dev_get_assert(dev, assertion, null);
    if (rc != c.FIDO_OK) {
        if (error_out != null) error_out.* = c.g_strdup(c.fido_strerr(rc));
        return FALSE;
    }

    credential_json = buildAuthenticationCredentialJson(assertion, client_data_json);
    payload = c.g_strdup_printf("{\"expectedChallenge\":\"%s\",\"credential\":%s}", expected_challenge, credential_json);
    if (fetch_url(constants.AUTH_PASSKEY_VERIFY_AUTHENTICATION_URL, &verify_chunk, payload, "POST") == FALSE) {
        setError(error_out, "Could not verify passkey authentication.");
        return FALSE;
    }

    server_error = extractErrorMessage(verify_chunk.memory);
    if (server_error != null) {
        if (error_out != null) error_out.* = server_error else c.g_free(server_error);
        return FALSE;
    }

    if (response_json_out != null) response_json_out.* = c.g_strdup(verify_chunk.memory);
    success = TRUE;
    return success;
}

fn fido2Register(username: [*c]const c.gchar, response_json_out: [*c][*c]c.gchar, error_out: [*c][*c]c.gchar) c.gboolean {
    if (username == null or username[0] == 0) {
        setError(error_out, "Username is required.");
        return FALSE;
    }

    var options_chunk = std.mem.zeroes(types.MemoryStruct);
    var verify_chunk = std.mem.zeroes(types.MemoryStruct);
    var parser: ?*c.JsonParser = null;
    var request_payload: [*c]c.gchar = null;
    var client_data_json: [*c]c.gchar = null;
    var credential_json: [*c]c.gchar = null;
    var verify_payload: [*c]c.gchar = null;
    var user_id_bytes = ByteBuf{};
    var attestation_object = ByteBuf{};
    var client_hash: [c.SHA256_DIGEST_LENGTH]c.guchar = undefined;
    var dev: ?*c.fido_dev_t = null;
    var cred: ?*c.fido_cred_t = null;

    c.fido_init(0);
    request_payload = c.g_strdup_printf("{\"username\":\"%s\"}", username);
    if (fetch_url(constants.AUTH_PASSKEY_GENERATE_REGISTRATION_URL, &options_chunk, request_payload, "POST") == FALSE) {
        setError(error_out, "Could not start passkey registration.");
        c.g_free(request_payload);
        return FALSE;
    }
    defer c.g_free(options_chunk.memory);
    defer c.g_free(verify_chunk.memory);
    defer if (parser != null) c.g_object_unref(parser);
    defer byteBufClear(&user_id_bytes);
    defer byteBufClear(&attestation_object);
    defer c.g_free(request_payload);
    defer c.g_free(client_data_json);
    defer c.g_free(credential_json);
    defer c.g_free(verify_payload);
    defer if (cred != null) c.fido_cred_free(&cred);
    defer if (dev != null) {
        _ = c.fido_dev_close(dev);
        c.fido_dev_free(&dev);
    };

    const root = parseResponseObject(&parser, options_chunk.memory, error_out);
    if (root == null) return FALSE;
    var server_error = extractErrorMessage(options_chunk.memory);
    if (server_error != null) {
        if (error_out != null) error_out.* = server_error else c.g_free(server_error);
        return FALSE;
    }

    const options = c.json_object_get_object_member(root, "options");
    const rp = c.json_object_get_object_member(options, "rp");
    const user = c.json_object_get_object_member(options, "user");
    const challenge = c.json_object_get_string_member(options, "challenge");
    const challenge_token = c.json_object_get_string_member(root, "challenge");
    const rp_id = c.json_object_get_string_member(rp, "id");
    const rp_name = c.json_object_get_string_member(rp, "name");
    const user_id = c.json_object_get_string_member(user, "id");
    const user_name = c.json_object_get_string_member(user, "name");
    const display_name = c.json_object_get_string_member(user, "displayName");

    if (challenge == null or challenge_token == null or rp_id == null or rp_name == null or user_id == null or user_name == null or display_name == null) {
        setError(error_out, "Registration options were incomplete.");
        return FALSE;
    }
    if (base64urlDecode(user_id, &user_id_bytes) == FALSE) {
        setError(error_out, "Could not decode WebAuthn user ID.");
        return FALSE;
    }

    client_data_json = buildClientDataJson("webauthn.create", challenge);
    sha256Bytes(client_data_json, &client_hash);
    if (openFirstFidoDevice(&dev, error_out) == FALSE) return FALSE;

    cred = c.fido_cred_new();
    var rc = c.fido_cred_set_type(cred, c.COSE_ES256);
    if (rc == c.FIDO_OK) rc = c.fido_cred_set_clientdata_hash(cred, &client_hash, client_hash.len);
    if (rc == c.FIDO_OK) rc = c.fido_cred_set_rp(cred, rp_id, rp_name);
    if (rc == c.FIDO_OK) rc = c.fido_cred_set_user(cred, user_id_bytes.data, user_id_bytes.len, user_name, display_name, null);
    if (rc == c.FIDO_OK) rc = c.fido_cred_set_rk(cred, c.FIDO_OPT_OMIT);
    if (rc == c.FIDO_OK) rc = c.fido_cred_set_uv(cred, c.FIDO_OPT_OMIT);
    if (rc == c.FIDO_OK) rc = c.fido_dev_make_cred(dev, cred, null);
    if (rc != c.FIDO_OK) {
        if (error_out != null) error_out.* = c.g_strdup(c.fido_strerr(rc));
        return FALSE;
    }

    if (buildAttestationObject(cred, &attestation_object) == FALSE) {
        setError(error_out, "Could not build WebAuthn attestation object.");
        return FALSE;
    }

    credential_json = buildRegistrationCredentialJson(cred, client_data_json, &attestation_object);
    verify_payload = c.g_strdup_printf("{\"username\":\"%s\",\"challenge\":\"%s\",\"credential\":%s}", username, challenge_token, credential_json);
    if (fetch_url(constants.AUTH_PASSKEY_VERIFY_REGISTRATION_URL, &verify_chunk, verify_payload, "POST") == FALSE) {
        setError(error_out, "Could not verify passkey registration.");
        return FALSE;
    }

    server_error = extractErrorMessage(verify_chunk.memory);
    if (server_error != null) {
        if (error_out != null) error_out.* = server_error else c.g_free(server_error);
        return FALSE;
    }

    if (response_json_out != null) response_json_out.* = c.g_strdup(verify_chunk.memory);
    return TRUE;
}
