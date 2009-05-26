/*
 * XZ decoder as a single file for uncompressing the kernel and initramfs
 *
 * NOTE: This file hasn't been tested in kernel space yet!
 *
 * Author: Lasse Collin <lasse.collin@tukaani.org>
 *
 * This file has been put into the public domain.
 * You can do whatever you want with this file.
 */

/*
 * Important notes about in-place decompression
 *
 * At least on x86, the kernel is decompressed in place: the compressed data
 * is placed to the end of the output buffer, and the decompressor overwrites
 * most of the compressed data. There must be enough safety margin to
 * guarantee that the write position is always behind the read position.
 * The optimal safety margin for XZ with LZMA2 or BCJ+LZMA2 is calculated
 * below. Note that the margin with XZ is bigger than with Deflate (gzip)!
 *
 * The worst case for in-place decompression is that the beginning of
 * the file is compressed extremely well, and the rest of the file is
 * uncompressible. Thus, we must look for worst-case expansion when the
 * compressor is encoding uncompressible data.
 *
 * The structure of the .xz file in case of a compresed kernel is as follows.
 * Sizes (as bytes) of the fields are in parenthesis.
 *
 *    Stream Header (12)
 *    Block Header:
 *      Block Header (8-12)
 *      Compressed Data (N)
 *      Block Padding (0-3)
 *      CRC32 (4)
 *    Index (8-20)
 *    Stream Footer (12)
 *
 * Normally there is exactly one Block, but let's assume that there are
 * 2-4 Blocks just in case. Because Stream Header and also Block Header
 * of the first Block don't make the decompressor produce any uncompressed
 * data, we can ignore them from our calculations. Block Headers of possible
 * additional Blocks have to be taken into account still. With these
 * assumptions, it is safe to assume that the total header overhead is
 * less than 128 bytes.
 *
 * Compressed Data contains LZMA2 or BCJ+LZMA2 encoded data. Since BCJ
 * doesn't change the size of the data, it is enough to calculate the
 * safety margin for LZMA2.
 *
 * LZMA2 stores the data in chunks. Each chunk has a header whose size is
 * at maximum of 6 bytes, but to get round 2^n numbers, let's assume that
 * the maximum chunk header size is 8 bytes. After the chunk header, there
 * may be up to 64 KiB of actual payload in the chunk. Often the payload is
 * quite a bit smaller though; to be safe, let's assume that an average
 * chunk has only 32 KiB of payload.
 *
 * The maximum uncompressed size of the payload is 2 MiB. The minimum
 * uncompressed size of the payload is in practice never less than the
 * payload size itself. The LZMA2 format would allow uncompressed size
 * to be less than the payload size, but no sane compressor creates such
 * files. LZMA2 supports storing uncompressible data in uncompressed form,
 * so there's never a need to create payloads whose uncompressed size is
 * smaller than the compressed size.
 *
 * The assumption, that the uncompressed size of the payload is never
 * smaller than the payload itself, is valid only when talking about
 * the payload as a whole. It is possible that the payload has parts where
 * the decompressor consumes more input than it produces output. Calculating
 * the worst case for this would be tricky. Instead of trying to do that,
 * let's simply make sure that the decompressor never overwrites any bytes
 * of the payload which it is currently reading.
 *
 * Now we have enough information to calculate the safety margin. We need
 *   - 128 bytes for the .xz file format headers;
 *   - 8 bytes per every 32 KiB of uncompressed size (one LZMA2 chunk header
 *     per chunk, each chunk having average payload size of 32 KiB); and
 *   - 64 KiB (biggest possible LZMA2 chunk payload size) to make sure that
 *     the decompressor never overwrites anything from the LZMA2 chunk
 *     payload it is currently reading.
 *
 * We get the following formula:
 *
 *    safety_margin = 128 + uncompressed_size * 8 / 32768 + 65536
 *                  = 128 + (uncompressed_size >> 12) + 65536
 *
 * For comparision, according to arch/x86/boot/compressed/misc.c, the
 * equivalent formula for Deflate is this:
 *
 *    safety_margin = 18 + (uncompressed_size >> 12) + 32768
 *
 * Thus, when updating Deflate-only in-place kernel decompressor to
 * support XZ, the fixed overhead has to be increased from 18+32768 bytes
 * to 128+65536 bytes.
 */

