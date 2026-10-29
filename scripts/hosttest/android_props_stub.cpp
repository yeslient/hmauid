// Host-build definitions for the property calls watcher.cpp links against.
// Never called on a host: the ce_available probe reads Android properties that
// do not exist there. Abort, so a host run that reaches them fails loudly.
#include <cstdio>
#include <cstdlib>

#include <sys/system_properties.h>

extern "C" {

int __system_property_get(const char *, char *)
{
	std::fputs("host build reached __system_property_get\n", stderr);
	std::abort();
}

const struct prop_info *__system_property_find(const char *)
{
	std::fputs("host build reached __system_property_find\n", stderr);
	std::abort();
}

} // extern "C"
