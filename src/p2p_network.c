/*
 * p2p_network.c - P2P Network Layer Implementation
 * 
 * (c)2025 Lily
 * Licensed under the AGPLv3 license
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include <curl/curl.h>
#include "p2p_network.h"
#include "p2p_crypto.h"
#include "globals.h"
#include "network.h"
#include "constants.h"
#include "actions.h"

/* Forward declaration for UI refresh callback */
extern void p2p_refresh_messages_list(const gchar *contact_username);

static struct P2PTransportConfig g_transport_config = {
    .mode = P2P_TRANSPORT_TWEETAPUS  /* Default to relay mode for ease of use */
};
static GHashTable *g_active_connections = NULL;  /* username -> P2PContactInfo* */
static GHashTable *g_pending_messages = NULL;    /* nonce -> timestamp */
static int g_listen_socket = -1;
static guint16 g_listen_port = 0;
static gboolean g_listener_running = FALSE;
static gboolean g_polling_active = FALSE;
static GThread *g_listener_thread = NULL;
static GThread *g_poll_thread = NULL;
static GMutex g_network_mutex;
static GMutex g_connections_mutex;
static gboolean g_network_initialized = FALSE;

static void
p2p_free_contact_info(gpointer data)
{
    struct P2PContactInfo *info = (struct P2PContactInfo *)data;
    if (info) {
        g_free(info->username);
        g_free(info->public_key_fingerprint);
        g_free(info->direct_host);
        g_free(info->last_seen);
        if (info->socket_fd >= 0) {
            close(info->socket_fd);
        }
        g_free(info);
    }
}

/* Wrapper for UI refresh callback to match GSourceFunc signature */
static gboolean refresh_messages_idle_cb(gpointer user_data)
{
    gchar *username = (gchar *)user_data;
    if (username && g_p2p_session) {
        p2p_refresh_messages_list(username);
    }
    g_free(user_data);
    return G_SOURCE_REMOVE;
}

/* Set socket to non-blocking mode */
static int
set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* Generate nonce for replay protection */
static guint64
generate_nonce(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((guint64)ts.tv_sec << 32) | (guint64)(ts.tv_nsec & 0xFFFFFFFF);
}

/* Serialize message to JSON for wire format */
gchar *
p2p_serialize_message(struct P2PNetworkMessage *msg)
{
    if (!msg) return NULL;

    JsonObject *obj = json_object_new();
    json_object_set_int_member(obj, "type", msg->type);
    json_object_set_string_member(obj, "sender_id", msg->sender_id ? msg->sender_id : "");
    json_object_set_string_member(obj, "recipient_id", msg->recipient_id ? msg->recipient_id : "");
    json_object_set_string_member(obj, "payload", msg->payload ? msg->payload : "");
    json_object_set_string_member(obj, "timestamp", msg->timestamp ? msg->timestamp : "");
    json_object_set_int_member(obj, "nonce", (gint64)msg->nonce);

    JsonNode *root = json_node_new(JSON_NODE_OBJECT);
    json_node_set_object(root, obj);
    gchar *json_str = json_to_string(root, FALSE);

    json_node_free(root);
    json_object_unref(obj);

    return json_str;
}

