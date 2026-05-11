#include "webauthn_fido2.h"

#include <string.h>
#include <json-glib/json-glib.h>
#include "constants.h"
#include "network.h"
#include "types.h"

#ifdef USE_FIDO2
#include <fido.h>
#include <openssl/sha.h>
#endif

#ifndef USE_FIDO2
gboolean
webauthn_fido2_is_enabled(void)
{
    return FALSE;
}

gboolean
webauthn_fido2_login(gchar **response_json_out, gchar **error_out)
{
    (void)response_json_out;
    if (error_out) {
        *error_out = g_strdup("Native passkey support was not enabled at build time.");
    }
    return FALSE;
}

gboolean
webauthn_fido2_register(const gchar *username, gchar **response_json_out, gchar **error_out)
{
    (void)username;
    (void)response_json_out;
    if (error_out) {
        *error_out = g_strdup("Native passkey support was not enabled at build time.");
    }
    return FALSE;
}
#else

typedef struct {
    guchar *data;
    gsize len;
} ByteBuf;

static void
byte_buf_clear(ByteBuf *buf)
{
    if (!buf) {
        return;
    }
    g_free(buf->data);
    buf->data = NULL;
    buf->len = 0;
}

static gchar *
extract_error_message(const gchar *json_data)
{
    JsonParser *parser = json_parser_new();
    gchar *message = NULL;

    if (json_data && json_parser_load_from_data(parser, json_data, -1, NULL)) {
        JsonNode *root = json_parser_get_root(parser);
        if (root && JSON_NODE_HOLDS_OBJECT(root)) {
            JsonObject *obj = json_node_get_object(root);
            if (json_object_has_member(obj, "error") &&
                !json_node_is_null(json_object_get_member(obj, "error"))) {
                message = g_strdup(json_object_get_string_member(obj, "error"));
            }
        }
    }

    g_object_unref(parser);
    return message;
}

static gchar *
base64url_encode(const guchar *data, gsize len)
{
    gchar *b64 = g_base64_encode(data, len);
    gchar *p;

    for (p = b64; *p; p++) {
        if (*p == '+') {
            *p = '-';
        } else if (*p == '/') {
            *p = '_';
        } else if (*p == '=') {
            *p = '\0';
            break;
        }
    }

    return b64;
}

static gboolean
base64url_decode(const gchar *value, ByteBuf *out)
{
    gchar *b64;
    gsize len;

    if (!value || !out) {
        return FALSE;
    }

    b64 = g_strdup(value);
    for (gchar *p = b64; *p; p++) {
        if (*p == '-') {
            *p = '+';
        } else if (*p == '_') {
            *p = '/';
        }
    }
    len = strlen(b64);
    if (len % 4) {
        gchar *padded = g_strnfill(len + (4 - (len % 4)), '=');
        memcpy(padded, b64, len);
        g_free(b64);
        b64 = padded;
    }

    out->data = g_base64_decode(b64, &out->len);
    g_free(b64);
    return out->data != NULL;
}

static const gchar *
runtime_origin(void)
{
    const gchar *env = g_getenv("TWEETA_BASE_DOMAIN");
    return (env && *env) ? env : BASE_DOMAIN;
}

static gchar *
build_client_data_json(const gchar *type, const gchar *challenge)
{
    JsonBuilder *builder = json_builder_new();
    JsonGenerator *gen = json_generator_new();
    JsonNode *root;
    gchar *json;

    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "type");
    json_builder_add_string_value(builder, type);
    json_builder_set_member_name(builder, "challenge");
    json_builder_add_string_value(builder, challenge);
    json_builder_set_member_name(builder, "origin");
    json_builder_add_string_value(builder, runtime_origin());
    json_builder_set_member_name(builder, "crossOrigin");
    json_builder_add_boolean_value(builder, FALSE);
    json_builder_end_object(builder);

    root = json_builder_get_root(builder);
    json_generator_set_root(gen, root);
    json = json_generator_to_data(gen, NULL);
    json_node_free(root);
    g_object_unref(gen);
    g_object_unref(builder);
    return json;
}

static void
sha256_bytes(const gchar *data, guchar out[SHA256_DIGEST_LENGTH])
{
    SHA256((const unsigned char *)data, strlen(data), out);
}

