/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FAKE_LINUX_SORT_H
#define _FAKE_LINUX_SORT_H

#include <stdlib.h>

/*
 * The kernel's sort() with the comparator shape the host test already has;
 * the swap hook is unused here (qsort picks its own element swapping).
 */
static inline void sort(void *base, size_t num, size_t size,
			int (*cmp)(const void *, const void *),
			void (*swap)(void *, void *, int))
{
	(void)swap;
	(void)size;
	qsort(base, num, size, cmp);
}

#endif
