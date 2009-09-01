#!/bin/sh
#
# This is a wrapper for xz to use appropriate compression options depending
# on what is being compressed. The only argument to this script should be
# "kernel" or "misc" to indicate what is being compressed.
#
# Author: Lasse Collin <lasse.collin@tukaani.org>
#
# This file has been put into the public domain.
# You can do whatever you want with this file.
#

# Defaults: No BCJ filter and no extra LZMA2 options.
BCJ=
LZMA2OPTS=

# Big dictionary is OK for the kernel image, but it's not OK
# for other things.
#
# BCJ filter is used only for the kernel, at least for now.
# It could be useful for non-trivial initramfs too, but it
# depends on the exact content of the initramfs image.
case $1 in
	kernel)
		DICT=16MiB
		case $ARCH in
			x86|x86_64)     BCJ=--x86 ;;
			powerpc)        BCJ=--powerpc ;;
			ia64)           BCJ=--ia64; LZMA2OPTS=pb=4 ;;
			arm)            BCJ=--arm ;;
			sparc)          BCJ=--sparc ;;
		esac
		;;
	misc)
		DICT=1MiB
		;;
	*)
		echo "xz_wrap.sh: Invalid argument \`$1'" >&2
		exit 1
		;;
esac

# This is very slow, but it should give very good compression too.
exec xz --stdout --quiet --threads=1 --check=crc32 \
		$BCJ --lzma2=preset=6e,$LZMA2OPTS,dict=$DICT
