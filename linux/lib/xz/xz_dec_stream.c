/*
 * .xz Stream decoder
 *
 * Author: Lasse Collin <lasse.collin@tukaani.org>
 *
 * This file has been put into the public domain.
 * You can do whatever you want with this file.
 */

#include "xz_private.h"
#include "xz_stream.h"

#ifdef __KERNEL__
#	include <asm/byteorder.h>
#	define read_le32(buf) le32_to_cpup((const uint32_t *)(buf))
#else
static uint32_t XZ_FUNC read_le32(const uint8_t *buf)
{
	return (uint32_t)buf[0]
			| ((uint32_t)buf[1] << 8)
			| ((uint32_t)buf[2] << 16)
			| ((uint32_t)buf[3] << 24);
}
#endif

/* Hash used to validate the Index field */
struct xz_dec_hash {
	vli_type unpadded;
	vli_type uncompressed;
	uint32_t crc32;
};

struct xz_dec {
	/* Position in dec_main() */
	enum {
		SEQ_STREAM_HEADER,
		SEQ_BLOCK_START,
		SEQ_BLOCK_HEADER,
		SEQ_BLOCK_UNCOMPRESS,
		SEQ_BLOCK_PADDING,
		SEQ_BLOCK_CHECK,
		SEQ_INDEX_COUNT,
		SEQ_INDEX_UNPADDED,
		SEQ_INDEX_UNCOMPRESSED,
		SEQ_INDEX_PADDING,
		SEQ_INDEX_CRC32,
		SEQ_STREAM_FOOTER
	} sequence;

	/* Position in variable-length integers and Check fields */
	uint32_t pos;

	/* Variable-length integer decoded by dec_vli() */
	vli_type vli;

	/* Saved in_pos and out_pos */
	size_t in_start;
	size_t out_start;

	/* CRC32 value in Block or Index */
	uint32_t crc32;

	/* True if CRC32 is calculated from uncompressed data */
	bool has_crc32;

	/* True if we are operating in single-call mode. */
	bool single_call;

	/*
	 * True if the next call to xz_dec_run() is allowed to return
	 * XZ_BUF_ERROR.
	 */
	bool allow_buf_error;

	/* Information stored in Block Header */
	struct {
		/*
		 * Value stored in the Compressed Size field, or
		 * VLI_UNKNOWN if Compressed Size is not present.
		 */
		vli_type compressed;

		/*
		 * Value stored in the Uncompressed Size field, or
		 * VLI_UNKNOWN if Uncompressed Size is not present.
		 */
		vli_type uncompressed;

		/* Size of the Block Header field */
		uint32_t size;

#ifdef XZ_DEC_BCJ
		/* Type of the BCJ filter */
		enum xz_bcj_type bcj_type;
#endif
	} block_header;

	/* Information collected when decoding Blocks */
	struct {
		/* Observed compressed size of the current Block */
		vli_type compressed;

		/* Observed uncompressed size of the current Block */
		vli_type uncompressed;

		/* Number of Blocks decoded so far */
		vli_type count;

		/*
		 * Hash calculated from the Block sizes. This is used to
		 * validate the Index field.
		 */
		struct xz_dec_hash hash;
	} block;

	/* Variables needed when verifying the Index field */
	struct {
		/* Size of the Index in bytes */
		vli_type size;

		/* Number of Records (matches block.count in valid files) */
		vli_type count;

		/*
		 * Hash calculated from the Records (matches block.hash in
		 * valid files).
		 */
		struct xz_dec_hash hash;
	} index;

	/*
	 * Temporary buffer needed to hold Stream Header, Block Header,
	 * and Stream Footer. The Block Header is the biggest (1 KiB)
	 * so we reserve space according to that. buf[] has to be aligned
	 * to a multiple of four bytes; the size_t variables before it
	 * should guarantee this.
	 */
	struct {
		size_t pos;
		size_t size;
		uint8_t buf[1024];
	} temp;