/*
 * Set the linkage of normally extern functions to static when compiling
 * the decompressor the kernel image. This must be done before including
 * <linux/decompress/mm.h>, which defines STATIC to be empty if it wasn't
 * already defined. We also define XZ_STATIC which is used only in ifdefs
 * to test if STATIC was defined before <linux/decompress/mm.h> was included.
 */
#ifdef STATIC
#	define XZ_EXTERN STATIC
#	define XZ_STATIC
#endif

#ifdef __KERNEL__
#	include <linux/decompress/mm.h>
#	include <linux/decompress/generic.h>
#endif

/*
 * Use INIT defined in <linux/decompress/mm.h> to possibly add __init
 * to every function.
 */
#ifdef INIT
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
#	define XZ_DEC_POWERPC
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

#include "xz/xz_private.h"

#ifdef XZ_STATIC
/*
 * Replace the normal allocation functions with the versions
 * from <linux/decompress/mm.h>.
 */
#undef kmalloc
#undef kfree
#undef vmalloc
#undef vfree
#define kmalloc(size, flags) malloc(size)
#define kfree(ptr) free(ptr)
#define vmalloc(size) malloc(size)
#define vfree(ptr) free(ptr)

/*
 * FIXME: Not all basic memory functions are provided in architecture-specific
 * files (yet). We define our own versions here for now, but this should be
 * only a temporary solution.
 *
 * memeq and memzero are not used much and any remotely sane implementation
 * is fast enough. memcpy/memmove speed matters in multi-call mode, but
 * the kernel image is decompressed in single-call mode, in which only
 * memcpy speed can matter and only if there is a lot of uncompressible data
 * (LZMA2 stores uncompressible chunks in uncompressed form). Thus, the
 * functions below should just be kept small; it's probably not worth
 * optimizing for speed.
 */

#ifndef memeq
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
#endif

#ifndef memzero
static void XZ_FUNC memzero(void *buf, size_t size)
{
	uint8_t *b = buf;
	uint8_t *e = b + size;
	while (b != e)
		*b++ = '\0';
}
#endif

#ifndef memmove
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
#endif

/* Since we need memmove anyway, use it as memcpy too. */
#ifndef memcpy
#	define memcpy memmove
#endif
#endif /* XZ_STATIC */

#include "xz/xz_crc32.c"
#include "xz/xz_dec_stream.c"
#include "xz/xz_dec_lzma2.c"
#ifdef XZ_DEC_BCJ
#	include "xz/xz_dec_bcj.c"
#endif

/*
 * Maximum LZMA2 dictionary size. This matters only in multi-call mode.
 * If you change this, remember to update also the error message in
 * "case XZ_MEMLIMIT_ERROR".
 */
#define DICT_MAX (1024 * 1024)

/*
 * This function implements the API defined in <linux/decompress/generic.h>.
 *
 * This wrapper will automatically choose single-call or multi-call mode
 * of the native XZ decoder API. The single-call mode can be used only when
 * both input and output buffers are available as a single chunk, i.e. when
 * fill() and flush() won't be used.
 *
 * This API doesn't provide a way to specify the maximum dictionary size
 * for the multi-call mode of the native XZ decoder API. We will use
 * DICT_MAX bytes, which will be allocated with vmalloc().
 *
 * Input:
 *  - If in_size > 0, in is assumed to have in_size bytes of data that should
 *    be decompressed.
 *  - If in_size == 0 and in != NULL, in is assumed to have COMPR_IOBUF_SIZE
 *    bytes of space that we can use with fill(). (COMPR_IOBUF_SIZE is
 *    defined in <linux/decompress/generic.h>.)
 *  - If in_size == 0 and in == NULL, we will kmalloc() a temporary buffer
 *    of COMPR_IOBUF_SIZE bytes to be used with fill().
 *  - If in_used != NULL, the amount of input used after successful
 *    decompression will be stored in *in_used. If an error occurs,
 *    the value of *in_used is undefined.
 *
 * Output:
 *  - If flush == NULL, out is used the output buffer and flush() is not
 *    used. We don't know the size of the output buffer though, so we just
 *    hope that we don't overflow it. We also don't have any way to tell
 *    the caller how much uncompressed output we produced. This is OK when
 *    decompressing the kernel image.
 *  - If flush != NULL, out is ignored. We will kmalloc() a temporary buffer
 *    of COMPR_IOBUF_SIZE bytes to be used with flush().
 *
 * Memory usage:
 *  - If fill() and flush() are not used, about 30 KiB of memory is needed.
 *  - If fill() or flush() is used, about 30 KiB + DICT_SIZE bytes of memory
 *    is needed.
 *  - The memory allocator should return 8-byte aligned pointers, because
 *    there are a few uint64_t variables in the XZ decompressor.
 *
 * Error handling:
 *  - If decompression is successful, this function returns zero.
 *  - If an error occurs, error() is called with a string constant describing
 *    the error. If error() returns, this function returns -1.
 */
