const std = @import("std");
const c = @import("c.zig").c;
const cstr = @import("cstr.zig");

const FALSE: c.gboolean = 0;
const TRUE: c.gboolean = 1;

fn lit(comptime value: [:0]const u8) [*c]const c.gchar {
    return @ptrCast(value.ptr);
}

fn logMsg(level: c.GLogLevelFlags, comptime fmt: [:0]const u8, args: anytype) void {
    @call(.auto, c.g_log, .{ @as([*c]const c.gchar, null), level, lit(fmt) } ++ args);
}

var g_gpg_ctx: c.gpgme_ctx_t = null;
var g_local_fingerprint: [*c]c.gchar = null;
var g_crypto_mutex: c.GMutex = undefined;
var g_crypto_initialized: c.gboolean = FALSE;

const GpgmeGenkeyResult = extern struct {
    flags: c_uint,
    fpr: [*c]c.gchar,
    pubkey: c.gpgme_data_t,
    seckey: c.gpgme_data_t,
};

const GpgmeImportResult = extern struct {
    considered: c_int,
    no_user_id: c_int,
    imported: c_int,
};

const GpgmeSignature = extern struct {
    next: ?*GpgmeSignature,
    summary: c_uint,
    fpr: [*c]c.gchar,
    status: c.gpgme_error_t,
};

const GpgmeVerifyResult = extern struct {
    signatures: ?*GpgmeSignature,
};

fn initGpgme() void {
    _ = c.gpgme_check_version(null);
    _ = c.gpgme_set_locale(null, c.LC_CTYPE, c.setlocale(c.LC_CTYPE, null));
    _ = c.gpgme_set_locale(null, c.LC_MESSAGES, c.setlocale(c.LC_MESSAGES, null));
}

fn dataToString(data: c.gpgme_data_t) [*c]c.gchar {
    const len = c.gpgme_data_seek(data, 0, std.posix.SEEK.END);
    _ = c.gpgme_data_seek(data, 0, std.posix.SEEK.SET);
    if (len < 0) {
        return null;
    }

    const size: usize = @intCast(len);
    const out: [*c]c.gchar = @ptrCast(c.g_malloc(size + 1));
    if (out == null) {
        return null;
    }

    _ = c.gpgme_data_read(data, out, size);
    out[size] = 0;
    return out;
}

export fn p2p_crypto_init() c.gboolean {
    if (g_crypto_initialized != FALSE) return TRUE;

    c.g_mutex_init(&g_crypto_mutex);
    c.g_mutex_lock(&g_crypto_mutex);
    initGpgme();

    const err = c.gpgme_new(&g_gpg_ctx);
    if (err != 0) {
        logMsg(c.G_LOG_LEVEL_WARNING, "Failed to create GPGME context: %s", .{c.gpgme_strerror(err)});
        c.g_mutex_unlock(&g_crypto_mutex);
        return FALSE;
    }

    c.gpgme_set_armor(g_gpg_ctx, 1);
    c.gpgme_set_textmode(g_gpg_ctx, 1);

    g_crypto_initialized = TRUE;
    c.g_mutex_unlock(&g_crypto_mutex);
    return TRUE;
}

export fn p2p_crypto_cleanup() void {
    if (g_crypto_initialized == FALSE) return;

    c.g_mutex_lock(&g_crypto_mutex);

    if (g_gpg_ctx != null) {
        c.gpgme_release(g_gpg_ctx);
        g_gpg_ctx = null;
    }

    c.g_free(g_local_fingerprint);
    g_local_fingerprint = null;

    c.g_mutex_unlock(&g_crypto_mutex);
    c.g_mutex_clear(&g_crypto_mutex);
    g_crypto_initialized = FALSE;
}

