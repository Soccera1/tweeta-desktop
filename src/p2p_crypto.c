/*
 * p2p_crypto.c - P2P GPG Encryption implementation
 * 
 * Uses GPGME library for GPG operations.
 * 
 * (c)2025 Lily
 * Licensed under the AGPLv3 license
 */

#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <gpgme.h>
#include "p2p_crypto.h"
#include "globals.h"

static gpgme_ctx_t g_gpg_ctx = NULL;
static gchar *g_local_fingerprint = NULL;
static GMutex g_crypto_mutex;
static gboolean g_crypto_initialized = FALSE;

static void
init_gpgme(void)
{
    gpgme_check_version(NULL);
    gpgme_set_locale(NULL, LC_CTYPE, setlocale(LC_CTYPE, NULL));
#ifndef HAVE_W32_SYSTEM
    gpgme_set_locale(NULL, LC_MESSAGES, setlocale(LC_MESSAGES, NULL));
#endif
}

gboolean
p2p_crypto_init(void)
{
    if (g_crypto_initialized) return TRUE;

    g_mutex_init(&g_crypto_mutex);
    g_mutex_lock(&g_crypto_mutex);

    init_gpgme();

    gpgme_error_t err = gpgme_new(&g_gpg_ctx);
    if (err) {
        g_warning("Failed to create GPGME context: %s", gpgme_strerror(err));
        g_mutex_unlock(&g_crypto_mutex);
        return FALSE;
    }

    gpgme_set_armor(g_gpg_ctx, 1);
    gpgme_set_textmode(g_gpg_ctx, 1);

    g_crypto_initialized = TRUE;
    g_mutex_unlock(&g_crypto_mutex);
    return TRUE;
}

void
p2p_crypto_cleanup(void)
{
    if (!g_crypto_initialized) return;

    g_mutex_lock(&g_crypto_mutex);

    if (g_gpg_ctx) {
        gpgme_release(g_gpg_ctx);
        g_gpg_ctx = NULL;
    }

    g_free(g_local_fingerprint);
    g_local_fingerprint = NULL;

    g_mutex_unlock(&g_crypto_mutex);
    g_mutex_clear(&g_crypto_mutex);
    g_crypto_initialized = FALSE;
}

gchar *
p2p_generate_keypair(const gchar *username, const gchar *email, const gchar *passphrase)
{
    if (!g_gpg_ctx || !username || !email) {
        return NULL;
    }

    g_mutex_lock(&g_crypto_mutex);

    gpgme_genkey_result_t result = NULL;
    gchar *params = g_strdup_printf(
        "<GnupgKeyParams format=\"internal\">\n"
        "Key-Type: RSA\n"
        "Key-Length: 4096\n"
        "Subkey-Type: RSA\n"
        "Subkey-Length: 4096\n"
        "Name-Real: %s\n"
        "Name-Email: %s\n"
        "Expire-Date: 0\n"
        "Passphrase: %s\n"
        "</GnupgKeyParams>\n",
        username, email, passphrase ? passphrase : ""
    );

    gpgme_error_t err = gpgme_op_genkey(g_gpg_ctx, params, NULL, NULL);
    g_free(params);

    if (err) {
        g_warning("Failed to generate key: %s", gpgme_strerror(err));
        g_mutex_unlock(&g_crypto_mutex);
        return NULL;
    }

    result = gpgme_op_genkey_result(g_gpg_ctx);
    if (!result || !result->fpr) {
        g_warning("Key generation returned no fingerprint");
        g_mutex_unlock(&g_crypto_mutex);
        return NULL;
    }

    g_free(g_local_fingerprint);
    g_local_fingerprint = g_strdup(result->fpr);

    gchar *fingerprint = g_strdup(result->fpr);

    g_mutex_unlock(&g_crypto_mutex);
    return fingerprint;
}