static JsonObject *
parse_response_object(JsonParser **parser_out, const gchar *json_data, gchar **error_out)
{
    JsonParser *parser = json_parser_new();
    GError *error = NULL;

    if (!json_parser_load_from_data(parser, json_data, -1, &error)) {
        if (error_out) {
            *error_out = g_strdup(error ? error->message : "Invalid server response.");
        }
        if (error) {
            g_error_free(error);
        }
        g_object_unref(parser);
        return NULL;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
        if (error_out) {
            *error_out = g_strdup("Invalid server response.");
        }
        g_object_unref(parser);
        return NULL;
    }

    *parser_out = parser;
    return json_node_get_object(root);
}

static gboolean
open_first_fido_device(fido_dev_t **dev_out, gchar **error_out)
{
    const size_t max_devices = 16;
    fido_dev_info_t *infos = fido_dev_info_new(max_devices);
    size_t count = 0;
    int rc;

    rc = fido_dev_info_manifest(infos, max_devices, &count);
    if (rc != FIDO_OK || count == 0) {
        if (error_out) {
            *error_out = g_strdup(count == 0 ? "No FIDO2 security key was found." : fido_strerr(rc));
        }
        fido_dev_info_free(&infos, max_devices);
        return FALSE;
    }

    for (size_t i = 0; i < count; i++) {
        const fido_dev_info_t *info = fido_dev_info_ptr(infos, i);
        const char *path = fido_dev_info_path(info);
        fido_dev_t *dev = fido_dev_new();
        rc = fido_dev_open(dev, path);
        if (rc == FIDO_OK) {
            *dev_out = dev;
            fido_dev_info_free(&infos, max_devices);
            return TRUE;
        }
        fido_dev_free(&dev);
    }

    if (error_out) {
        *error_out = g_strdup("Could not open a FIDO2 security key.");
    }
    fido_dev_info_free(&infos, max_devices);
    return FALSE;
}

static void
cbor_append_type_len(GByteArray *out, guint8 major, gsize len)
{
    if (len < 24) {
        guint8 b = (guint8)((major << 5) | len);
        g_byte_array_append(out, &b, 1);
    } else if (len <= 0xff) {
        guint8 b[2] = { (guint8)((major << 5) | 24), (guint8)len };
        g_byte_array_append(out, b, sizeof(b));
    } else if (len <= 0xffff) {
        guint8 b[3] = { (guint8)((major << 5) | 25), (guint8)(len >> 8), (guint8)len };
        g_byte_array_append(out, b, sizeof(b));
    } else {
        guint8 b[5] = {
            (guint8)((major << 5) | 26),
            (guint8)(len >> 24), (guint8)(len >> 16), (guint8)(len >> 8), (guint8)len
        };
        g_byte_array_append(out, b, sizeof(b));
    }
}

static void
cbor_append_text(GByteArray *out, const gchar *text)
{
    gsize len = text ? strlen(text) : 0;
    cbor_append_type_len(out, 3, len);
    if (len) {
        g_byte_array_append(out, (const guint8 *)text, len);
    }
}

static void
cbor_append_bytes(GByteArray *out, const guchar *bytes, gsize len)
{
    cbor_append_type_len(out, 2, len);
    if (len) {
        g_byte_array_append(out, bytes, len);
    }
}

static gboolean
build_attestation_object(fido_cred_t *cred, ByteBuf *out)
{
    const gchar *fmt = fido_cred_fmt(cred);
    const guchar *authdata = fido_cred_authdata_raw_ptr(cred);
    gsize authdata_len = fido_cred_authdata_raw_len(cred);
    const guchar *attstmt = fido_cred_attstmt_ptr(cred);
    gsize attstmt_len = fido_cred_attstmt_len(cred);
    GByteArray *bytes;
    guint8 map3 = 0xa3;

    if (!fmt || !authdata || authdata_len == 0 || !attstmt || attstmt_len == 0) {
        return FALSE;
    }

    bytes = g_byte_array_new();
    g_byte_array_append(bytes, &map3, 1);
    cbor_append_text(bytes, "fmt");
    cbor_append_text(bytes, fmt);
    cbor_append_text(bytes, "authData");
    cbor_append_bytes(bytes, authdata, authdata_len);
    cbor_append_text(bytes, "attStmt");
    g_byte_array_append(bytes, attstmt, attstmt_len);

    out->len = bytes->len;
    out->data = g_byte_array_free(bytes, FALSE);
    return TRUE;
}

static void
json_builder_add_b64_member(JsonBuilder *builder, const gchar *name, const guchar *data, gsize len)
{
    gchar *encoded = base64url_encode(data, len);
    json_builder_set_member_name(builder, name);
    json_builder_add_string_value(builder, encoded);
    g_free(encoded);
}

