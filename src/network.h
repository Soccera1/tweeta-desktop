#ifndef NETWORK_H
#define NETWORK_H

#include <glib.h>
#include "types.h"

gboolean fetch_url(const gchar *url, struct MemoryStruct *chunk, const gchar *post_data, const gchar *method);
gboolean fetch_url_internal(const gchar *url, struct MemoryStruct *chunk, const gchar *post_data, const gchar *method, long *response_code);

#endif // NETWORK_H