gchar *
p2p_export_public_key(const gchar *fingerprint)
{
    if (!g_gpg_ctx || !fingerprint) {
        return NULL;
    }

    g_mutex_lock(&g_crypto_mutex);

    gpgme_data_t key_data = NULL;
    gpgme_error_t err = gpgme_data_new(&key_data);
    if (err) {
        g_warning("Failed to create data object: %s", gpgme_strerror(err));
        g_mutex_unlock(&g_crypto_mutex);
        return NULL;
    }

    err = gpgme_op_export(g_gpg_ctx, fingerprint, 0, key_data);
    if (err) {
        g_warning("Failed to export key: %s", gpgme_strerror(err));
        gpgme_data_release(key_data);
        g_mutex_unlock(&g_crypto_mutex);
        return NULL;
    }

    size_t len = gpgme_data_seek(key_data, 0, SEEK_END);
    gpgme_data_seek(key_data, 0, SEEK_SET);

    gchar *armored_key = g_malloc(len + 1);
    gpgme_data_read(key_data, armored_key, len);
    armored_key[len] = '\0';

    gpgme_data_release(key_data);

    g_mutex_unlock(&g_crypto_mutex);
    return armored_key;
}

gboolean
p2p_import_public_key(const gchar *armored_key, const gchar *fingerprint)
{
    (void)fingerprint;
    if (!g_gpg_ctx || !armored_key) {
        return FALSE;
    }

    g_mutex_lock(&g_crypto_mutex);

    gpgme_data_t key_data = NULL;
    gpgme_error_t err = gpgme_data_new_from_mem(&key_data, armored_key, strlen(armored_key), 0);
    if (err) {
        g_warning("Failed to create data from memory: %s", gpgme_strerror(err));
        g_mutex_unlock(&g_crypto_mutex);
        return FALSE;
    }

    err = gpgme_op_import(g_gpg_ctx, key_data);
    gpgme_data_release(key_data);

    if (err) {
        g_warning("Failed to import key: %s", gpgme_strerror(err));
        g_mutex_unlock(&g_crypto_mutex);
        return FALSE;
    }

    gpgme_import_result_t import_result = gpgme_op_import_result(g_gpg_ctx);
    if (!import_result || import_result->imported == 0) {
        g_warning("No keys were imported");
        g_mutex_unlock(&g_crypto_mutex);
        return FALSE;
    }

    g_mutex_unlock(&g_crypto_mutex);
    return TRUE;
}

gchar *
p2p_encrypt_message(const gchar *plaintext, const gchar *recipient_fingerprint)
{
    if (!g_gpg_ctx || !plaintext || !recipient_fingerprint) {
        return NULL;
    }

    g_mutex_lock(&g_crypto_mutex);

    gpgme_data_t plain_data = NULL;
    gpgme_data_t cipher_data = NULL;
    gchar *encrypted = NULL;

    gpgme_error_t err = gpgme_data_new_from_mem(&plain_data, plaintext, strlen(plaintext), 0);
    if (err) {
        g_warning("Failed to create plain data: %s", gpgme_strerror(err));
        goto cleanup;
    }

    err = gpgme_data_new(&cipher_data);
    if (err) {
        g_warning("Failed to create cipher data: %s", gpgme_strerror(err));
        goto cleanup;
    }

    gpgme_key_t recipient_key = NULL;
    err = gpgme_get_key(g_gpg_ctx, recipient_fingerprint, &recipient_key, 0);
    if (err) {
        g_warning("Failed to get recipient key: %s", gpgme_strerror(err));
        goto cleanup;
    }

    gpgme_key_t keys[] = { recipient_key, NULL };
    err = gpgme_op_encrypt(g_gpg_ctx, keys, GPGME_ENCRYPT_ALWAYS_TRUST, plain_data, cipher_data);
    gpgme_key_release(recipient_key);

    if (err) {
        g_warning("Encryption failed: %s", gpgme_strerror(err));
        goto cleanup;
    }

    size_t len = gpgme_data_seek(cipher_data, 0, SEEK_END);
    gpgme_data_seek(cipher_data, 0, SEEK_SET);

    encrypted = g_malloc(len + 1);
    gpgme_data_read(cipher_data, encrypted, len);
    encrypted[len] = '\0';

cleanup:
    if (plain_data) gpgme_data_release(plain_data);
    if (cipher_data) gpgme_data_release(cipher_data);

    g_mutex_unlock(&g_crypto_mutex);
    return encrypted;
}