static gchar *
build_authentication_credential_json(fido_assert_t *assertion, const gchar *client_data_json)
{
    JsonBuilder *builder = json_builder_new();
    JsonGenerator *gen = json_generator_new();
    JsonNode *root;
    gchar *json;
    const guchar *raw_id = fido_assert_id_ptr(assertion, 0);
    gsize raw_id_len = fido_assert_id_len(assertion, 0);
    const guchar *user_id = fido_assert_user_id_ptr(assertion, 0);
    gsize user_id_len = fido_assert_user_id_len(assertion, 0);

    json_builder_begin_object(builder);
    json_builder_add_b64_member(builder, "id", raw_id, raw_id_len);
    json_builder_add_b64_member(builder, "rawId", raw_id, raw_id_len);
    json_builder_set_member_name(builder, "type");
    json_builder_add_string_value(builder, "public-key");
    json_builder_set_member_name(builder, "response");
    json_builder_begin_object(builder);
    json_builder_add_b64_member(builder, "clientDataJSON", (const guchar *)client_data_json, strlen(client_data_json));
    json_builder_add_b64_member(builder, "authenticatorData", fido_assert_authdata_raw_ptr(assertion, 0), fido_assert_authdata_raw_len(assertion, 0));
    json_builder_add_b64_member(builder, "signature", fido_assert_sig_ptr(assertion, 0), fido_assert_sig_len(assertion, 0));
    json_builder_set_member_name(builder, "userHandle");
    if (user_id && user_id_len > 0) {
        gchar *encoded = base64url_encode(user_id, user_id_len);
        json_builder_add_string_value(builder, encoded);
        g_free(encoded);
    } else {
        json_builder_add_null_value(builder);
    }
    json_builder_end_object(builder);
    json_builder_set_member_name(builder, "clientExtensionResults");
    json_builder_begin_object(builder);
    json_builder_end_object(builder);
    json_builder_end_object(builder);

    root = json_builder_get_root(builder);
    json_generator_set_root(gen, root);
    json = json_generator_to_data(gen, NULL);
    json_node_free(root);
    g_object_unref(gen);
    g_object_unref(builder);
    return json;
}

static gchar *
build_registration_credential_json(fido_cred_t *cred, const gchar *client_data_json, ByteBuf *attestation_object)
{
    JsonBuilder *builder = json_builder_new();
    JsonGenerator *gen = json_generator_new();
    JsonNode *root;
    gchar *json;
    const guchar *raw_id = fido_cred_id_ptr(cred);
    gsize raw_id_len = fido_cred_id_len(cred);

    json_builder_begin_object(builder);
    json_builder_add_b64_member(builder, "id", raw_id, raw_id_len);
    json_builder_add_b64_member(builder, "rawId", raw_id, raw_id_len);
    json_builder_set_member_name(builder, "type");
    json_builder_add_string_value(builder, "public-key");
    json_builder_set_member_name(builder, "response");
    json_builder_begin_object(builder);
    json_builder_add_b64_member(builder, "clientDataJSON", (const guchar *)client_data_json, strlen(client_data_json));
    json_builder_add_b64_member(builder, "attestationObject", attestation_object->data, attestation_object->len);
    json_builder_set_member_name(builder, "transports");
    json_builder_begin_array(builder);
    json_builder_add_string_value(builder, "usb");
    json_builder_end_array(builder);
    json_builder_end_object(builder);
    json_builder_set_member_name(builder, "clientExtensionResults");
    json_builder_begin_object(builder);
    json_builder_end_object(builder);
    json_builder_end_object(builder);

    root = json_builder_get_root(builder);
    json_generator_set_root(gen, root);
    json = json_generator_to_data(gen, NULL);
    json_node_free(root);
    g_object_unref(gen);
    g_object_unref(builder);
    return json;
}

gboolean
webauthn_fido2_is_enabled(void)
{
    return TRUE;
}