XZ_EXTERN int XZ_FUNC unxz(unsigned char *in, int in_size,
		int (*fill)(void *dest, unsigned int size),
		int (*flush)(/*const*/ void *src, unsigned int size),
		unsigned char *out, int *in_used,
		void (*error)(/*const*/ char *x))
{
	struct xz_buf b;
	struct xz_dec *s;
	enum xz_ret ret;
	int tmp;
	bool in_allocated = false;

	xz_crc32_init();

	if (in_size > 0 && flush == NULL)
		s = xz_dec_init(0);
	else
		s = xz_dec_init(DICT_MAX);

	if (s == NULL)
		goto error_alloc_state;

	b.in = in;
	b.in_pos = 0;
	b.in_size = in_size;
	b.out = out;
	b.out_pos = 0;
	b.out_size = (size_t)-1;

	if (in_used != NULL)
		*in_used = 0;

	if (in_size > 0 && flush == NULL) {
		ret = xz_dec_run(s, &b);
	} else {
		if (in == NULL) {
			in = kmalloc(COMPR_IOBUF_SIZE, GFP_KERNEL);
			if (in == NULL)
				goto error_alloc_in;

			in_allocated = true;
			b.in = in;
		}

		if (flush != NULL) {
			b.out = kmalloc(COMPR_IOBUF_SIZE, GFP_KERNEL);
			if (b.out == NULL)
				goto error_alloc_out;

			b.out_size = COMPR_IOBUF_SIZE;
		}

		do {
			if (b.in_pos == b.in_size && in_size == 0) {
				if (in_used != NULL)
					*in_used += b.in_pos;

				b.in_pos = 0;

				tmp = fill(in, COMPR_IOBUF_SIZE);
				if (tmp < 0) {
					/*
					 * This isn't an optimal error code
					 * but it probably isn't worth making
					 * a new one either.
					 */
					ret = XZ_BUF_ERROR;
					break;
				}

				b.in_size = tmp;
			}

			ret = xz_dec_run(s, &b);

			if (flush != NULL) {
				if (b.out_pos == b.out_size || ret != XZ_OK) {
					if (flush(b.out, b.out_pos)
							!= b.out_pos)
						ret = XZ_BUF_ERROR;

					b.out_pos = 0;
				}
			}
		} while (ret == XZ_OK);

		if (flush != NULL)
			kfree(b.out);

		if (in_allocated)
			kfree(in);
	}

	if (in_used != NULL)
		*in_used += b.in_pos;

	xz_dec_end(s);

	switch (ret) {
	case XZ_STREAM_END:
		return 0;

	case XZ_MEMLIMIT_ERROR:
		/* This can occur only in multi-call mode. */
		error("Multi-call XZ decompressor limits "
				"the LZMA2 dictionary to 1 MiB");
		break;

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

	return -1;

error_alloc_out:
	if (in_allocated)
		kfree(in);

error_alloc_in:
	xz_dec_end(s);

error_alloc_state:
	error("XZ decoder ran out of memory");
	return -1;
}

/*
 * This macro is used by architecture-specific files to decompress
 * the kernel image.
 */
#define decompress unxz