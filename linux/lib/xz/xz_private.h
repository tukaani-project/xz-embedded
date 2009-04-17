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
#	ifndef XZ_IGNORE_KCONFIG
#		ifdef CONFIG_XZ_DEC_X86
#			define XZ_DEC_X86
#		endif
#		ifdef CONFIG_XZ_DEC_POWERPC
#			define XZ_DEC_POWERPC
#		endif
#		ifdef CONFIG_XZ_DEC_IA64
#			define XZ_DEC_IA64
#		endif
#		ifdef CONFIG_XZ_DEC_ARM
#			define XZ_DEC_ARM
#		endif
#		ifdef CONFIG_XZ_DEC_ARMTHUMB
#			define XZ_DEC_ARMTHUMB
#		endif
#		ifdef CONFIG_XZ_DEC_SPARC
#			define XZ_DEC_SPARC
#		endif
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
#	ifndef noinline_for_stack
#		ifdef __GNUC__
#			define noinline_for_stack __attribute__((__noinline__))
#		else
#			define noinline_for_stack
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

#ifndef XZ_DEC_BCJ
#	if defined(XZ_DEC_X86) || defined(XZ_DEC_POWERPC) \
			|| defined(XZ_DEC_IA64) || defined(XZ_DEC_ARM) \
			|| defined(XZ_DEC_ARM) || defined(XZ_DEC_ARMTHUMB) \
			|| defined(XZ_DEC_SPARC)
#		define XZ_DEC_BCJ
#	endif
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

/*
 * Allocate memory for BCJ decoders. xz_dec_bcj_reset() must be used before
 * calling xz_dec_bcj_run().
 */
extern struct xz_dec_bcj * XZ_FUNC xz_dec_bcj_create(void);

/*
 * Decode the Filter ID of a BCJ filter. This implementation doesn't
 * support custom start offsets, so no decoding of Filter Properties
 * is needed. Returns XZ_OK if the given Filter ID is supported.
 * Otherwise XZ_OPTIONS_ERROR is returned.
 */
extern enum xz_ret XZ_FUNC xz_dec_bcj_reset(struct xz_dec_bcj *s, uint8_t id);

/*
 * Decode raw BCJ + LZMA2 stream. This must be used only if there actually is
 * a BCJ filter in the chain. If the chain has only LZMA2, xz_dec_lzma2_run()
 * must be called directly.
 */
extern enum xz_ret XZ_FUNC xz_dec_bcj_run(struct xz_dec_bcj *s,
		struct xz_dec_lzma2 *lzma2, struct xz_buf *b);

/* Free the memory allocated for the BCJ filters. */
#define xz_dec_bcj_end(s) kfree(s)

#endif