gboolean
webauthn_fido2_login(gchar **response_json_out, gchar **error_out)
{
    struct MemoryStruct options_chunk = {0};
    struct MemoryStruct verify_chunk = {0};
    JsonParser *parser = NULL;
    JsonObject *root;
    JsonObject *options;
    const gchar *challenge;
    const gchar *rp_id;
    const gchar *expected_challenge;
    gchar *client_data_json = NULL;
    gchar *credential_json = NULL;
    gchar *payload = NULL;
    guchar client_hash[SHA256_DIGEST_LENGTH];
    fido_dev_t *dev = NULL;
    fido_assert_t *assertion = NULL;
    gboolean success = FALSE;
    int rc;

    fido_init(0);
    if (!fetch_url(AUTH_PASSKEY_GENERATE_AUTHENTICATION_URL, &options_chunk, "{}", "POST")) {
        if (error_out) {
            *error_out = g_strdup("Could not start passkey authentication.");
        }
        goto done;
    }

    root = parse_response_object(&parser, options_chunk.memory, error_out);
    if (!root) {
        goto done;
    }
    gchar *server_error = extract_error_message(options_chunk.memory);
    if (server_error) {
        if (error_out) {
            *error_out = server_error;
        } else {
            g_free(server_error);
        }
        goto done;
    }

    options = json_object_get_object_member(root, "options");
    expected_challenge = json_object_get_string_member(root, "expectedChallenge");
    challenge = json_object_get_string_member(options, "challenge");
    rp_id = json_object_get_string_member(options, "rpId");
    if (!challenge || !rp_id || !expected_challenge) {
        if (error_out) {
            *error_out = g_strdup("Authentication options were incomplete.");
        }
        goto done;
    }

    client_data_json = build_client_data_json("webauthn.get", challenge);
    sha256_bytes(client_data_json, client_hash);
    if (!open_first_fido_device(&dev, error_out)) {
        goto done;
    }

    assertion = fido_assert_new();
    if ((rc = fido_assert_set_clientdata_hash(assertion, client_hash, sizeof(client_hash))) != FIDO_OK ||
        (rc = fido_assert_set_rp(assertion, rp_id)) != FIDO_OK ||
        (rc = fido_assert_set_up(assertion, FIDO_OPT_TRUE)) != FIDO_OK ||
        (rc = fido_dev_get_assert(dev, assertion, NULL)) != FIDO_OK) {
        if (error_out) {
            *error_out = g_strdup(fido_strerr(rc));
        }
        goto done;
    }

    credential_json = build_authentication_credential_json(assertion, client_data_json);
    payload = g_strdup_printf("{\"expectedChallenge\":\"%s\",\"credential\":%s}",
                              expected_challenge,
                              credential_json);
    if (!fetch_url(AUTH_PASSKEY_VERIFY_AUTHENTICATION_URL, &verify_chunk, payload, "POST")) {
        if (error_out) {
            *error_out = g_strdup("Could not verify passkey authentication.");
        }
        goto done;
    }

    server_error = extract_error_message(verify_chunk.memory);
    if (server_error) {
        if (error_out) {
            *error_out = server_error;
        } else {
            g_free(server_error);
        }
        goto done;
    }

    if (response_json_out) {
        *response_json_out = g_strdup(verify_chunk.memory);
    }
    success = TRUE;

done:
    if (assertion) {
        fido_assert_free(&assertion);
    }
    if (dev) {
        fido_dev_close(dev);
        fido_dev_free(&dev);
    }
    if (parser) {
        g_object_unref(parser);
    }
    g_free(client_data_json);
    g_free(credential_json);
    g_free(payload);
    g_free(options_chunk.memory);
    g_free(verify_chunk.memory);
    return success;
}

