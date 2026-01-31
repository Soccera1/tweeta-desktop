/*
 * p2p_crypto.h - P2P GPG Encryption for secure messaging
 * 
 * Provides GPG-based encryption/decryption for peer-to-peer messaging
 * that bypasses the tweetapus server backdoor.
 * 
 * (c)2025 Lily
 * Licensed under the AGPLv3 license
 */

#ifndef P2P_CRYPTO_H
#define P2P_CRYPTO_H

#include <gtk/gtk.h>
#include "types.h"

/* Initialize P2P crypto system - returns TRUE on success */
gboolean p2p_crypto_init(void);

/* Cleanup P2P crypto system */
void p2p_crypto_cleanup(void);

/* Generate a new GPG key pair for the user - returns fingerprint or NULL on error */
gchar *p2p_generate_keypair(const gchar *username, const gchar *email, const gchar *passphrase);

/* Export public key as armored ASCII - caller must free result */
gchar *p2p_export_public_key(const gchar *fingerprint);

/* Import a contact's public key - returns TRUE on success */
gboolean p2p_import_public_key(const gchar *armored_key, const gchar *fingerprint);

/* Encrypt message for a recipient - returns encrypted ASCII armor or NULL on error */
gchar *p2p_encrypt_message(const gchar *plaintext, const gchar *recipient_fingerprint);

/* Decrypt message using local private key - returns plaintext or NULL on error */
gchar *p2p_decrypt_message(const gchar *encrypted_armor, const gchar *passphrase);

/* Sign a message - returns signature or NULL on error */
gchar *p2p_sign_message(const gchar *message, const gchar *fingerprint, const gchar *passphrase);

/* Verify a signed message - returns TRUE if signature is valid */
gboolean p2p_verify_signature(const gchar *message, const gchar *signature, const gchar *signer_fingerprint);

/* Get local key fingerprint - returns fingerprint or NULL */
const gchar *p2p_get_local_fingerprint(void);

/* Check if GPG is available and working - returns TRUE if ready */
gboolean p2p_crypto_is_ready(void);

#endif /* P2P_CRYPTO_H */
