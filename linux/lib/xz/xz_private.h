/*
 * Private includes and definitions
 *
 * Author: Lasse Collin <lasse.collin@tukaani.org>
 *
 * This file has been put into the public domain.
 * You can do whatever you want with this file.
 */

#ifndef XZ_PRIVATE_H
#define XZ_PRIVATE_H

#ifdef __KERNEL__
#	ifndef XZ_MEM_FUNCS
#		include <linux/slab.h>
#		include <linux/vmalloc.h>
#		include <linux/string.h>
#	endif
#	include <linux/xz.h>
#else
#	ifndef XZ_MEM_FUNCS
#		include <stdlib.h>
#		include <string.h>
#		define kmalloc(size, flags) malloc(size)
#		define kfree(ptr) free(ptr)
#		define vmalloc(size) malloc(size)
#		define vfree(ptr) free(ptr)
#	endif
#	include <stdbool.h>
#	include "xz.h"
#	define min(x, y) ((x) < (y) ? (x) : (y))
#	define min_t(type, x, y) min(x, y)
#	ifndef __always_inline
#		ifdef __GNUC__
#			define __always_inline \
				inline __attribute__((__always_inline__))
#		else
#			define __always_inline inline
#		endif
#	endif
#endif

#ifdef XZ_MEM_FUNCS
#	define kmalloc(size, flags) malloc(size)
#	define kfree(ptr) ((void)0)
#	define vmalloc(size) malloc(size)
#	define vfree(ptr) ((void)0)
#else
#	define memeq(a, b, size) (memcmp(a, b, size) == 0)
#	define memzero(buf, size) memset(buf, 0, size)
#endif

/*
 * Allocate memory for LZMA2 decoder. xz_dec_lzma2_reset() must be used
 * before calling xz_dec_lzma2_run().
 */
extern struct xz_dec_lzma2 * XZ_FUNC xz_dec_lzma2_create(uint32_t dict_max);

/*
 * Decode the LZMA2 properties (one byte) and reset the decoder. Return
 * XZ_OK on success, XZ_MEMLIMIT_ERROR if the preallocated dictionary is not
 * big enough, and XZ_OPTIONS_ERROR if props indicates something that this
 * decoder doesn't support.
 */
extern enum xz_ret XZ_FUNC xz_dec_lzma2_reset(
		struct xz_dec_lzma2 *s, uint8_t props);

/* Decode raw LZMA2 stream from b->in to b->out. */
extern enum xz_ret XZ_FUNC xz_dec_lzma2_run(
		struct xz_dec_lzma2 *s, struct xz_buf *b);

/* Free the memory allocated for the LZMA2 decoder. */
extern void XZ_FUNC xz_dec_lzma2_end(struct xz_dec_lzma2 *s);

/* BCJ filter types (Branch/Jump/Call) */
enum xz_bcj_type {
	XZ_BCJ_NONE = 0,
	XZ_BCJ_X86 = 4,
	XZ_BCJ_POWERPC = 5, /* Big endian */
	XZ_BCJ_IA64 = 6,
	XZ_BCJ_ARM = 7, /* Little endian */
	XZ_BCJ_ARMTHUMB = 8, /* Little endian */
	XZ_BCJ_SPARC = 9
};

#endif