gboolean
webauthn_fido2_register(const gchar *username, gchar **response_json_out, gchar **error_out)
{
    struct MemoryStruct options_chunk = {0};
    struct MemoryStruct verify_chunk = {0};
    JsonParser *parser = NULL;
    JsonObject *root;
    JsonObject *options;
    JsonObject *rp;
    JsonObject *user;
    const gchar *challenge;
    const gchar *challenge_token;
    const gchar *rp_id;
    const gchar *rp_name;
    const gchar *user_id;
    const gchar *user_name;
    const gchar *display_name;
    gchar *request_payload = NULL;
    gchar *client_data_json = NULL;
    gchar *credential_json = NULL;
    gchar *verify_payload = NULL;
    ByteBuf user_id_bytes = {0};
    ByteBuf attestation_object = {0};
    guchar client_hash[SHA256_DIGEST_LENGTH];
    fido_dev_t *dev = NULL;
    fido_cred_t *cred = NULL;
    gboolean success = FALSE;
    int rc;

    if (!username || !*username) {
        if (error_out) {
            *error_out = g_strdup("Username is required.");
        }
        return FALSE;
    }

    fido_init(0);
    request_payload = g_strdup_printf("{\"username\":\"%s\"}", username);
    if (!fetch_url(AUTH_PASSKEY_GENERATE_REGISTRATION_URL, &options_chunk, request_payload, "POST")) {
        if (error_out) {
            *error_out = g_strdup("Could not start passkey registration.");
        }
        goto done;
    }

    root = parse_response_object(&parser, options_chunk.memory, error_out);
    if (!root) {
        goto done;
    }
    gchar *server_error = extract_error_message(options_chunk.memory);
    if (server_error) {
        if (error_out) {
            *error_out = server_error;
        } else {
            g_free(server_error);
        }
        goto done;
    }

    options = json_object_get_object_member(root, "options");
    rp = json_object_get_object_member(options, "rp");
    user = json_object_get_object_member(options, "user");
    challenge = json_object_get_string_member(options, "challenge");
    challenge_token = json_object_get_string_member(root, "challenge");
    rp_id = json_object_get_string_member(rp, "id");
    rp_name = json_object_get_string_member(rp, "name");
    user_id = json_object_get_string_member(user, "id");
    user_name = json_object_get_string_member(user, "name");
    display_name = json_object_get_string_member(user, "displayName");

    if (!challenge || !challenge_token || !rp_id || !rp_name || !user_id || !user_name || !display_name) {
        if (error_out) {
            *error_out = g_strdup("Registration options were incomplete.");
        }
        goto done;
    }
    if (!base64url_decode(user_id, &user_id_bytes)) {
        if (error_out) {
            *error_out = g_strdup("Could not decode WebAuthn user ID.");
        }
        goto done;
    }

    client_data_json = build_client_data_json("webauthn.create", challenge);
    sha256_bytes(client_data_json, client_hash);
    if (!open_first_fido_device(&dev, error_out)) {
        goto done;
    }

    cred = fido_cred_new();
    if ((rc = fido_cred_set_type(cred, COSE_ES256)) != FIDO_OK ||
        (rc = fido_cred_set_clientdata_hash(cred, client_hash, sizeof(client_hash))) != FIDO_OK ||
        (rc = fido_cred_set_rp(cred, rp_id, rp_name)) != FIDO_OK ||
        (rc = fido_cred_set_user(cred, user_id_bytes.data, user_id_bytes.len, user_name, display_name, NULL)) != FIDO_OK ||
        (rc = fido_cred_set_rk(cred, FIDO_OPT_OMIT)) != FIDO_OK ||
        (rc = fido_cred_set_uv(cred, FIDO_OPT_OMIT)) != FIDO_OK ||
        (rc = fido_dev_make_cred(dev, cred, NULL)) != FIDO_OK) {
        if (error_out) {
            *error_out = g_strdup(fido_strerr(rc));
        }
        goto done;
    }

    if (!build_attestation_object(cred, &attestation_object)) {
        if (error_out) {
            *error_out = g_strdup("Could not build WebAuthn attestation object.");
        }
        goto done;
    }

    credential_json = build_registration_credential_json(cred, client_data_json, &attestation_object);
    verify_payload = g_strdup_printf("{\"username\":\"%s\",\"challenge\":\"%s\",\"credential\":%s}",
                                     username,
                                     challenge_token,
                                     credential_json);
    if (!fetch_url(AUTH_PASSKEY_VERIFY_REGISTRATION_URL, &verify_chunk, verify_payload, "POST")) {
        if (error_out) {
            *error_out = g_strdup("Could not verify passkey registration.");
        }
        goto done;
    }

    server_error = extract_error_message(verify_chunk.memory);
    if (server_error) {
        if (error_out) {
            *error_out = server_error;
        } else {
            g_free(server_error);
        }
        goto done;
    }

    if (response_json_out) {
        *response_json_out = g_strdup(verify_chunk.memory);
    }
    success = TRUE;

done:
    if (cred) {
        fido_cred_free(&cred);
    }
    if (dev) {
        fido_dev_close(dev);
        fido_dev_free(&dev);
    }
    if (parser) {
        g_object_unref(parser);
    }
    byte_buf_clear(&user_id_bytes);
    byte_buf_clear(&attestation_object);
    g_free(request_payload);
    g_free(client_data_json);
    g_free(credential_json);
    g_free(verify_payload);
    g_free(options_chunk.memory);
    g_free(verify_chunk.memory);
    return success;
}

#endif