/* Deserialize JSON to message structure */
struct P2PNetworkMessage *
p2p_deserialize_message(const gchar *data)
{
    if (!data) return NULL;

    JsonParser *parser = json_parser_new();
    GError *error = NULL;

    if (!json_parser_load_from_data(parser, data, -1, &error)) {
        g_warning("Failed to parse P2P message: %s", error->message);
        g_error_free(error);
        g_object_unref(parser);
        return NULL;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (!root || JSON_NODE_TYPE(root) != JSON_NODE_OBJECT) {
        g_object_unref(parser);
        return NULL;
    }

    JsonObject *obj = json_node_get_object(root);
    struct P2PNetworkMessage *msg = g_new0(struct P2PNetworkMessage, 1);

    msg->type = (P2PMessageType)json_object_get_int_member(obj, "type");
    msg->sender_id = g_strdup(json_object_get_string_member(obj, "sender_id"));
    msg->recipient_id = g_strdup(json_object_get_string_member(obj, "recipient_id"));
    msg->payload = g_strdup(json_object_get_string_member(obj, "payload"));
    msg->timestamp = g_strdup(json_object_get_string_member(obj, "timestamp"));
    msg->nonce = (guint64)json_object_get_int_member(obj, "nonce");

    g_object_unref(parser);
    return msg;
}

/* Encode encrypted payload for tweetapus transport */
gchar *
p2p_encode_for_tweetapus(const gchar *encrypted_armor)
{
    if (!encrypted_armor) return NULL;

    /* Base64 encode the GPG armored message to make it safe for JSON transport */
    gchar *encoded = g_base64_encode((const guchar *)encrypted_armor, strlen(encrypted_armor));

    return encoded;
}

/* Decode payload from tweetapus transport */
gchar *
p2p_decode_from_tweetapus(const gchar *encoded_data)
{
    if (!encoded_data) return NULL;

    gsize len;
    guchar *decoded = g_base64_decode(encoded_data, &len);
    if (!decoded) return NULL;

    gchar *result = g_strndup((gchar *)decoded, len);
    g_free(decoded);
    return result;
}

/* Free network message */
void
p2p_free_network_message(struct P2PNetworkMessage *msg)
{
    if (!msg) return;
    g_free(msg->sender_id);
    g_free(msg->recipient_id);
    g_free(msg->payload);
    g_free(msg->timestamp);
    g_free(msg);
}

/* Initialize P2P network layer */
gboolean
p2p_network_init(struct P2PTransportConfig *config)
{
    if (g_network_initialized) return TRUE;

    g_mutex_init(&g_network_mutex);
    g_mutex_init(&g_connections_mutex);

    if (config) {
        memcpy(&g_transport_config, config, sizeof(struct P2PTransportConfig));
        if (config->local_username) {
            g_transport_config.local_username = g_strdup(config->local_username);
        }
        if (config->local_key_fingerprint) {
            g_transport_config.local_key_fingerprint = g_strdup(config->local_key_fingerprint);
        }
        if (config->listen_host) {
            g_transport_config.listen_host = g_strdup(config->listen_host);
        }
        if (config->relay_server_url) {
            g_transport_config.relay_server_url = g_strdup(config->relay_server_url);
        }
    }

    g_active_connections = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, p2p_free_contact_info);
    g_pending_messages = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

    g_network_initialized = TRUE;
    return TRUE;
}

/* Cleanup P2P network layer */
void
p2p_network_cleanup(void)
{
    if (!g_network_initialized) return;

    p2p_stop_listener();
    p2p_stop_message_polling();

    g_mutex_lock(&g_connections_mutex);
    if (g_active_connections) {
        g_hash_table_destroy(g_active_connections);
        g_active_connections = NULL;
    }
    g_mutex_unlock(&g_connections_mutex);

    if (g_pending_messages) {
        g_hash_table_destroy(g_pending_messages);
        g_pending_messages = NULL;
    }

    g_free(g_transport_config.local_username);
    g_free(g_transport_config.local_key_fingerprint);
    g_free(g_transport_config.listen_host);
    g_free(g_transport_config.relay_server_url);
    memset(&g_transport_config, 0, sizeof(g_transport_config));

    g_mutex_clear(&g_network_mutex);
    g_mutex_clear(&g_connections_mutex);
    g_network_initialized = FALSE;
}

/* Listener thread function for direct P2P connections */
static gpointer
listener_thread_func(gpointer data)
{
    (void)data;

    fd_set read_fds;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    while (g_listener_running && g_listen_socket >= 0) {
        FD_ZERO(&read_fds);
        FD_SET(g_listen_socket, &read_fds);

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int ret = select(g_listen_socket + 1, &read_fds, NULL, NULL, &tv);
        if (ret < 0) {
            if (errno != EINTR) {
                g_warning("select() error in P2P listener: %s", strerror(errno));
            }
            continue;
        }

        if (ret == 0) continue;  /* Timeout */

        if (FD_ISSET(g_listen_socket, &read_fds)) {
            int client_fd = accept(g_listen_socket, (struct sockaddr *)&client_addr, &client_len);
            if (client_fd < 0) {
                g_warning("accept() failed: %s", strerror(errno));
                continue;
            }

            set_nonblocking(client_fd);

            gchar *client_ip = g_strdup(inet_ntoa(client_addr.sin_addr));
            g_debug("P2P: Incoming connection from %s:%d", client_ip, ntohs(client_addr.sin_port));
            g_free(client_ip);

            /* TODO: Handle client connection - start reader thread */
            /* For now, just close it (will implement full handshake later) */
            close(client_fd);
        }
    }

    return NULL;
}

