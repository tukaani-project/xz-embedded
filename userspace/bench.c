// SPDX-License-Identifier: 0BSD

/*
 * Benchmark file decompression speed
 *
 * Usage:
 * Single-call mode, 10 iterations: bench foo.xz s 10
 * Multi-call mode, 10 iterations:  bench foo.xz m 10
 *
 * Author: Lasse Collin <lasse.collin@tukaani.org>
 */

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "xz.h"

#ifndef DICT_SIZE_MAX
#	define DICT_SIZE_MAX (64U << 20)
#endif

#define BUFFER_SIZE (64 * 1024 * 1024)

static uint8_t in[BUFFER_SIZE];
static uint8_t out[BUFFER_SIZE];

static void decode_single(struct xz_dec *s, size_t in_size)
{
	struct xz_buf b = {
		.in = in,
		.in_pos = 0,
		.in_size = in_size,
		.out = out,
		.out_pos = 0,
		.out_size = sizeof(out),
	};

	enum xz_ret ret = xz_dec_run(s, &b);

	if (ret != XZ_STREAM_END) {
		fprintf(stderr, "Single-call decoding failed (%d)\n",
				(int)ret);
		exit(1);
	}
}

static void decode_multi(struct xz_dec *s, size_t in_size)
{
	const size_t chunk_size = 4096;

	struct xz_buf b = {
		.in = in,
		.in_pos = 0,
		.in_size = 0,
		.out = out,
		.out_pos = 0,
		.out_size = chunk_size,
	};

	enum xz_ret ret;

	do {
		if (b.in_pos == b.in_size) {
			b.in_size += chunk_size;
			if (b.in_size > in_size)
				b.in_size = in_size;
		}

		if (b.out_pos == b.out_size)
			b.out_pos = 0;

		ret = xz_dec_run(s, &b);
	} while (ret == XZ_OK);

	if (ret != XZ_STREAM_END) {
		fprintf(stderr, "Multi-call decoding failed (%d)\n",
				(int)ret);
		exit(1);
	}
}

int main(int argc, char **argv)
{
	FILE *file;
	size_t in_size;
	bool use_multi_call;
	int iters;
	struct timespec start;
	struct timespec end;
	struct timespec result;
	struct xz_dec *s;

	if (argc != 4) {
		fprintf(stderr, "Usage: %s FILE.xz <s|m> ITERATIONS\n",
				argv[0]);
		return 1;
	}

	if (strcmp(argv[2], "s") == 0) {
		use_multi_call = false;
	} else if (strcmp(argv[2], "m") == 0) {
		use_multi_call = true;
	} else {
		fprintf(stderr, "Expected 's' (single- call mode) or "
				"'m' (multi-call mode): '%s'\n", argv[2]);
		return 1;
	}

	iters = atoi(argv[3]);
	if (iters <= 0) {
		fprintf(stderr, "Invalid iteration count '%s'\n", argv[3]);
		return 1;
	}

	file = fopen(argv[1], "rb");
	if (file == NULL) {
		fprintf(stderr, "Cannot open '%s'\n", argv[1]);
		return 1;
	}

	in_size = fread(in, 1, sizeof(in), file);
	if (ferror(file)) {
		fprintf(stderr, "Error reading from file '%s'\n", argv[1]);
		return 1;
	}

	if (!feof(file)) {
		fprintf(stderr, "The file '%s' is too big\n", argv[1]);
		return 1;
	}

	xz_crc32_init();

	if (use_multi_call)
		s = xz_dec_init(XZ_DYNALLOC, DICT_SIZE_MAX);
	else
		s = xz_dec_init(XZ_SINGLE, 0);

	if (s == NULL) {
		fputs("Initialization failed\n", stderr);
		return 1;
	}

	while (iters-- > 0) {
		// Single-call mode self-resets. Resetting is cheap though,
		// so this condition is here primarily for testing.
		if (use_multi_call)
			xz_dec_reset(s);

		if (clock_gettime(CLOCK_MONOTONIC, &start)) {
			fputs("clock_gettime() failed\n", stderr);
			return 1;
		}

		if (use_multi_call)
			decode_multi(s, in_size);
		else
			decode_single(s, in_size);

		if (clock_gettime(CLOCK_MONOTONIC, &end)) {
			fputs("clock_gettime() failed\n", stderr);
			return 1;
		}

		result.tv_sec = end.tv_sec - start.tv_sec
				- (end.tv_nsec < start.tv_nsec);
		result.tv_nsec = end.tv_nsec - start.tv_nsec
					+ (end.tv_nsec < start.tv_nsec
						? 1000000000 : 0);

		printf("%0.1f ms\n", (double)result.tv_sec * 1000.0
				+ (double)result.tv_nsec / 1000000.0);
	}

	xz_dec_end(s);

	return 0;
}