	struct xz_dec_lzma2 *lzma2;

#ifdef XZ_DEC_BCJ
	struct xz_dec_bcj *bcj;
#endif
};

/* Call the raw filter decoder chain. */
static enum xz_ret XZ_FUNC xz_dec_raw(struct xz_dec *s, struct xz_buf *b)
{
#ifdef XZ_DEC_BCJ
	if (s->block_header.bcj_type != XZ_BCJ_NONE)
		return xz_dec_bcj_run(s, b);
#endif

	return xz_dec_lzma2_run(s->lzma2, b);
}

/*
 * Fill s->temp by copying data starting from b->in[b->in_pos]. Caller
 * must have set s->temp.pos to indicate how much data we are supposed
 * to copy into s->temp.buf. Return true once s->temp.pos has reached
 * s->temp.size.
 */
static bool XZ_FUNC fill_temp(struct xz_dec *s, struct xz_buf *b)
{
	size_t copy_size = min_t(size_t,
			b->in_size - b->in_pos, s->temp.size - s->temp.pos);

	memcpy(s->temp.buf + s->temp.pos, b->in + b->in_pos, copy_size);
	b->in_pos += copy_size;
	s->temp.pos += copy_size;

	if (s->temp.pos == s->temp.size) {
		s->temp.pos = 0;
		return true;
	}

	return false;
}

/* Decode a variable-length integer (little-endian base-128 encoding) */
static enum xz_ret XZ_FUNC dec_vli(struct xz_dec *s,
		const uint8_t *in, size_t *in_pos, size_t in_size)
{
	uint8_t byte;

	if (s->pos == 0)
		s->vli = 0;

	while (*in_pos < in_size) {
		byte = in[*in_pos];
		++*in_pos;

		s->vli |= (uint64_t)(byte & 0x7F) << s->pos;

		if ((byte & 0x80) == 0) {
			/* Don't allow non-minimal encodings. */
			if (byte == 0 && s->pos != 0)
				return XZ_DATA_ERROR;

			s->pos = 0;
			return XZ_STREAM_END;
		}

		if (s->pos >= 56)
			return XZ_DATA_ERROR;

		s->pos += 7;
	}

	return XZ_OK;
}

/* Update the Index size and the CRC32 value. */
static void XZ_FUNC index_update(struct xz_dec *s, const struct xz_buf *b)
{
	size_t in_used = b->in_pos - s->in_start;
	s->index.size += in_used;
	s->crc32 = xz_crc32(b->in + s->in_start, in_used, s->crc32);
}

/*
 * Validate that the next four input bytes match the value of s->crc32.
 * s->pos must be zero when starting to validate the first byte.
 */
static enum xz_ret XZ_FUNC crc32_validate(struct xz_dec *s, struct xz_buf *b)
{
	do {
		if (b->in_pos == b->in_size)
			return XZ_OK;

		if (((s->crc32 >> s->pos) & 0xFF) != b->in[b->in_pos++])
			return XZ_DATA_ERROR;

		s->pos += 8;

	} while (s->pos < 32);

	s->crc32 = 0;
	s->pos = 0;

	return XZ_STREAM_END;
}

/* Decode the Stream Header field (the first 12 bytes of the .xz Stream). */
static enum xz_ret XZ_FUNC dec_stream_header(struct xz_dec *s)
{
	if (!memeq(s->temp.buf, HEADER_MAGIC, HEADER_MAGIC_SIZE))
		return XZ_FORMAT_ERROR;

	if (xz_crc32(s->temp.buf + HEADER_MAGIC_SIZE, 2, 0)
			!= read_le32(s->temp.buf + HEADER_MAGIC_SIZE + 2))
		return XZ_DATA_ERROR;

	/*
	 * Decode the Stream Flags field. Of integrity checks, we support
	 * only none (Check ID = 0) and CRC32 (Check ID = 1).
	 */
	if (s->temp.buf[HEADER_MAGIC_SIZE] != 0
			|| s->temp.buf[HEADER_MAGIC_SIZE + 1] > 1)
		return XZ_OPTIONS_ERROR;

