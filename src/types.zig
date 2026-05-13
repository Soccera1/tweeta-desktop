const c = @import("c.zig").c;

pub const TimelineType = enum(c_int) {
    TIMELINE_PUBLIC = 0,
    TIMELINE_FOLLOWING = 1,
};

pub const P2PTransportMode = enum(c_int) {
    P2P_TRANSPORT_DIRECT = 0,
    P2P_TRANSPORT_TWEETAPUS = 1,
};

pub const P2PMessageType = enum(c_int) {
    P2P_MSG_HELLO = 0,
    P2P_MSG_CHAT = 1,
    P2P_MSG_PING = 2,
    P2P_MSG_PONG = 3,
    P2P_MSG_BYE = 4,
};

pub const Attachment = extern struct {
    id: [*c]c.gchar,
    file_url: [*c]c.gchar,
    file_type: [*c]c.gchar,
    file_hash: [*c]c.gchar,
    file_name: [*c]c.gchar,
    file_size: c.gint64,
    is_spoiler: c.gboolean,
};

pub const Tweet = extern struct {
    content: [*c]c.gchar,
    author_id: [*c]c.gchar,
    author_name: [*c]c.gchar,
    author_username: [*c]c.gchar,
    author_avatar: [*c]c.gchar,
    author_verified: c.gboolean,
    author_gold: c.gboolean,
    author_gray: c.gboolean,
    id: [*c]c.gchar,
    note: [*c]c.gchar,
    note_severity: [*c]c.gchar,
    edited_at: [*c]c.gchar,
    attachments: [*c]c.GList,
    liked: c.gboolean,
    retweeted: c.gboolean,
    bookmarked: c.gboolean,
    like_count: c_int,
    retweet_count: c_int,
    reply_count: c_int,
    view_count: c_int,
    quote_count: c_int,
    reaction_count: c_int,
    pinned: c.gboolean,
    quote_tweet: [*c]Tweet,
    poll: [*c]Poll,
    content_type: [*c]c.gchar,
    retweet_created_at: [*c]c.gchar,
    original_post_id: [*c]c.gchar,
    article_title: [*c]c.gchar,
    article_body_markdown: [*c]c.gchar,
    created_at: [*c]c.gchar,
};

pub const Emoji = extern struct {
    id: [*c]c.gchar,
    name: [*c]c.gchar,
    file_url: [*c]c.gchar,
    file_hash: [*c]c.gchar,
    created_by: [*c]c.gchar,
};

pub const Profile = extern struct {
    id: [*c]c.gchar,
    name: [*c]c.gchar,
    username: [*c]c.gchar,
    bio: [*c]c.gchar,
    avatar: [*c]c.gchar,
    banner: [*c]c.gchar,
    location: [*c]c.gchar,
    website: [*c]c.gchar,
    pronouns: [*c]c.gchar,
    theme: [*c]c.gchar,
    accent_color: [*c]c.gchar,
    label_type: [*c]c.gchar,
    follower_count: c_int,
    following_count: c_int,
    post_count: c_int,
    avatar_radius: c_int,
    is_following: c.gboolean,
    follows_me: c.gboolean,
    is_own_profile: c.gboolean,
    blocked_by_profile: c.gboolean,
    blocked_profile: c.gboolean,
    notify_tweets: c.gboolean,
    label_automated: c.gboolean,
    author_verified: c.gboolean,
    author_gold: c.gboolean,
    author_gray: c.gboolean,
};

pub const Notification = extern struct {
    id: [*c]c.gchar,
    type: [*c]c.gchar,
    content: [*c]c.gchar,
    related_id: [*c]c.gchar,
    actor_id: [*c]c.gchar,
    actor_username: [*c]c.gchar,
    actor_name: [*c]c.gchar,
    actor_avatar: [*c]c.gchar,
    actor_verified: c.gboolean,
    actor_gold: c.gboolean,
    read: c.gboolean,
    created_at: [*c]c.gchar,
};