export fn p2p_generate_keypair(
    username: [*c]const c.gchar,
    email: [*c]const c.gchar,
    passphrase: [*c]const c.gchar,
) [*c]c.gchar {
    if (g_gpg_ctx == null or username == null or email == null) {
        return null;
    }

    c.g_mutex_lock(&g_crypto_mutex);

    const params = c.g_strdup_printf(
        "<GnupgKeyParams format=\"internal\">\n" ++
            "Key-Type: RSA\n" ++
            "Key-Length: 4096\n" ++
            "Subkey-Type: RSA\n" ++
            "Subkey-Length: 4096\n" ++
            "Name-Real: %s\n" ++
            "Name-Email: %s\n" ++
            "Expire-Date: 0\n" ++
            "Passphrase: %s\n" ++
            "</GnupgKeyParams>\n",
        username,
        email,
        if (passphrase != null) passphrase else lit(""),
    );
    const err = c.gpgme_op_genkey(g_gpg_ctx, params, null, null);
    c.g_free(params);
    if (err != 0) {
        logMsg(c.G_LOG_LEVEL_WARNING, "Failed to generate key: %s", .{c.gpgme_strerror(err)});
        c.g_mutex_unlock(&g_crypto_mutex);
        return null;
    }

    const result_opaque = c.gpgme_op_genkey_result(g_gpg_ctx) orelse {
        logMsg(c.G_LOG_LEVEL_WARNING, "Key generation returned no fingerprint", .{});
        c.g_mutex_unlock(&g_crypto_mutex);
        return null;
    };
    const result: *GpgmeGenkeyResult = @ptrCast(@alignCast(result_opaque));
    if (result.fpr == null) {
        logMsg(c.G_LOG_LEVEL_WARNING, "Key generation returned no fingerprint", .{});
        c.g_mutex_unlock(&g_crypto_mutex);
        return null;
    }

    c.g_free(g_local_fingerprint);
    g_local_fingerprint = c.g_strdup(result.fpr);
    const fingerprint = c.g_strdup(result.fpr);

    c.g_mutex_unlock(&g_crypto_mutex);
    return fingerprint;
}

export fn p2p_export_public_key(fingerprint: [*c]const c.gchar) [*c]c.gchar {
    if (g_gpg_ctx == null or fingerprint == null) {
        return null;
    }

    c.g_mutex_lock(&g_crypto_mutex);

    var key_data: c.gpgme_data_t = null;
    var err = c.gpgme_data_new(&key_data);
    if (err != 0) {
        logMsg(c.G_LOG_LEVEL_WARNING, "Failed to create data object: %s", .{c.gpgme_strerror(err)});
        c.g_mutex_unlock(&g_crypto_mutex);
        return null;
    }

    err = c.gpgme_op_export(g_gpg_ctx, fingerprint, 0, key_data);
    if (err != 0) {
        logMsg(c.G_LOG_LEVEL_WARNING, "Failed to export key: %s", .{c.gpgme_strerror(err)});
        c.gpgme_data_release(key_data);
        c.g_mutex_unlock(&g_crypto_mutex);
        return null;
    }

    const armored_key = dataToString(key_data);
    c.gpgme_data_release(key_data);
    c.g_mutex_unlock(&g_crypto_mutex);
    return armored_key;
}