	s->has_crc32 = s->temp.buf[HEADER_MAGIC_SIZE + 1];

	return XZ_OK;
}

/* Decode the Stream Footer field (the last 12 bytes of the .xz Stream) */
static enum xz_ret XZ_FUNC dec_stream_footer(struct xz_dec *s)
{
	if (!memeq(s->temp.buf + 10, FOOTER_MAGIC, FOOTER_MAGIC_SIZE))
		return XZ_DATA_ERROR;

	if (xz_crc32(s->temp.buf + 4, 6, 0) != read_le32(s->temp.buf))
		return XZ_DATA_ERROR;

	/*
	 * Validate Backward Size. Note that we never added the size of the
	 * Index CRC32 field to s->index.size, thus we use s->index.size / 4
	 * instead of s->index.size / 4 - 1.
	 */
	if (s->index.size / 4 != read_le32(s->temp.buf + 4))
		return XZ_DATA_ERROR;

	if (s->temp.buf[8] != 0 || s->temp.buf[9] != s->has_crc32)
		return XZ_DATA_ERROR;

	/*
	 * Use XZ_STREAM_END instead of XZ_OK to be more convenient
	 * for the caller.
	 */
	return XZ_STREAM_END;
}

/* Decode the Block Header and initialize the filter chain. */
static enum xz_ret XZ_FUNC dec_block_header(struct xz_dec *s)
{
	enum xz_ret ret;

	/*
	 * Validate the CRC32. We know that the temp buffer is at least
	 * eight bytes so this is safe.
	 */
	s->temp.size -= 4;
	if (xz_crc32(s->temp.buf, s->temp.size, 0)
			!= read_le32(s->temp.buf + s->temp.size))
		return XZ_DATA_ERROR;

	s->temp.pos = 2;

	/*
	 * Catch unsupported Block Flags. We support only one or two filters
	 * in the chain, so we catch that with the same test.
	 */
#ifdef XZ_DEC_BCJ
	if (s->temp.buf[1] & 0x3E)
#else
	if (s->temp.buf[1] & 0x3F)
#endif
		return XZ_OPTIONS_ERROR;

	/* Compressed Size */
	if (s->temp.buf[1] & 0x40) {
		if (dec_vli(s, s->temp.buf, &s->temp.pos, s->temp.size)
					!= XZ_STREAM_END)
			return XZ_DATA_ERROR;

		s->block_header.compressed = s->vli;
	} else {
		s->block_header.compressed = VLI_UNKNOWN;
	}

	/* Uncompressed Size */
	if (s->temp.buf[1] & 0x80) {
		if (dec_vli(s, s->temp.buf, &s->temp.pos, s->temp.size)
				!= XZ_STREAM_END)
			return XZ_DATA_ERROR;

		s->block_header.uncompressed = s->vli;
	} else {
		s->block_header.uncompressed = VLI_UNKNOWN;
	}

#ifdef XZ_DEC_BCJ
	/* If there are two filters, the first one must be a BCJ filter. */
	if (s->temp.buf[1] & 0x01) {
		if (s->temp.size - s->temp.pos < 2)
			return XZ_OPTIONS_ERROR;

		switch (s->temp.buf[s->temp.pos]) {
#ifdef XZ_DEC_BCJ_X86
		case XZ_BCJ_X86:
#endif
#ifdef XZ_DEC_BCJ_POWERPC
		case XZ_BCJ_POWERPC:
#endif
#ifdef XZ_DEC_BCJ_IA64
		case XZ_BCJ_IA64:
#endif
#ifdef XZ_DEC_BCJ_ARM
		case XZ_BCJ_ARM:
#endif
#ifdef XZ_DEC_BCJ_ARMTHUMB
		case XZ_BCJ_ARMTHUMB:
#endif
#ifdef XZ_DEC_BCJ_SPARC
		case XZ_BCJ_SPARC:
#endif
			break;

		default:
			return XZ_OPTIONS_ERROR;
		}

		s->block_header.bcj_type = s->temp.buf[s->temp.pos++];

		/*
		 * We don't support custom start offset,
		 * so Size of Properties must be zero.
		 */
		if (s->temp.buf[s->temp.pos++] != 0x00)
			return XZ_OPTIONS_ERROR;

		xz_dec_bcj_reset(s);
	} else {
		s->bcj.type = XZ_BCJ_NONE;
	}
#endif

