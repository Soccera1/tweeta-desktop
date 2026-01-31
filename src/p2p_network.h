/*
 * p2p_network.h - P2P Network Layer for encrypted messaging
 * 
 * Provides two transport modes:
 * 1. Direct P2P: TCP socket-based peer-to-peer communication
 * 2. Tweetapus Transport: GPG-encrypted messages sent via tweetapus API
 *    (appears as garbage data to any backdoor)
 * 
 * Key exchange must happen off-platform due to the tweetapus backdoor.
 * 
 * (c)2025 Lily
 * Licensed under the AGPLv3 license
 */

#ifndef P2P_NETWORK_H
#define P2P_NETWORK_H

#include <gtk/gtk.h>
#include "types.h"

typedef enum {
    P2P_TRANSPORT_DIRECT,       /* Direct TCP socket connection */
    P2P_TRANSPORT_TWEETAPUS     /* Via tweetapus API (encrypted garbage to backdoor) */
} P2PTransportMode;

/* P2P Network message types */
typedef enum {
    P2P_MSG_HELLO,              /* Initial handshake */
    P2P_MSG_CHAT,               /* Encrypted chat message */
    P2P_MSG_PING,               /* Keepalive */
    P2P_MSG_PONG,               /* Keepalive response */
    P2P_MSG_BYE                 /* Disconnect */
} P2PMessageType;

/* Network message structure for wire format */
struct P2PNetworkMessage {
    P2PMessageType type;
    gchar *sender_id;           /* Username or fingerprint */
    gchar *recipient_id;
    gchar *payload;             /* GPG encrypted content (for DIRECT) or base64 armored (for TWEETAPUS) */
    gchar *timestamp;
    guint64 nonce;              /* Prevent replay attacks */
};

/* P2P Transport configuration */
struct P2PTransportConfig {
    P2PTransportMode mode;
    gchar *local_username;
    gchar *local_key_fingerprint;
    gchar *listen_host;
    guint16 listen_port;
    gchar *relay_server_url;    /* For TWEETAPUS mode */
};

/* Contact network info */
struct P2PContactInfo {
    gchar *username;
    gchar *public_key_fingerprint;
    P2PTransportMode preferred_mode;
    gchar *direct_host;
    guint16 direct_port;
    gchar *last_seen;
    gboolean is_online;
    int socket_fd;              /* Active connection for DIRECT mode */
};

/* Initialize P2P network layer - returns TRUE on success */
gboolean p2p_network_init(struct P2PTransportConfig *config);

/* Cleanup P2P network layer */
void p2p_network_cleanup(void);

/* Start listening for incoming P2P connections (DIRECT mode) */
gboolean p2p_start_listener(const gchar *host, guint16 port);

/* Stop the P2P listener */
void p2p_stop_listener(void);

/* Connect to a peer (DIRECT mode) - returns socket fd or -1 on error */
int p2p_connect_to_peer(const gchar *host, guint16 port, const gchar *username);

/* Send an encrypted message - works with both transport modes */
gboolean p2p_send_message(const gchar *recipient_username, 
                          const gchar *plaintext,
                          const gchar *recipient_fingerprint);

/* Broadcast message via tweetapus encrypted transport */
gboolean p2p_broadcast_encrypted(const gchar *encrypted_payload,
                                 const gchar *recipient_username,
                                 const gchar *sender_fingerprint);

/* Poll for incoming messages via tweetapus transport */
GList *p2p_poll_tweetapus_messages(const gchar *since_timestamp);

/* Process received message (decrypt and store) */
gboolean p2p_process_received_message(struct P2PNetworkMessage *net_msg);

/* Get current transport mode */
P2PTransportMode p2p_get_transport_mode(void);

/* Set transport mode */
void p2p_set_transport_mode(P2PTransportMode mode);

/* Check if listener is running */
gboolean p2p_is_listener_running(void);

/* Get local listening address */
gchar *p2p_get_listen_address(void);

/* Free network message structure */
void p2p_free_network_message(struct P2PNetworkMessage *msg);

/* Serialize network message to wire format */
gchar *p2p_serialize_message(struct P2PNetworkMessage *msg);

/* Deserialize wire format to network message */
struct P2PNetworkMessage *p2p_deserialize_message(const gchar *data);

/* Encode encrypted payload for tweetapus transport (base64) */
gchar *p2p_encode_for_tweetapus(const gchar *encrypted_armor);

/* Decode payload from tweetapus transport */
gchar *p2p_decode_from_tweetapus(const gchar *encoded_data);

/* Start background message polling thread */
void p2p_start_message_polling(void);

/* Stop background message polling */
void p2p_stop_message_polling(void);

#endif /* P2P_NETWORK_H */
