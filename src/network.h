#ifndef NETWORK_H
#define NETWORK_H

#include <glib.h>
#include "types.h"

gboolean fetch_url(const gchar *url, struct MemoryStruct *chunk, const gchar *post_data, const gchar *method);
gboolean fetch_url_internal(const gchar *url, struct MemoryStruct *chunk, const gchar *post_data, const gchar *method, long *response_code);
gboolean fetch_url_with_file(const gchar *url, struct MemoryStruct *chunk, const gchar *file_path, const gchar *field_name);
gboolean fetch_url_with_auth_token(const gchar *url,
                                   struct MemoryStruct *chunk,
                                   const gchar *post_data,
                                   const gchar *method,
                                   const gchar *auth_token_override);
gboolean fetch_url_with_file_auth_token(const gchar *url,
                                        struct MemoryStruct *chunk,
                                        const gchar *file_path,
                                        const gchar *field_name,
                                        const gchar *auth_token_override);

#endif /* NETWORK_H */