	/* Valid Filter Flags always take at least two bytes. */
	if (s->temp.size - s->temp.pos < 2)
		return XZ_DATA_ERROR;

	/* Filter ID = LZMA2 */
	if (s->temp.buf[s->temp.pos++] != 0x21)
		return XZ_OPTIONS_ERROR;

	/* Size of Properties = 1-byte Filter Properties */
	if (s->temp.buf[s->temp.pos++] != 0x01)
		return XZ_OPTIONS_ERROR;

	/* Filter Properties contains LZMA2 dictionary size. */
	if (s->temp.size - s->temp.pos < 1)
		return XZ_DATA_ERROR;

	ret = xz_dec_lzma2_reset(s->lzma2, s->temp.buf[s->temp.pos++]);
	if (ret != XZ_OK)
		return ret;

	/* The rest must be Header Padding. */
	while (s->temp.pos < s->temp.size)
		if (s->temp.buf[s->temp.pos++] != 0x00)
			return XZ_OPTIONS_ERROR;

	s->temp.pos = 0;
	s->block.compressed = 0;
	s->block.uncompressed = 0;

	return XZ_OK;
}

static enum xz_ret XZ_FUNC dec_main(struct xz_dec *s, struct xz_buf *b)
{
	enum xz_ret ret;

	/*
	 * Store the start position for the case when we are in the middle
	 * of the Index field.
	 */
	s->in_start = b->in_pos;

