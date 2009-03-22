/*
 * Test application for xz_boot.c
 *
 * Author: Lasse Collin <lasse.collin@tukaani.org>
 *
 * This file has been put into the public domain.
 * You can do whatever you want with this file.
 */

#include <stdio.h>

static void error(const char *msg)
{
	fprintf(stderr, "%s\n", msg);
}

#define XZ_MEM_FUNCS
#include "../linux/lib/xz/xz_boot.c"

static uint8_t in[1024 * 1024];
static uint8_t out[1024 * 1024];
static uint8_t heap[40 * 1024];

int main(void)
{
	int ret;
	struct xz_buf b = { in, 0, 0, out, 0, sizeof(out) };
	b.in_size = fread(in, 1, sizeof(in), stdin);

	malloc_buf = heap;
	malloc_avail = sizeof(heap);
	ret = xz_dec_buf(&b);

	fwrite(out, 1, b.out_pos, stdout);
	fprintf(stderr, "%d\n", ret);
	return 0;
}