gchar *
p2p_decrypt_message(const gchar *encrypted_armor, const gchar *passphrase)
{
    (void)passphrase;
    if (!g_gpg_ctx || !encrypted_armor) {
        return NULL;
    }

    g_mutex_lock(&g_crypto_mutex);

    gpgme_data_t cipher_data = NULL;
    gpgme_data_t plain_data = NULL;
    gchar *plaintext = NULL;

    gpgme_error_t err = gpgme_data_new_from_mem(&cipher_data, encrypted_armor, strlen(encrypted_armor), 0);
    if (err) {
        g_warning("Failed to create cipher data: %s", gpgme_strerror(err));
        goto cleanup;
    }

    err = gpgme_data_new(&plain_data);
    if (err) {
        g_warning("Failed to create plain data: %s", gpgme_strerror(err));
        goto cleanup;
    }

    err = gpgme_op_decrypt(g_gpg_ctx, cipher_data, plain_data);
    if (err) {
        g_warning("Decryption failed: %s", gpgme_strerror(err));
        goto cleanup;
    }

    size_t len = gpgme_data_seek(plain_data, 0, SEEK_END);
    gpgme_data_seek(plain_data, 0, SEEK_SET);

    plaintext = g_malloc(len + 1);
    gpgme_data_read(plain_data, plaintext, len);
    plaintext[len] = '\0';

cleanup:
    if (cipher_data) gpgme_data_release(cipher_data);
    if (plain_data) gpgme_data_release(plain_data);

    g_mutex_unlock(&g_crypto_mutex);
    return plaintext;
}

gchar *
p2p_sign_message(const gchar *message, const gchar *fingerprint, const gchar *passphrase)
{
    (void)passphrase;
    if (!g_gpg_ctx || !message || !fingerprint) {
        return NULL;
    }

    g_mutex_lock(&g_crypto_mutex);

    gpgme_data_t msg_data = NULL;
    gpgme_data_t sig_data = NULL;
    gchar *signature = NULL;

    gpgme_error_t err = gpgme_data_new_from_mem(&msg_data, message, strlen(message), 0);
    if (err) {
        g_warning("Failed to create message data: %s", gpgme_strerror(err));
        goto cleanup;
    }

    err = gpgme_data_new(&sig_data);
    if (err) {
        g_warning("Failed to create signature data: %s", gpgme_strerror(err));
        goto cleanup;
    }

    err = gpgme_op_sign(g_gpg_ctx, msg_data, sig_data, GPGME_SIG_MODE_DETACH);
    if (err) {
        g_warning("Signing failed: %s", gpgme_strerror(err));
        goto cleanup;
    }

    size_t len = gpgme_data_seek(sig_data, 0, SEEK_END);
    gpgme_data_seek(sig_data, 0, SEEK_SET);

    signature = g_malloc(len + 1);
    gpgme_data_read(sig_data, signature, len);
    signature[len] = '\0';

cleanup:
    if (msg_data) gpgme_data_release(msg_data);
    if (sig_data) gpgme_data_release(sig_data);

    g_mutex_unlock(&g_crypto_mutex);
    return signature;
}

gboolean
p2p_verify_signature(const gchar *message, const gchar *signature, const gchar *signer_fingerprint)
{
    (void)signer_fingerprint;
    if (!g_gpg_ctx || !message || !signature) {
        return FALSE;
    }

    g_mutex_lock(&g_crypto_mutex);

    gpgme_data_t msg_data = NULL;
    gpgme_data_t sig_data = NULL;
    gboolean valid = FALSE;

    gpgme_error_t err = gpgme_data_new_from_mem(&msg_data, message, strlen(message), 0);
    if (err) {
        goto cleanup;
    }

    err = gpgme_data_new_from_mem(&sig_data, signature, strlen(signature), 0);
    if (err) {
        goto cleanup;
    }

    err = gpgme_op_verify(g_gpg_ctx, sig_data, msg_data, NULL);
    if (err) {
        goto cleanup;
    }

    gpgme_verify_result_t verify_result = gpgme_op_verify_result(g_gpg_ctx);
    if (verify_result && verify_result->signatures) {
        gpgme_signature_t sig = verify_result->signatures;
        if (sig->status == GPG_ERR_NO_ERROR) {
            valid = TRUE;
        }
    }

cleanup:
    if (msg_data) gpgme_data_release(msg_data);
    if (sig_data) gpgme_data_release(sig_data);

    g_mutex_unlock(&g_crypto_mutex);
    return valid;
}

const gchar *
p2p_get_local_fingerprint(void)
{
    return g_local_fingerprint;
}

gboolean
p2p_crypto_is_ready(void)
{
    return (g_gpg_ctx != NULL && g_local_fingerprint != NULL);
}