	while (true)
	switch (s->sequence) {
	case SEQ_STREAM_HEADER:
		/*
		 * Stream Header is copied to s->temp, and then decoded
		 * from there. This way if the caller gives us only little
		 * input at a time, we can still keep the Stream Header
		 * decoding code still simple. Similar approach is used
		 * in many places in this file.
		 */
		if (!fill_temp(s, b))
			return XZ_OK;

		ret = dec_stream_header(s);
		if (ret != XZ_OK)
			return ret;

		s->sequence = SEQ_BLOCK_START;

	case SEQ_BLOCK_START:
		/* We need one byte of input to continue. */
		if (b->in_pos == b->in_size)
			return XZ_OK;

		/* See if this is the beginning of the Index field. */
		if (b->in[b->in_pos] == 0) {
			s->in_start = b->in_pos++;
			s->sequence = SEQ_INDEX_COUNT;
			break;
		}

		/*
		 * Calculate the size of the Block Header and prepare
		 * to decode it.
		 */
		s->block_header.size = ((uint32_t)b->in[b->in_pos] + 1) * 4;

		s->temp.size = s->block_header.size;
		s->temp.pos = 0;
		s->sequence = SEQ_BLOCK_HEADER;

	case SEQ_BLOCK_HEADER:
		if (!fill_temp(s,b ))
			return XZ_OK;

		ret = dec_block_header(s);
		if (ret != XZ_OK)
			return ret;

		s->sequence = SEQ_BLOCK_UNCOMPRESS;

	case SEQ_BLOCK_UNCOMPRESS:
		s->in_start = b->in_pos;
		s->out_start = b->out_pos;

		ret = xz_dec_raw(s, b);

		s->block.compressed += b->in_pos - s->in_start;
		s->block.uncompressed += b->out_pos - s->out_start;

		if (s->block.compressed > s->block_header.compressed
				|| s->block.uncompressed
					> s->block_header.uncompressed)
			return XZ_DATA_ERROR;

		if (s->has_crc32)
			s->crc32 = xz_crc32(b->out + s->out_start,
					b->out_pos - s->out_start, s->crc32);

		if (ret != XZ_STREAM_END)
			return ret;

		/*
		 * If sizes were stored in Block Header, they must match
		 * the observed sizes now.
		 */
		if (s->block_header.compressed != VLI_UNKNOWN
				&& s->block_header.compressed
					!= s->block.compressed)
			return XZ_DATA_ERROR;

		if (s->block_header.uncompressed != VLI_UNKNOWN
				&& s->block_header.uncompressed
					!= s->block.uncompressed)
			return XZ_DATA_ERROR;

		/*
		 * Update the hash and count, which are used for
		 * Index validation.
		 */
		s->block.hash.unpadded += s->block_header.size
				+ s->block.compressed;
		if (s->has_crc32)
			s->block.hash.unpadded += 4;

		s->block.hash.uncompressed += s->block.uncompressed;
		s->block.hash.crc32 = xz_crc32(
				(const uint8_t *)&s->block.hash,
				sizeof(s->block.hash), s->block.hash.crc32);

		++s->block.count;
		s->sequence = SEQ_BLOCK_PADDING;

	case SEQ_BLOCK_PADDING:
		/*
		 * Size of Compressed Data + Block Padding must be a
		 * multiple of four. We don't need s->d->block.compressed
		 * for anything else anymore, so we use it here to test the
		 * size of the Block Padding field.
		 */
		while (s->block.compressed & 3) {
			if (b->in_pos == b->in_size)
				return XZ_OK;

			if (b->in[b->in_pos++] != 0)
				return XZ_DATA_ERROR;

			++s->block.compressed;
		}

		s->sequence = SEQ_BLOCK_CHECK;

	case SEQ_BLOCK_CHECK:
		if (s->has_crc32) {
			ret = crc32_validate(s, b);
			if (ret != XZ_STREAM_END)
				return ret;
		}

		s->sequence = SEQ_BLOCK_START;
		break;

	case SEQ_INDEX_COUNT:
	case SEQ_INDEX_UNPADDED:
	case SEQ_INDEX_UNCOMPRESSED:
		do {
			ret = dec_vli(s, b->in, &b->in_pos, b->in_size);
			if (ret != XZ_STREAM_END) {
				index_update(s, b);
				return ret;
			}

			switch (s->sequence) {
			case SEQ_INDEX_COUNT:
				s->index.count = s->vli;

				/*
				 * Validate that the Number of Records field
				 * indicates the same number of Records as
				 * there were Blocks in the Stream.
				 */
				if (s->index.count != s->block.count)
					return XZ_DATA_ERROR;

				s->sequence = SEQ_INDEX_UNPADDED;
				break;

			case SEQ_INDEX_UNPADDED:
				s->index.hash.unpadded += s->vli;
				s->sequence = SEQ_INDEX_UNCOMPRESSED;
				break;

			case SEQ_INDEX_UNCOMPRESSED:
				s->index.hash.uncompressed += s->vli;
				s->index.hash.crc32 = xz_crc32(
					(const uint8_t *)&s->index.hash,
					sizeof(s->index.hash),
					s->index.hash.crc32);
				--s->index.count;
				s->sequence = SEQ_INDEX_UNPADDED;
				break;

			default:
				/* Silence compiler warnings */
				break;
			}
		} while (s->index.count > 0);

		s->sequence = SEQ_INDEX_PADDING;

	case SEQ_INDEX_PADDING:
		while ((s->index.size + (b->in_pos - s->in_start)) & 3) {
			if (b->in_pos == b->in_size) {
				index_update(s, b);
				return XZ_OK;
			}

			if (b->in[b->in_pos++] != 0)
				return XZ_DATA_ERROR;
		}

		/* Finish the CRC32 value and Index size. */
		index_update(s, b);

		/* Compare the hashes to validate the Index field. */
		if (!memeq(&s->block.hash, &s->index.hash,
				sizeof(s->block.hash)))
			return XZ_DATA_ERROR;

		s->sequence = SEQ_INDEX_CRC32;

	case SEQ_INDEX_CRC32:
		ret = crc32_validate(s, b);
		if (ret != XZ_STREAM_END)
			return ret;

		s->temp.size = STREAM_HEADER_SIZE;
		s->sequence = SEQ_STREAM_FOOTER;

	case SEQ_STREAM_FOOTER:
		if (!fill_temp(s, b))
			return XZ_OK;

		return dec_stream_footer(s);
	}