pub const DirectMessage = extern struct {
    id: [*c]c.gchar,
    conversation_id: [*c]c.gchar,
    sender_id: [*c]c.gchar,
    content: [*c]c.gchar,
    message_type: [*c]c.gchar,
    reply_to: [*c]c.gchar,
    reply_preview: [*c]c.gchar,
    username: [*c]c.gchar,
    name: [*c]c.gchar,
    avatar: [*c]c.gchar,
    verified: c.gboolean,
    is_deleted: c.gboolean,
    created_at: [*c]c.gchar,
    edited_at: [*c]c.gchar,
    reactions_summary: [*c]c.gchar,
    mpi_kind: [*c]c.gchar,
    mpi_status: [*c]c.gchar,
    mpi_net: [*c]c.gchar,
    mpi_gross: [*c]c.gchar,
    mpi_note: [*c]c.gchar,
    mpi_order_id: [*c]c.gchar,
    mpi_payment_link_url: [*c]c.gchar,
    attachments: [*c]c.GList,
};

pub const Conversation = extern struct {
    id: [*c]c.gchar,
    type: [*c]c.gchar,
    title: [*c]c.gchar,
    display_name: [*c]c.gchar,
    display_avatar: [*c]c.gchar,
    last_message_content: [*c]c.gchar,
    last_message_time: [*c]c.gchar,
    last_message_sender: [*c]c.gchar,
    last_message_sender_name: [*c]c.gchar,
    unread_count: c_int,
    participant_count: c_int,
    disappearing_enabled: c.gboolean,
    disappearing_duration: c_int,
    participants: [*c]c.GList,
};

pub const AdminStats = extern struct {
    total_users: c.gint64,
    suspended_users: c.gint64,
    restricted_users: c.gint64,
    verified_users: c.gint64,
    gold_users: c.gint64,
    gray_users: c.gint64,
    total_posts: c.gint64,
    active_suspensions: c.gint64,
    active_restricted: c.gint64,
    active_suspended: c.gint64,
};

pub const MemoryStruct = extern struct {
    memory: [*c]c.gchar,
    size: usize,
};

pub const AsyncData = extern struct {
    list_box: ?*c.GtkListBox,
    tweets: [*c]c.GList,
    users: [*c]c.GList,
    notifications: [*c]c.GList,
    conversations: [*c]c.GList,
    messages: [*c]c.GList,
    communities: [*c]c.GList,
    lists: [*c]c.GList,
    success: c.gboolean,
    has_more: c.gboolean,
    profile: [*c]Profile,
    conversation: [*c]Conversation,
    list: [*c]TweetaList,
    username: [*c]c.gchar,
    query: [*c]c.gchar,
    conversation_id: [*c]c.gchar,
    community_id: [*c]c.gchar,
    json_data: [*c]c.gchar,
    request_id: c.guint,
    is_append: c.gboolean,
    before_id: [*c]c.gchar,
};

pub const AvatarData = extern struct {
    image: ?*c.GtkWidget,
    url: [*c]c.gchar,
    size: c_int,
};

pub const UploadContext = extern struct {
    parent_dialog: ?*c.GtkWidget,
    file_label: ?*c.GtkWidget,
    file_path: [*c]c.gchar,
    file_type: [*c]c.gchar,
    remote_url: [*c]c.gchar,
    remote_type: [*c]c.gchar,
};

pub const ReplyContext = extern struct {
    text_view: ?*c.GtkWidget,
    reply_to_id: [*c]c.gchar,
    upload: UploadContext,
};

pub const QuoteContext = extern struct {
    text_view: ?*c.GtkWidget,
    quote_id: [*c]c.gchar,
};

pub const NoteContext = extern struct {
    text_view: ?*c.GtkWidget,
    tweet_id: [*c]c.gchar,
    severity: [*c]c.gchar,
};

pub const InteractionData = extern struct {
    tweet_id: [*c]c.gchar,
    button: ?*c.GtkWidget,
    state_ptr: [*c]c.gboolean,
    count_ptr: [*c]c_int,
};

pub const ReactionContext = extern struct {
    tweet_id: [*c]c.gchar,
    parent_window: ?*c.GtkWidget,
};

pub const PollOption = extern struct {
    id: [*c]c.gchar,
    option_text: [*c]c.gchar,
    vote_count: c_int,
    percentage: c_int,
    voted: c.gboolean,
    user_vote: [*c]c.gchar,
};