export fn p2p_import_public_key(armored_key: [*c]const c.gchar, fingerprint: [*c]const c.gchar) c.gboolean {
    _ = fingerprint;
    if (g_gpg_ctx == null or armored_key == null) {
        return FALSE;
    }

    c.g_mutex_lock(&g_crypto_mutex);

    var key_data: c.gpgme_data_t = null;
    var err = c.gpgme_data_new_from_mem(&key_data, armored_key, cstr.len(armored_key), 0);
    if (err != 0) {
        logMsg(c.G_LOG_LEVEL_WARNING, "Failed to create data from memory: %s", .{c.gpgme_strerror(err)});
        c.g_mutex_unlock(&g_crypto_mutex);
        return FALSE;
    }

    err = c.gpgme_op_import(g_gpg_ctx, key_data);
    c.gpgme_data_release(key_data);
    if (err != 0) {
        logMsg(c.G_LOG_LEVEL_WARNING, "Failed to import key: %s", .{c.gpgme_strerror(err)});
        c.g_mutex_unlock(&g_crypto_mutex);
        return FALSE;
    }

    const import_result_opaque = c.gpgme_op_import_result(g_gpg_ctx) orelse {
        c.g_mutex_unlock(&g_crypto_mutex);
        return FALSE;
    };
    const import_result: *GpgmeImportResult = @ptrCast(@alignCast(import_result_opaque));
    if (import_result.imported == 0) {
        logMsg(c.G_LOG_LEVEL_WARNING, "No keys were imported", .{});
        c.g_mutex_unlock(&g_crypto_mutex);
        return FALSE;
    }

    c.g_mutex_unlock(&g_crypto_mutex);
    return TRUE;
}

export fn p2p_encrypt_message(
    plaintext: [*c]const c.gchar,
    recipient_fingerprint: [*c]const c.gchar,
) [*c]c.gchar {
    if (g_gpg_ctx == null or plaintext == null or recipient_fingerprint == null) {
        return null;
    }

    c.g_mutex_lock(&g_crypto_mutex);

    var plain_data: c.gpgme_data_t = null;
    var cipher_data: c.gpgme_data_t = null;
    var encrypted: [*c]c.gchar = null;

    var err = c.gpgme_data_new_from_mem(&plain_data, plaintext, cstr.len(plaintext), 0);
    if (err != 0) {
        logMsg(c.G_LOG_LEVEL_WARNING, "Failed to create plain data: %s", .{c.gpgme_strerror(err)});
    }
    if (err == 0) {
        err = c.gpgme_data_new(&cipher_data);
        if (err != 0) logMsg(c.G_LOG_LEVEL_WARNING, "Failed to create cipher data: %s", .{c.gpgme_strerror(err)});
    }
    if (err == 0) {
        var recipient_key: c.gpgme_key_t = null;
        err = c.gpgme_get_key(g_gpg_ctx, recipient_fingerprint, &recipient_key, 0);
        if (err != 0) {
            logMsg(c.G_LOG_LEVEL_WARNING, "Failed to get recipient key: %s", .{c.gpgme_strerror(err)});
        }
        if (err == 0) {
            var keys = [_]c.gpgme_key_t{ recipient_key, null };
            err = c.gpgme_op_encrypt(g_gpg_ctx, &keys, c.GPGME_ENCRYPT_ALWAYS_TRUST, plain_data, cipher_data);
            c.gpgme_key_release(recipient_key);
            if (err == 0) {
                encrypted = dataToString(cipher_data);
            } else {
                logMsg(c.G_LOG_LEVEL_WARNING, "Encryption failed: %s", .{c.gpgme_strerror(err)});
            }
        }
    }

    releaseData(plain_data);
    releaseData(cipher_data);
    c.g_mutex_unlock(&g_crypto_mutex);
    return encrypted;
}

fn releaseData(data: c.gpgme_data_t) void {
    if (data != null) c.gpgme_data_release(data);
}

export fn p2p_decrypt_message(encrypted_armor: [*c]const c.gchar, passphrase: [*c]const c.gchar) [*c]c.gchar {
    _ = passphrase;
    if (g_gpg_ctx == null or encrypted_armor == null) {
        return null;
    }

    c.g_mutex_lock(&g_crypto_mutex);
    var cipher_data: c.gpgme_data_t = null;
    var plain_data: c.gpgme_data_t = null;
    var plaintext: [*c]c.gchar = null;

    var err = c.gpgme_data_new_from_mem(&cipher_data, encrypted_armor, cstr.len(encrypted_armor), 0);
    if (err != 0) {
        logMsg(c.G_LOG_LEVEL_WARNING, "Failed to create cipher data: %s", .{c.gpgme_strerror(err)});
    }
    if (err == 0) {
        err = c.gpgme_data_new(&plain_data);
        if (err != 0) logMsg(c.G_LOG_LEVEL_WARNING, "Failed to create plain data: %s", .{c.gpgme_strerror(err)});
    }
    if (err == 0) {
        err = c.gpgme_op_decrypt(g_gpg_ctx, cipher_data, plain_data);
        if (err != 0) logMsg(c.G_LOG_LEVEL_WARNING, "Decryption failed: %s", .{c.gpgme_strerror(err)});
    }
    if (err == 0) plaintext = dataToString(plain_data);

    releaseData(cipher_data);
    releaseData(plain_data);
    c.g_mutex_unlock(&g_crypto_mutex);
    return plaintext;
}