	/* Never reached */
}

/*
 * xz_dec_run() is a wrapper for dec_main() to handle some special cases in
 * multi-call and single-call decoding.
 *
 * In multi-call mode, we must return XZ_BUF_ERROR when it seems clear that we
 * are not going to make any progress anymore. This is to prevent the caller
 * from calling us infinitely when the input file is truncated or otherwise
 * corrupt. Since zlib-style API allows that the caller fills the input buffer
 * only when the decoder doesn't produce any new output, we have to be careful
 * to avoid returning XZ_BUF_ERROR too easily: XZ_BUF_ERROR is returned only
 * after the second consecutive call to xz_dec_run() that makes no progress.
 *
 * In single-call mode, if we couldn't decode everything and no error
 * occurred, either the input is truncated or the output buffer is too small.
 * Since we know that the last input byte never produces any output, we know
 * that if all the input was consumed and decoding wasn't finished, the file
 * must be corrupt. Otherwise the output buffer has to be too small or the
 * file is corrupt in a way that decoding it produces too big output.
 *
 * If single-call decoding fails, we reset b->in_pos and b->out_pos back to
 * their original values. This is because with some filter chains there won't
 * be any valid uncompressed data in the output buffer unless the decoding
 * actually succeeds (that's the price to pay of using the output buffer as
 * the workspace).
 */
enum xz_ret XZ_FUNC xz_dec_run(struct xz_dec *s, struct xz_buf *b)
{
	size_t in_start;
	size_t out_start;
	enum xz_ret ret;

	if (s->single_call)
		xz_dec_reset(s);

	in_start = b->in_pos;
	out_start = b->out_pos;
	ret = dec_main(s, b);

	if (s->single_call) {
		if (ret == XZ_OK)
			ret = b->in_pos == b->in_size
					? XZ_DATA_ERROR : XZ_BUF_ERROR;

		if (ret != XZ_STREAM_END) {
			b->in_pos = in_start;
			b->out_pos = out_start;
		}

	} else if (ret == XZ_OK && in_start == b->in_pos
			&& out_start == b->out_pos) {
		if (s->allow_buf_error)
			ret = XZ_BUF_ERROR;

		s->allow_buf_error = true;
	} else {
		s->allow_buf_error = false;
	}

	return ret;
}

struct xz_dec * XZ_FUNC xz_dec_init(uint32_t dict_max)
{
	struct xz_dec *s = kmalloc(sizeof(*s), GFP_KERNEL);
	if (s == NULL)
		return NULL;

	s->lzma2 = xz_dec_lzma2_create(dict_max);
	if (s->lzma2 == NULL) {
		kfree(s);
		return NULL;
	}

	s->single_call = dict_max == 0;
	xz_dec_reset(s);
	return s;
}

void XZ_FUNC xz_dec_reset(struct xz_dec *s)
{
	if (s != NULL) {
		s->sequence = SEQ_STREAM_HEADER;
		s->allow_buf_error = false;
		s->pos = 0;
		s->crc32 = 0;

		s->block.count = 0;
		memzero(&s->block.hash, sizeof(s->block.hash));

		s->index.size = 0;
		memzero(&s->index.hash, sizeof(s->index.hash));

		s->temp.pos = 0;
		s->temp.size = STREAM_HEADER_SIZE;
	}
}

void XZ_FUNC xz_dec_end(struct xz_dec *s)
{
	if (s != NULL) {
		xz_dec_lzma2_end(s->lzma2);
		kfree(s);
	}
}