pub const Poll = extern struct {
    id: [*c]c.gchar,
    question: [*c]c.gchar,
    kind: [*c]c.gchar,
    steps: ?*c.JsonNode,
    options: [*c]c.GList,
    is_active: c.gboolean,
    expires_at: [*c]c.gchar,
    total_votes: c_int,
    has_user_answers: c.gboolean,
    user_score: c_int,
    user_total: c_int,
};

pub const InteractionState = extern struct {
    liked: c.gboolean,
    retweeted: c.gboolean,
    bookmarked: c.gboolean,
};

pub const Community = extern struct {
    id: [*c]c.gchar,
    name: [*c]c.gchar,
    description: [*c]c.gchar,
    rules: [*c]c.gchar,
    icon_url: [*c]c.gchar,
    banner_url: [*c]c.gchar,
    access_mode: [*c]c.gchar,
    member_count: c_int,
    is_member: c.gboolean,
    is_admin: c.gboolean,
    is_moderator: c.gboolean,
    tag_enabled: c.gboolean,
    tag_emoji: [*c]c.gchar,
    tag_text: [*c]c.gchar,
};

pub const TweetaList = extern struct {
    id: [*c]c.gchar,
    user_id: [*c]c.gchar,
    name: [*c]c.gchar,
    description: [*c]c.gchar,
    owner_username: [*c]c.gchar,
    owner_name: [*c]c.gchar,
    member_count: c.gint,
    follower_count: c.gint,
    is_private: c.gboolean,
    is_following: c.gboolean,
    is_owner: c.gboolean,
    members: [*c]c.GList,
    followers: [*c]c.GList,
};

pub const P2PContact = extern struct {
    username: [*c]c.gchar,
    display_name: [*c]c.gchar,
    public_key_fingerprint: [*c]c.gchar,
    public_key_armor: [*c]c.gchar,
    avatar_url: [*c]c.gchar,
    is_online: c.gboolean,
    last_seen: [*c]c.gchar,
    direct_host: [*c]c.gchar,
    direct_port: c.guint16,
};

pub const P2PMessage = extern struct {
    id: [*c]c.gchar,
    sender_username: [*c]c.gchar,
    recipient_username: [*c]c.gchar,
    encrypted_content: [*c]c.gchar,
    plaintext_content: [*c]c.gchar,
    timestamp: [*c]c.gchar,
    is_outgoing: c.gboolean,
    is_verified: c.gboolean,
};

pub const P2PSession = extern struct {
    local_username: [*c]c.gchar,
    local_key_fingerprint: [*c]c.gchar,
    contacts: ?*c.GHashTable,
    conversations: ?*c.GHashTable,
    session_mutex: c.GMutex,
};

pub const ProfileEditContext = extern struct {
    name_entry: ?*c.GtkWidget,
    bio_entry: ?*c.GtkWidget,
    avatar_btn: ?*c.GtkWidget,
    banner_btn: ?*c.GtkWidget,
    new_avatar_path: [*c]c.gchar,
    new_banner_path: [*c]c.gchar,
};

pub const P2PNetworkMessage = extern struct {
    type: P2PMessageType,
    sender_id: [*c]c.gchar,
    recipient_id: [*c]c.gchar,
    payload: [*c]c.gchar,
    timestamp: [*c]c.gchar,
    nonce: c.guint64,
};

pub const P2PTransportConfig = extern struct {
    mode: P2PTransportMode,
    local_username: [*c]c.gchar,
    local_key_fingerprint: [*c]c.gchar,
    listen_host: [*c]c.gchar,
    listen_port: c.guint16,
    relay_server_url: [*c]c.gchar,
};

pub const P2PContactInfo = extern struct {
    username: [*c]c.gchar,
    public_key_fingerprint: [*c]c.gchar,
    preferred_mode: P2PTransportMode,
    direct_host: [*c]c.gchar,
    direct_port: c.guint16,
    last_seen: [*c]c.gchar,
    is_online: c.gboolean,
    socket_fd: c_int,
};
