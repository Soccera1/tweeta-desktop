pub const c = @cImport({
    @cInclude("locale.h");
    @cInclude("glib.h");
    @cInclude("glib/gstdio.h");
    @cInclude("gio/gio.h");
    @cInclude("gtk/gtk.h");
    @cInclude("curl/curl.h");
    @cInclude("json-glib/json-glib.h");
    @cInclude("gpgme.h");
});
