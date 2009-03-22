#
# Makefile
#
# Author: Lasse Collin <lasse.collin@tukaani.org>
#
# This file has been put into the public domain.
# You can do whatever you want with this file.
#

CC = gcc -std=gnu89
CFLAGS = -ggdb3 -O2 -pedantic -Wall -Wextra
RM = rm -f
VPATH = ../linux/include/linux ../linux/lib/xz
COMMON_OBJS = xz_crc32.o xz_dec_stream.o xz_dec_lzma2.o
XZMINIDEC_OBJS = xzminidec.o
BUFTEST_OBJS = buftest.o
BOOTTEST_OBJS = boottest.o
XZ_HEADERS = xz.h xz_private.h xz_stream.h xz_lzma2.h
PROGRAMS = xzminidec buftest boottest

all: $(PROGRAMS)

%.o: %.c $(XZ_HEADERS)
	$(CC) -I../linux/include/linux $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

xzminidec: $(COMMON_OBJS) $(XZMINIDEC_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(COMMON_OBJS) $(XZMINIDEC_OBJS)

buftest: $(COMMON_OBJS) $(BUFTEST_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(COMMON_OBJS) $(BUFTEST_OBJS)

boottest: $(BOOTTEST_OBJS) xz_crc32.c xz_dec_stream.c xz_dec_lzma2.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(BOOTTEST_OBJS)

.PHONY: clean
clean:
	-$(RM) $(COMMON_OBJS) $(XZMINIDEC_OBJS) $(BUFTEST_OBJS) \
		$(BOOTTEST_OBJS) $(PROGRAMS)
