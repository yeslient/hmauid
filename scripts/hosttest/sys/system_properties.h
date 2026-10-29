/* Host-build stub for the NDK's <sys/system_properties.h>.
 *
 * Only what src/tools/watcher.cpp uses: the two calls that go straight through
 * the link (get/find) and the opaque handle find returns. The wait/serial pair
 * is dlsym'ed at run time and never links. On the host the definitions in
 * android_props_stub.cpp abort: a host build must never actually read Android
 * properties. */
#ifndef _FAKE_SYS_SYSTEM_PROPERTIES_H
#define _FAKE_SYS_SYSTEM_PROPERTIES_H

#include <stddef.h>

#define PROP_VALUE_MAX 92

#ifdef __cplusplus
extern "C" {
#endif

struct prop_info;

int __system_property_get(const char *name, char *value);
const struct prop_info *__system_property_find(const char *name);

#ifdef __cplusplus
}
#endif

#endif
