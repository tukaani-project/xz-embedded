/*
 * XZ decoder as a single file for uncompressing the kernel and initramfs
 *
 * Author: Lasse Collin <lasse.collin@tukaani.org>
 *
 * This file has been put into the public domain.
 * You can do whatever you want with this file.
 */

/*
 * Before #including this file, void error(char *) or void error(const char *)
 * must have been declared. It is called from xz_dec_buf() if an error
 * occurs; if error() returns, xz_dec_buf() will return false. On success,
 * xz_dec_buf() returns true, but if error() would never return, there's no
 * need to check the return value of xz_dec_buf().
 *
 * In the early phase of boot where kmalloc, memmove, and friends are not
 * available, you need to #define XZ_MEM_FUNCS before #including this file.
 * Before calling xz_dec_buf, set malloc_buf to the beginning of a workspace
 * buffer, and malloc_avail to the size of the workspace buffer. malloc_buf
 * must be aligned to a multiple of 8 bytes. About 30 KiB of space is
 * currently needed, but to be future-proof, having 40-50 KiB of space would
 * be good.
 *
 * Example:
 *
 *      static void error(const char *message);
 *      #define XZ_MEM_FUNCS
 *      #include "../../../../lib/xz/xz_boot.c"
 *      ...
 *      struct xz_buf b;
 *      b.in = (const uint8_t *)in_buf;
 *      b.in_pos = 0;
 *      b.in_size = in_buf_size;
 *      b.out = (uint8_t *)out_buf;
 *      b.out_pos = 0;
 *      b.out_size = out_buf_size_max;
 *      malloc_buf = (uint8_t *)free_mem_begin;
 *      malloc_avail = free_mem_avail;
 *      if (!xz_dec_buf(&b))
 *              goto error_handler;
 *
 * If xz_dec_buf() succeeded (it returned true), the amount if input consumed
 * is in b.in_pos and the amount of output used is in b.out_pos.
 */

/*
 * Allow using the macro INIT to mark all functions with __init.
 * INIT is already used for this purporse in the Deflate decoder,
 * so this should ease things a little.
 */
#if !defined(XZ_FUNC) && defined(INIT)
#	define XZ_FUNC INIT
#endif

/*
 * Use the internal CRC32 code instead of kernel's CRC32 module, which
 * is not available in early phase of booting.
 */
#define XZ_INTERNAL_CRC32

/*
 * Ignore the configuration specified in the kernel config for the xz_dec
 * module. For boot time use, we enable only the BCJ filter of the current
 * architecture, or none if no BCJ filter is available for the architecture.
 */
#define XZ_IGNORE_KCONFIG
#ifdef CONFIG_X86
#	define XZ_DEC_X86
#endif
#ifdef CONFIG_PPC
#	define XZ_DEC_PPC
#endif
#ifdef CONFIG_ARM
#	define XZ_DEC_ARM
#endif
#ifdef CONFIG_IA64
#	define XZ_DEC_IA64
#endif
#ifdef CONFIG_SPARC
#	define XZ_DEC_SPARC
#endif

#include "xz_private.h"

#ifdef XZ_MEM_FUNCS
/*
 * Very simple malloc, which just picks big enough chunk from a preallocated
 * buffer. There's no free(), because the decoder is (at least for now) used
 * only once per compilation unit. Thus, free() would be just waste of space.
 */
static uint8_t *malloc_buf;
static size_t malloc_avail;

static void * XZ_FUNC malloc(size_t size)
{
	void *ptr = malloc_buf;
	size = (size + 7) & ~(size_t)7;
	if (size > malloc_avail)
		return NULL;

	malloc_buf += size;
	malloc_avail -= size;
	return ptr;
}

/*
 * memeq and memzero are not used much and any remotely sane implementation
 * is fast enough. memcpy/memmove speed matters in multi-call mode, but
 * xz_dec_buf() uses only the single-call mode, in which only memcpy speed
 * can matter and only if there is a lot of uncompressible data (LZMA2 stores
 * uncompressible chunks in uncompressed form). Thus, the functions below
 * should just be kept small; it's probably not worth optimizing for speed.
 */
static bool XZ_FUNC memeq(const void *a, const void *b, size_t size)
{
	const uint8_t *x = a;
	const uint8_t *y = b;
	size_t i;

	for (i = 0; i < size; ++i)
		if (x[i] != y[i])
			return false;

	return true;
}

static void XZ_FUNC memzero(void *buf, size_t size)
{
	uint8_t *b = buf;
	uint8_t *e = b + size;
	while (b != e)
		*b++ = '\0';
}

static void * XZ_FUNC memmove(void *dest, const void *src, size_t size)
{
	uint8_t *d = dest;
	const uint8_t *s = src;
	size_t i;

	if (d < s) {
		for (i = 0; i < size; ++i)
			d[i] = s[i];
	} else if (d > s) {
		i = size;
		while (i-- > 0)
			d[i] = s[i];
	}

	return dest;
}

/* Since we need memmove anyway, use it as memcpy too. */
#define memcpy memmove
#endif

#include "xz_crc32.c"
#include "xz_dec_stream.c"
#include "xz_dec_lzma2.c"
#ifdef XZ_DEC_BCJ
#	include "xz_dec_bcj.c"
#endif

/**
 * xz_dec_buf() - Single-call XZ decoder
 * @b:          Input and output buffers
 *
 * On success, true is returned. On error, error() is called and false is
 * returned in case error() returns.
 */
static bool XZ_FUNC xz_dec_buf(struct xz_buf *b)
{
	struct xz_dec *s;
	enum xz_ret ret;

	xz_crc32_init();

	s = xz_dec_init(0);
	if (s == NULL) {
		error("XZ decoder ran out of memory");
		return false;
	}

	ret = xz_dec_run(s, b);
	xz_dec_end(s);

	switch (ret) {
	case XZ_STREAM_END:
		return true;

	case XZ_FORMAT_ERROR:
		error("Input is not in the XZ format (wrong magic bytes)");
		break;

	case XZ_OPTIONS_ERROR:
		error("Input was encoded with settings that are not "
				"supported by this XZ decoder");
		break;

	case XZ_DATA_ERROR:
		error("XZ-compressed data is corrupt");
		break;

	case XZ_BUF_ERROR:
		error("Output buffer is too small or the "
				"XZ-compressed data is corrupt");
		break;

	default:
		error("Bug in the XZ decoder");
		break;
	}

	return false;
}
