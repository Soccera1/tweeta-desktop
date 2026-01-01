#include "challenge.h"
#include "constants.h"
#include "network.h"
#include <json-glib/json-glib.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static guint32 cap_hash(const gchar *e) {
    guint32 t = 2166136261U;
    for (int i = 0; e[i] != '\0'; i++) {
        t ^= (guint8)e[i];
        t += (t << 1) + (t << 4) + (t << 7) + (t << 8) + (t << 24);
    }
    return t;
}

static gchar* cap_prng_gen(const gchar *e, int t_len) {
    guint32 r = cap_hash(e);
    GString *i = g_string_new("");
    while (i->len < (gsize)t_len) {
        r ^= r << 13;
        r ^= r >> 17;
        r ^= r << 5;
        gchar *hex = g_strdup_printf("%08x", r);
        g_string_append(i, hex);
        g_free(hex);
    }
    gchar *res = g_strndup(i->str, t_len);
    g_string_free(i, TRUE);
    return res;
}

static guint64 solve_pow(const gchar *salt, const gchar *target_hex) {
    int target_len = strlen(target_hex) / 2;
    if (target_len > 32) target_len = 32;
    guint8 target[32];
    for (int i = 0; i < target_len; i++) {
        unsigned int val;
        sscanf(target_hex + 2 * i, "%02x", &val);
        target[i] = (guint8)val;
    }

    guint64 nonce = 0;
    while (TRUE) {
        GChecksum *checksum = g_checksum_new(G_CHECKSUM_SHA256);
        gchar *nonce_str = g_strdup_printf("%llu", (unsigned long long)nonce);
        gchar *combined = g_strconcat(salt, nonce_str, NULL);
        
        g_checksum_update(checksum, (const guchar *)combined, strlen(combined));
        
        guint8 digest[32];
        gsize digest_len = 32;
        g_checksum_get_digest(checksum, digest, &digest_len);
        
        gboolean found = TRUE;
        for (int i = 0; i < target_len; i++) {
            if (digest[i] != target[i]) {
                found = FALSE;
                break;
            }
        }
        
        g_free(combined);
        g_free(nonce_str);
        g_checksum_free(checksum);
        
        if (found) return nonce;
        nonce++;
    }
}

JsonArray* solve_challenge_internal(JsonObject *obj, const gchar *token) {
    JsonArray *solutions = json_array_new();

    if (json_object_has_member(obj, "c") && json_object_has_member(obj, "s") && json_object_has_member(obj, "d")) {
        int count = json_object_get_int_member(obj, "c");
        int salt_len = json_object_get_int_member(obj, "s");
        int difficulty = json_object_get_int_member(obj, "d");
        
        for (int i = 1; i <= count; i++) {
            gchar *seed_salt = g_strdup_printf("%s%d", token, i);
            gchar *seed_target = g_strdup_printf("%s%dd", token, i);
            
            gchar *salt = cap_prng_gen(seed_salt, salt_len);
            gchar *target = cap_prng_gen(seed_target, difficulty);
            
            guint64 nonce = solve_pow(salt, target);
            json_array_add_int_element(solutions, (gint64)nonce);
            
            g_free(salt);
            g_free(target);
            g_free(seed_salt);
            g_free(seed_target);
        }
    }
    return solutions;
}

gchar* solve_challenge(const gchar *challenge_json, const gchar *token) {
    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_data(parser, challenge_json, -1, NULL)) {
        g_object_unref(parser);
        return NULL;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_OBJECT(root)) {
        g_object_unref(parser);
        return NULL;
    }
    
    JsonArray *solutions = solve_challenge_internal(json_node_get_object(root), token);
    
    JsonGenerator *gen = json_generator_new();
    JsonNode *solutions_node = json_node_new(JSON_NODE_ARRAY);
    json_node_set_array(solutions_node, solutions);
    json_generator_set_root(gen, solutions_node);
    gchar *res = json_generator_to_data(gen, NULL);
    
    g_object_unref(gen);
    g_object_unref(parser);
    
    return res;
}

gchar* check_and_solve_challenge(const gchar *response_json) {
    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_data(parser, response_json, -1, NULL)) {
        g_object_unref(parser);
        return NULL;
    }
    
    JsonNode *root = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_OBJECT(root)) {
        g_object_unref(parser);
        return NULL;
    }
    JsonObject *obj = json_node_get_object(root);
    
    gchar *redeemed_token = NULL;

    if (json_object_has_member(obj, "challenge") && json_object_has_member(obj, "token")) {
        const gchar *token = json_object_get_string_member(obj, "token");
        JsonObject *challenge_obj = json_object_get_object_member(obj, "challenge");
        
        JsonArray *solutions = solve_challenge_internal(challenge_obj, token);
        
        // Prepare redeem request
        JsonBuilder *builder = json_builder_new();
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "token");
        json_builder_add_string_value(builder, token);
        json_builder_set_member_name(builder, "solutions");
        json_builder_add_value(builder, json_node_init_array(json_node_alloc(), solutions));
        json_builder_end_object(builder);
        
        JsonGenerator *gen = json_generator_new();
        json_generator_set_root(gen, json_builder_get_root(builder));
        gchar *post_data = json_generator_to_data(gen, NULL);
        
        struct MemoryStruct chunk;
        chunk.memory = NULL;
        chunk.size = 0;
        long response_code = 0;
        if (fetch_url_internal(CAP_REDEEM_URL, &chunk, post_data, "POST", &response_code)) {
            JsonParser *redeem_parser = json_parser_new();
            if (json_parser_load_from_data(redeem_parser, chunk.memory, -1, NULL)) {
                JsonNode *r_root = json_parser_get_root(redeem_parser);
                JsonObject *r_obj = json_node_get_object(r_root);
                if (json_object_has_member(r_obj, "success") && json_object_get_boolean_member(r_obj, "success")) {
                    redeemed_token = g_strdup(json_object_get_string_member(r_obj, "token"));
                }
            }
            g_object_unref(redeem_parser);
            free(chunk.memory);
        }
        
        g_free(post_data);
        g_object_unref(gen);
        g_object_unref(builder);
    }
    
    g_object_unref(parser);
    return redeemed_token;
}