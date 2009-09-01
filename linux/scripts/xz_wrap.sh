#!/bin/sh
#
# Wrapper for xz to use the BCJ filter of the target architecture
#
# Author: Lasse Collin <lasse.collin@tukaani.org>
#
# This file has been put into the public domain.
# You can do whatever you want with this file.
#

PB=2
case $ARCH in
	x86|x86_64)     BCJ=--x86 ;;
	powerpc)        BCJ=--powerpc ;;
	ia64)           BCJ=--ia64; PB=4 ;;
	arm)            BCJ=--arm ;;
	sparc)          BCJ=--sparc ;;
	*)              BCJ= ;;
esac

# Big dictionary is OK for the kernel image, but it's not OK for initramfs
# or initrd.
case $1 in
	arch/*)         DICT=16MiB ;;
	*)              DICT=1MiB ;;
esac

# This is very slow, but it should give very good compression too.
xz --stdout --quiet --threads=1 --check=crc32 \
		$BCJ --lzma2=preset=9e,dict=$DICT,pb=$PB "$1"
