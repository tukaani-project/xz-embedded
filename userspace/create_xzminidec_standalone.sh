#!/bin/sh
# SPDX-License-Identifier: 0BSD

#############################################################################
#
# Create a single C source file version of xzminidec with all optional
# features enabled:
#
#     sh create_xzminidec_standalone.sh > xzminidec_standalone.c
#
# Author: Lasse Collin <lasse.collin@tukaani.org>
#
#############################################################################

set -e

# Split the SPDX string with extra quotes so that it doesn't accidentally
# match when this script is grepped.
SPDX_LI='SPDX''-License-Identifier'': 0BSD'

# This is to make the headings stand out better in the combined file.
STARS='**********************************************************************'

# Include the committer date (not author date) of the latest commit.
TIMESTAMP=$(git log -n 1 --pretty='%ci')
COMMIT=$(git log -n 1 --pretty='%H')

cat << EOF
/* $SPDX_LI */

/*
 * xzminidec_standalone.c - xzminidec combined into a single C source file
 *
 * Created from xz-embedded.git:
 * commit $COMMIT
 * CommitDate: $TIMESTAMP
 *
 * Build instructions:
 *
 *     cc -O2 -o xzminidec xzminidec_standalone.c
 *
 * Compressed .xz file is read from standard input and decompressed data
 * is written to standard output. Examples:
 *
 *     ./xzminidec < input.xz > output
 *     ./xzminidec < foo.tar.xz | tar xf -
 */

/* Enable support for concatenated .xz files. */
#define XZ_DEC_CONCATENATED

/* Enable all supported integrity check types. CRC32 is always enabled. */
#define XZ_USE_CRC64
#define XZ_USE_SHA256
#define XZ_DEC_ANY_CHECK

/* Enable all BCJ filters. */
#define XZ_DEC_X86
#define XZ_DEC_ARM
#define XZ_DEC_ARMTHUMB
#define XZ_DEC_ARM64
#define XZ_DEC_RISCV
#define XZ_DEC_POWERPC
#define XZ_DEC_IA64
#define XZ_DEC_SPARC
EOF

# Combine the files. The order of xz.h, xz_config.h, and xz_private.h matters
# and they must be before the other files.
for FILE in xz.h xz_config.h xz_private.h xz_stream.h xz_lzma2.h \
		xz_crc32.c xz_crc64.c xz_sha256.c \
		xz_dec_stream.c xz_dec_lzma2.c xz_dec_bcj.c \
		xzminidec.c
do
	# The files are from three different directories.
	for DIR in . ../linux/lib/xz ../linux/include/linux ERROR
	do
		if test "x$DIR" = "xERROR"
		then
			echo "File not found: $FILE" >&2
			exit 1
		fi

		if test -f "$DIR/$FILE"
		then
			printf '\n\n/*%s\n * %s\n %s*/\n' \
					"$STARS" "$FILE" "$STARS"

			# Omit the SPDX license tags. We already
			# added one at the top.
			#
			# Omit the #include directives that refer to
			# our own headers.
			grep -v -e "$SPDX_LI" -e '#.*include "' "$DIR/$FILE"
			break
		fi
	done
done