/* Start P2P listener */
gboolean
p2p_start_listener(const gchar *host, guint16 port)
{
    if (g_listen_socket >= 0) {
        g_warning("P2P listener already running");
        return FALSE;
    }

    g_listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen_socket < 0) {
        g_warning("Failed to create P2P socket: %s", strerror(errno));
        return FALSE;
    }

    int opt = 1;
    if (setsockopt(g_listen_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        g_warning("setsockopt() failed: %s", strerror(errno));
        close(g_listen_socket);
        g_listen_socket = -1;
        return FALSE;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (host) {
        inet_pton(AF_INET, host, &addr.sin_addr);
    } else {
        addr.sin_addr.s_addr = INADDR_ANY;
    }

    if (bind(g_listen_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        g_warning("bind() failed: %s", strerror(errno));
        close(g_listen_socket);
        g_listen_socket = -1;
        return FALSE;
    }

    if (listen(g_listen_socket, 10) < 0) {
        g_warning("listen() failed: %s", strerror(errno));
        close(g_listen_socket);
        g_listen_socket = -1;
        return FALSE;
    }

    /* Get the actual port assigned */
    socklen_t len = sizeof(addr);
    if (getsockname(g_listen_socket, (struct sockaddr *)&addr, &len) == 0) {
        g_listen_port = ntohs(addr.sin_port);
    } else {
        g_listen_port = port;
    }

    set_nonblocking(g_listen_socket);

    g_listener_running = TRUE;
    g_listener_thread = g_thread_new("p2p-listener", listener_thread_func, NULL);

    g_debug("P2P listener started on %s:%d", host ? host : "0.0.0.0", g_listen_port);
    return TRUE;
}

/* Stop P2P listener */
void
p2p_stop_listener(void)
{
    g_listener_running = FALSE;

    if (g_listener_thread) {
        g_thread_join(g_listener_thread);
        g_listener_thread = NULL;
    }

    if (g_listen_socket >= 0) {
        close(g_listen_socket);
        g_listen_socket = -1;
    }

    g_listen_port = 0;
}

/* Connect to peer (DIRECT mode) */
int
p2p_connect_to_peer(const gchar *host, guint16 port, const gchar *username)
{
    if (!host || !username) return -1;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        g_warning("Failed to create socket: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        g_warning("Invalid address: %s", host);
        close(sock);
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        g_warning("connect() failed to %s:%d: %s", host, port, strerror(errno));
        close(sock);
        return -1;
    }

    set_nonblocking(sock);

    /* Store connection info */
    struct P2PContactInfo *info = g_new0(struct P2PContactInfo, 1);
    info->username = g_strdup(username);
    info->direct_host = g_strdup(host);
    info->direct_port = port;
    info->socket_fd = sock;
    info->is_online = TRUE;
    info->preferred_mode = P2P_TRANSPORT_DIRECT;

    g_mutex_lock(&g_connections_mutex);
    g_hash_table_insert(g_active_connections, g_strdup(username), info);
    g_mutex_unlock(&g_connections_mutex);

    /* Send HELLO message */
    struct P2PNetworkMessage hello = {0};
    hello.type = P2P_MSG_HELLO;
    hello.sender_id = g_strdup(g_transport_config.local_username);
    hello.recipient_id = g_strdup(username);
    hello.timestamp = g_strdup("");
    hello.nonce = generate_nonce();

    gchar *hello_json = p2p_serialize_message(&hello);
    send(sock, hello_json, strlen(hello_json), 0);
    send(sock, "\n", 1, 0);  /* Line terminator */

    g_free(hello_json);
    g_free(hello.sender_id);
    g_free(hello.recipient_id);
    g_free(hello.timestamp);

    g_debug("P2P: Connected to %s at %s:%d", username, host, port);
    return sock;
}

/* Send message via DIRECT mode */
static gboolean
send_direct_message(const gchar *recipient, struct P2PNetworkMessage *msg)
{
    g_mutex_lock(&g_connections_mutex);
    struct P2PContactInfo *info = g_hash_table_lookup(g_active_connections, recipient);
    g_mutex_unlock(&g_connections_mutex);

    if (!info || info->socket_fd < 0) {
        g_warning("No active connection to %s", recipient);
        return FALSE;
    }

    gchar *json = p2p_serialize_message(msg);
    if (!json) return FALSE;

    ssize_t sent = send(info->socket_fd, json, strlen(json), 0);
    send(info->socket_fd, "\n", 1, 0);

    g_free(json);

    return (sent > 0);
}

/* Send message via TWEETAPUS mode (encrypted garbage to backdoor) */
static gboolean
send_tweetapus_message(const gchar *recipient, struct P2PNetworkMessage *msg)
{
    /* Serialize and encode the message */
    gchar *json = p2p_serialize_message(msg);
    if (!json) return FALSE;

    /* Double-encrypt: first with recipient's key (already done), 
     * then encode for transport */
    gchar *encoded_payload = p2p_encode_for_tweetapus(msg->payload);
    g_free(json);

    if (!encoded_payload) return FALSE;

    /* Construct API URL for tweetapus encrypted transport */
    /* We'll use a special endpoint that accepts "garbage" encrypted data */
    gchar *url = g_strdup_printf("%s/api/v1/p2p/encrypted", 
        g_transport_config.relay_server_url ? g_transport_config.relay_server_url : API_BASE_URL);

    /* Create JSON payload for the API */
    JsonObject *obj = json_object_new();
    json_object_set_string_member(obj, "recipient", recipient);
    json_object_set_string_member(obj, "sender_fingerprint", g_transport_config.local_key_fingerprint);
    json_object_set_string_member(obj, "encrypted_data", encoded_payload);
    json_object_set_int_member(obj, "nonce", (gint64)msg->nonce);

    JsonNode *root = json_node_new(JSON_NODE_OBJECT);
    json_node_set_object(root, obj);
    gchar *post_data = json_to_string(root, FALSE);

    json_node_free(root);
    json_object_unref(obj);
    g_free(encoded_payload);

    /* Send via HTTP POST */
    struct MemoryStruct chunk = {0};
    gboolean success = fetch_url(url, &chunk, post_data, "POST");

    g_free(url);
    g_free(post_data);
    if (chunk.memory) g_free(chunk.memory);

    return success;
}

/* Main send message function */
gboolean
p2p_send_message(const gchar *recipient_username, 
                 const gchar *plaintext,
                 const gchar *recipient_fingerprint)
{
    if (!recipient_username || !plaintext || !recipient_fingerprint) {
        g_warning("Invalid parameters for p2p_send_message");
        return FALSE;
    }

    /* Encrypt the message */
    gchar *encrypted = p2p_encrypt_message(plaintext, recipient_fingerprint);
    if (!encrypted) {
        g_warning("Failed to encrypt message for %s", recipient_username);
        return FALSE;
    }

    /* Create network message */
    struct P2PNetworkMessage msg = {0};
    msg.type = P2P_MSG_CHAT;
    msg.sender_id = g_strdup(g_transport_config.local_username);
    msg.recipient_id = g_strdup(recipient_username);
    msg.payload = encrypted;  /* Already encrypted with recipient's key */
    msg.timestamp = g_strdup("");
    msg.nonce = generate_nonce();

    gboolean sent = FALSE;

    /* Determine transport mode */
    g_mutex_lock(&g_connections_mutex);
    struct P2PContactInfo *info = g_hash_table_lookup(g_active_connections, recipient_username);
    P2PTransportMode mode = info ? info->preferred_mode : g_transport_config.mode;
    g_mutex_unlock(&g_connections_mutex);

    if (mode == P2P_TRANSPORT_DIRECT && info && info->socket_fd >= 0) {
        sent = send_direct_message(recipient_username, &msg);
    } else {
        /* Fall back to tweetapus transport or use it if preferred */
        sent = send_tweetapus_message(recipient_username, &msg);
    }

    /* Store locally regardless of send success */
    if (g_p2p_session) {
        struct P2PMessage *local_msg = g_new0(struct P2PMessage, 1);
        local_msg->id = g_strdup_printf("p2p_%lu", (unsigned long)msg.nonce);
        local_msg->sender_username = g_strdup(g_transport_config.local_username);
        local_msg->recipient_username = g_strdup(recipient_username);
        local_msg->plaintext_content = g_strdup(plaintext);
        local_msg->encrypted_content = g_strdup(encrypted);
        local_msg->timestamp = g_strdup("");
        local_msg->is_outgoing = TRUE;
        local_msg->is_verified = TRUE;

        g_mutex_lock(&g_p2p_session->session_mutex);
        GList *conversation = g_hash_table_lookup(g_p2p_session->conversations, recipient_username);
        conversation = g_list_append(conversation, local_msg);
        g_hash_table_insert(g_p2p_session->conversations, g_strdup(recipient_username), conversation);
        g_mutex_unlock(&g_p2p_session->session_mutex);
    }

    g_free(msg.sender_id);
    g_free(msg.recipient_id);
    g_free(msg.timestamp);
    /* encrypted is msg.payload - don't double free */

    return sent;
}

/* Poll thread for tweetapus transport */
static gpointer
poll_thread_func(gpointer data)
{
    (void)data;

    gchar *last_poll_time = g_strdup("1970-01-01T00:00:00Z");

    while (g_polling_active) {
        /* Only poll if using tweetapus transport */
        if (g_transport_config.mode == P2P_TRANSPORT_TWEETAPUS || 
            g_transport_config.relay_server_url != NULL) {
            
            GList *messages = p2p_poll_tweetapus_messages(last_poll_time);
            
            /* Process received messages */
            for (GList *l = messages; l; l = l->next) {
                struct P2PNetworkMessage *msg = l->data;
                if (msg) {
                    p2p_process_received_message(msg);
                    p2p_free_network_message(msg);
                }
            }
            g_list_free(messages);

            /* Update poll time */
            g_free(last_poll_time);
            GDateTime *dt = g_date_time_new_now_utc();
            last_poll_time = g_date_time_format_iso8601(dt);
            g_date_time_unref(dt);
        }

        /* Sleep for 5 seconds between polls */
        for (int i = 0; i < 50 && g_polling_active; i++) {
            g_usleep(100000);  /* 100ms */
        }
    }

    g_free(last_poll_time);
    return NULL;
}

/* Start message polling */
void
p2p_start_message_polling(void)
{
    if (g_polling_active) return;

    g_polling_active = TRUE;
    g_poll_thread = g_thread_new("p2p-poll", poll_thread_func, NULL);
    g_debug("P2P message polling started");
}

/* Stop message polling */
void
p2p_stop_message_polling(void)
{
    g_polling_active = FALSE;

    if (g_poll_thread) {
        g_thread_join(g_poll_thread);
        g_poll_thread = NULL;
    }
}

/* Poll for messages via tweetapus transport */
GList *
p2p_poll_tweetapus_messages(const gchar *since_timestamp)
{
    GList *messages = NULL;

    gchar *url = g_strdup_printf("%s/api/v1/p2p/encrypted/inbox?since=%s",
        g_transport_config.relay_server_url ? g_transport_config.relay_server_url : API_BASE_URL,
        since_timestamp ? since_timestamp : "1970-01-01T00:00:00Z");

    struct MemoryStruct chunk = {0};
    if (fetch_url(url, &chunk, NULL, "GET")) {
        /* Parse response - array of encrypted messages */
        JsonParser *parser = json_parser_new();
        GError *error = NULL;

        if (json_parser_load_from_data(parser, chunk.memory, -1, &error)) {
            JsonNode *root = json_parser_get_root(parser);
            if (root && JSON_NODE_TYPE(root) == JSON_NODE_ARRAY) {
                JsonArray *array = json_node_get_array(root);
                guint len = json_array_get_length(array);

                for (guint i = 0; i < len; i++) {
                    JsonObject *obj = json_array_get_object_element(array, i);
                    if (!obj) continue;

                    /* Extract encrypted message data */
                    const gchar *encoded_data = json_object_get_string_member(obj, "encrypted_data");
                    const gchar *sender_fp = json_object_get_string_member(obj, "sender_fingerprint");
                    gint64 nonce = json_object_get_int_member(obj, "nonce");

                    if (!encoded_data) continue;

                    /* Decode from base64 */
                    gchar *decoded = p2p_decode_from_tweetapus(encoded_data);
                    if (!decoded) continue;

                    /* Create network message */
                    struct P2PNetworkMessage *msg = g_new0(struct P2PNetworkMessage, 1);
                    msg->type = P2P_MSG_CHAT;
                    msg->sender_id = g_strdup(sender_fp);
                    msg->recipient_id = g_strdup(g_transport_config.local_username);
                    msg->payload = decoded;  /* This is the GPG encrypted content */
                    msg->nonce = (guint64)nonce;

                    messages = g_list_append(messages, msg);
                }
            }
        }

        if (error) g_error_free(error);
        g_object_unref(parser);
    }

    g_free(url);
    if (chunk.memory) g_free(chunk.memory);

    return messages;
}

/* Process received message (decrypt and store) */
gboolean
p2p_process_received_message(struct P2PNetworkMessage *net_msg)
{
    if (!net_msg || !net_msg->payload) return FALSE;

    /* Decrypt the payload */
    gchar *passphrase = NULL;  /* TODO: Get passphrase from user or keyring */
    gchar *plaintext = p2p_decrypt_message(net_msg->payload, passphrase);

    if (!plaintext) {
        g_warning("Failed to decrypt message from %s", net_msg->sender_id ? net_msg->sender_id : "unknown");
        return FALSE;
    }

    /* Store in conversation */
    if (g_p2p_session) {
        struct P2PMessage *msg = g_new0(struct P2PMessage, 1);
        msg->id = g_strdup_printf("p2p_recv_%lu", (unsigned long)net_msg->nonce);
        msg->sender_username = g_strdup(net_msg->sender_id);
        msg->recipient_username = g_strdup(g_transport_config.local_username);
        msg->plaintext_content = plaintext;
        msg->encrypted_content = g_strdup(net_msg->payload);
        msg->timestamp = g_strdup(net_msg->timestamp);
        msg->is_outgoing = FALSE;
        msg->is_verified = TRUE;  /* TODO: Verify signature */

        g_mutex_lock(&g_p2p_session->session_mutex);
        GList *conversation = g_hash_table_lookup(g_p2p_session->conversations, net_msg->sender_id);
        conversation = g_list_append(conversation, msg);
        g_hash_table_insert(g_p2p_session->conversations, g_strdup(net_msg->sender_id), conversation);
        g_mutex_unlock(&g_p2p_session->session_mutex);

        /* Update UI */
        g_idle_add(refresh_messages_idle_cb, g_strdup(net_msg->sender_id));
    }

    return TRUE;
}

/* Get transport mode */
P2PTransportMode
p2p_get_transport_mode(void)
{
    return g_transport_config.mode;
}

/* Set transport mode */
void
p2p_set_transport_mode(P2PTransportMode mode)
{
    g_mutex_lock(&g_network_mutex);
    g_transport_config.mode = mode;
    g_mutex_unlock(&g_network_mutex);

    g_debug("P2P transport mode changed to: %s", 
        mode == P2P_TRANSPORT_DIRECT ? "DIRECT" : "TWEETAPUS");
}

/* Check if listener is running */
gboolean
p2p_is_listener_running(void)
{
    return g_listener_running;
}

/* Get local listen address */
gchar *
p2p_get_listen_address(void)
{
    if (!g_listener_running || g_listen_port == 0) return NULL;

    gchar *host = g_transport_config.listen_host ? g_transport_config.listen_host : g_strdup("127.0.0.1");
    gchar *addr = g_strdup_printf("%s:%d", host, g_listen_port);
    
    if (!g_transport_config.listen_host) g_free(host);
    
    return addr;
}