export fn p2p_sign_message(
    message: [*c]const c.gchar,
    fingerprint: [*c]const c.gchar,
    passphrase: [*c]const c.gchar,
) [*c]c.gchar {
    _ = passphrase;
    if (g_gpg_ctx == null or message == null or fingerprint == null) {
        return null;
    }

    c.g_mutex_lock(&g_crypto_mutex);
    var msg_data: c.gpgme_data_t = null;
    var sig_data: c.gpgme_data_t = null;
    var signature: [*c]c.gchar = null;

    var err = c.gpgme_data_new_from_mem(&msg_data, message, cstr.len(message), 0);
    if (err != 0) {
        logMsg(c.G_LOG_LEVEL_WARNING, "Failed to create message data: %s", .{c.gpgme_strerror(err)});
    }
    if (err == 0) {
        err = c.gpgme_data_new(&sig_data);
        if (err != 0) logMsg(c.G_LOG_LEVEL_WARNING, "Failed to create signature data: %s", .{c.gpgme_strerror(err)});
    }
    if (err == 0) {
        err = c.gpgme_op_sign(g_gpg_ctx, msg_data, sig_data, c.GPGME_SIG_MODE_DETACH);
        if (err != 0) logMsg(c.G_LOG_LEVEL_WARNING, "Signing failed: %s", .{c.gpgme_strerror(err)});
    }
    if (err == 0) signature = dataToString(sig_data);

    releaseData(msg_data);
    releaseData(sig_data);
    c.g_mutex_unlock(&g_crypto_mutex);
    return signature;
}

export fn p2p_verify_signature(
    message: [*c]const c.gchar,
    signature: [*c]const c.gchar,
    signer_fingerprint: [*c]const c.gchar,
) c.gboolean {
    _ = signer_fingerprint;
    if (g_gpg_ctx == null or message == null or signature == null) {
        return FALSE;
    }

    c.g_mutex_lock(&g_crypto_mutex);
    var msg_data: c.gpgme_data_t = null;
    var sig_data: c.gpgme_data_t = null;
    var valid: c.gboolean = FALSE;

    var err = c.gpgme_data_new_from_mem(&msg_data, message, cstr.len(message), 0);
    if (err == 0) err = c.gpgme_data_new_from_mem(&sig_data, signature, cstr.len(signature), 0);
    if (err == 0) err = c.gpgme_op_verify(g_gpg_ctx, sig_data, msg_data, null);
    if (err == 0) {
        if (c.gpgme_op_verify_result(g_gpg_ctx)) |vr_opaque| {
            const vr: *GpgmeVerifyResult = @ptrCast(@alignCast(vr_opaque));
            if (vr.signatures != null and vr.signatures.?.status == c.GPG_ERR_NO_ERROR) {
                valid = TRUE;
            }
        }
    }

    releaseData(msg_data);
    releaseData(sig_data);
    c.g_mutex_unlock(&g_crypto_mutex);
    return valid;
}

export fn p2p_get_local_fingerprint() [*c]const c.gchar {
    return g_local_fingerprint;
}

export fn p2p_crypto_is_ready() c.gboolean {
    return if (g_gpg_ctx != null and g_local_fingerprint != null) TRUE else FALSE;
}
