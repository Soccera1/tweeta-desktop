#ifndef NETWORK_H
#define NETWORK_H

#include <glib.h>
#include "types.h"

gboolean fetch_url(const gchar *url, struct MemoryStruct *chunk, const gchar *post_data);

#endif // NETWORK_H
