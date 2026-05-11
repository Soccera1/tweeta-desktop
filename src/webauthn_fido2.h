#ifndef WEBAUTHN_FIDO2_H
#define WEBAUTHN_FIDO2_H

#include <glib.h>

gboolean webauthn_fido2_is_enabled(void);
gboolean webauthn_fido2_login(gchar **response_json_out, gchar **error_out);
gboolean webauthn_fido2_register(const gchar *username, gchar **response_json_out, gchar **error_out);

#endif /* WEBAUTHN_FIDO2_H */
