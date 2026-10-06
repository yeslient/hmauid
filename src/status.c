// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/syscalls.h>
#include <linux/uidgid.h>
#include <linux/user.h>
#include <linux/version.h>

#include "tosya.h"
#include "kaux.h"

static struct kaux_status g_status = {
	.magic = KAUX_STATUS_MAGIC,
	.version = KAUX_FAMILY_VERSION,
	/* what this build assumes; the tool checks it against the running kernel */
	.va_bits = CONFIG_ARM64_VA_BITS,
	.page_shift = PAGE_SHIFT,
};
/*
 * One lock for the whole record. Every path that touches it is cold -- an
 * install, an apply, a status reply -- but the reply copies the record as a
 * whole while the others update single fields, and the flags are
 * read-modify-write: an unlocked reader can be handed counts and flags from two
 * different moments, and two writers can lose a flag each.
 */
static DEFINE_MUTEX(g_status_lock);

void tosya_status_get(struct kaux_status *out)
{
	mutex_lock(&g_status_lock);
	*out = g_status;
	mutex_unlock(&g_status_lock);
	/* The reader checks these before it believes anything else, so they are set
	 * here and not left to whoever filled the rest in. */
	out->magic = KAUX_STATUS_MAGIC;
	out->size = sizeof(*out);
	out->version = KAUX_FAMILY_VERSION;
}

/* g_status_lock is held. */
static void status_set_hooks_locked(unsigned int native, unsigned int compat)
{
	g_status.native = native;
	g_status.compat = compat;
	/*
	 * Both are taken away before either is set again. A flag that is only ever
	 * added keeps saying the hooks are in place after the count that earned it
	 * has gone down -- and that is the one thing this line is read for.
	 */
	g_status.flags &= ~(KAUX_F_NATIVE | KAUX_F_COMPAT);
	if (native == g_status.native_expected)
		g_status.flags |= KAUX_F_NATIVE;
	if (compat == g_status.compat_expected)
		g_status.flags |= KAUX_F_COMPAT;
}

void tosya_status_set_hooks(unsigned int native, unsigned int compat)
{
	mutex_lock(&g_status_lock);
	status_set_hooks_locked(native, compat);
	mutex_unlock(&g_status_lock);
}
static void set_name(char *dst, size_t len, const char *src)
{
	if (src && *src) {
		strscpy(dst, src, len);
		return;
	}
	dst[0] = '\0';
}

void tosya_status_set_uid_tier(const char *name)
{
	mutex_lock(&g_status_lock);
	set_name(g_status.uid_tier, sizeof(g_status.uid_tier), name);
	mutex_unlock(&g_status_lock);
}

void tosya_status_set_setuid_tier(const char *name)
{
	mutex_lock(&g_status_lock);
	set_name(g_status.setuid_tier, sizeof(g_status.setuid_tier), name);
	if (name && *name)
		g_status.flags |= KAUX_F_SETUID;
	mutex_unlock(&g_status_lock);
}

void tosya_status_set_apks(unsigned int inodes, unsigned int expected,
			   unsigned int failed)
{
	mutex_lock(&g_status_lock);
	g_status.apk_inodes = inodes;
	g_status.apk_offered = expected;
	g_status.apk_failed = failed;
	/*
	 * An inode that could not be put in place is worth keeping: the next apply
	 * that goes well would otherwise report no failures at all, and a hook that
	 * is missing on one file is not something a later success undoes.
	 */
	g_status.apk_failed_total += failed;
	g_status.apk_updates++;
	if (failed == 0)
		g_status.flags |= KAUX_F_APKS;
	mutex_unlock(&g_status_lock);
}
void tosya_status_note(int error)
{
	mutex_lock(&g_status_lock);
	g_status.last_error = error;
	mutex_unlock(&g_status_lock);
}

void tosya_status_set_hooks_expected(unsigned int native, unsigned int compat)
{
	mutex_lock(&g_status_lock);
	g_status.native_expected = native;
	g_status.compat_expected = compat;
	mutex_unlock(&g_status_lock);
}

void tosya_status_add_hooks(unsigned int native, unsigned int compat)
{
	mutex_lock(&g_status_lock);
	status_set_hooks_locked(g_status.native + native,
				g_status.compat + compat);
	mutex_unlock(&g_status_lock);
}

void tosya_status_add_hooks_expected(unsigned int native, unsigned int compat)
{
	mutex_lock(&g_status_lock);
	g_status.native_expected += native;
	g_status.compat_expected += compat;
	mutex_unlock(&g_status_lock);
}
