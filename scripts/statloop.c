/* statloop.c -- per-lookup cost of the directory-guard hook, A/B instrument.
 *
 * Measures the cost a path walk pays per component under the protected
 * parents: on 0.7.0 that is the lookup_fast hook entry + the name hash +
 * the fixed-slot probe (untagged callers are passed through after it), on
 * 0.5.0 it is the plain lookup. Same binary on both sides, so the delta is
 * the hook. snprintf is inside the loop on purpose: its cost is constant
 * across the A/B and cancels in the difference.
 *
 * Build (Termux): cc -O2 -o statloop statloop.c
 * Run (root, untagged on purpose): su -c /data/local/tmp/statloop \
 *      /data/data 100000 probe
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>

static double ns_per_call(const char *dir, const char *prefix, long n)
{
	struct timespec t0, t1;
	char path[512];
	struct stat sb;

	for (long i = 0; i < 1000;
	     i++) { /* warm the dcache and the allocator */
		snprintf(path, sizeof path, "%s/%s_warm%ld", dir, prefix, i);
		stat(path, &sb);
	}
	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (long i = 0; i < n; i++) {
		snprintf(path, sizeof path, "%s/%s_%ld", dir, prefix, i);
		stat(path,
		     &sb); /* a fresh name: ->lookup runs, a negative entry lands */
	}
	clock_gettime(CLOCK_MONOTONIC, &t1);
	return ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) /
	       (double)n;
}

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : "/data/data";
	long n = argc > 2 ? atol(argv[2]) : 100000;
	const char *prefix = argc > 3 ? argv[3] : "probe";

	if (n < 1000)
		return 1;
	/* three rounds: the spread between them is the noise floor */
	for (int round = 1; round <= 3; round++)
		printf("round %d: %8.1f ns/lookup (%s, n=%ld)\n", round,
		       ns_per_call(dir, prefix, n), dir, n);
	return 0;
}
