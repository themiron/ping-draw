/*
 * ping-draw for https://jinglepings.com/
 * Copyright (c) 2018 Vladislav Grishenko <themiron@mail.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 dated June, 1991, or
 * (at your option) version 3 dated 29 June, 2007.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <err.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/times.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/icmp6.h>
#include <png.h>

#undef offsetof
#ifdef __compiler_offsetof
#define offsetof(TYPE,MEMBER) __compiler_offsetof(TYPE,MEMBER)
#else
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

#define min(a, b) (a < b ? a : b)

#define ADDR "2001:4c08:2028:00:00:00:00:00"
#define MAX_WIDTH 160
#define MAX_HEIGHT 120
#define FPS 20
#define SCAN 2

static int sock;
static png_uint_32 width, height;
static int bit_depth, color_type;

static int ping6(struct in6_addr *addr)
{
	static union {
		struct icmp6_hdr hdr;
		unsigned char pad[20];
	} packet = {
		.hdr = { 
			.icmp6_type = ICMP6_ECHO_REQUEST,
			.icmp6_seq = 1
		}
	};
	struct sockaddr_in6 sin6 = {
		.sin6_family = AF_INET6,
		.sin6_addr = *addr
	};
	int ret;

	packet.hdr.icmp6_id++;

	do {
		ret = sendto(sock, &packet, sizeof(packet), 0, (struct sockaddr *)&sin6, sizeof(sin6));
	} while (ret < 0 && errno == EINTR);

	return ret;
}

static int dec2bcd(int value)
{
	int n, r, bcd = 0;

	for (n = 0; value; n++) {
		r = value % 10;
		bcd += r << (4 * n);
		value /= 10;
	}

	return bcd;
}

static int draw(int x, int y, int r, int g, int b)
{
	struct in6_addr addr;

	if (x >= MAX_WIDTH || y >= MAX_HEIGHT)
		return 0;

	if (r == 0 && g == 0 && b == 0)
		return 0;

	inet_pton(AF_INET6, ADDR, &addr);
	addr.s6_addr16[3] = ntohs(dec2bcd(x));
	addr.s6_addr16[4] = ntohs(dec2bcd(y));
	addr.s6_addr16[5] = ntohs(r);
	addr.s6_addr16[6] = ntohs(g);
	addr.s6_addr16[7] = ntohs(b);

	return ping6(&addr);
}

static png_bytep *read_png(const char *fname)
{
	FILE *fp;
	unsigned char header[8];
	png_structp png_ptr;
	png_infop info_ptr;
	png_bytep *row_pointers;
	int y;

	fp = fopen(fname, "rb");
	if (fp == NULL)
		err(1, "fopen %s", fname);

	if (fread(header, 1, sizeof(header), fp) != sizeof(header) ||
	    png_sig_cmp(header, 0, 8))
		err(1, "%s is not png file", fname);

	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (png_ptr == NULL)
		err(1, "png_create_read_struct error");

	info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == NULL)
		err(1, "png_create_info_struct error");

//	if (setjmp(png_jmpbuf(png_ptr)))
//		err(1, "init");

	png_init_io(png_ptr, fp);
	png_set_sig_bytes(png_ptr, 8);
	png_read_info(png_ptr, info_ptr);
	png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, NULL, NULL, NULL);

	png_read_update_info(png_ptr, info_ptr);

//	if (setjmp(png_jmpbuf(png_ptr)))
//		err(1, "read");

	row_pointers = malloc(sizeof(png_bytep) * height);
	if (row_pointers == NULL)
		err(1, "malloc %ux%u error", (unsigned int)width, (unsigned int)height);
	for (y = 0; y < height; y++) {
		row_pointers[y] = malloc(png_get_rowbytes(png_ptr, info_ptr));
		if (row_pointers[y] == NULL)
			err(1, "malloc %ux%u error", (unsigned int)width, (unsigned int)height);
	}

	png_read_image(png_ptr, row_pointers);
	png_read_end(png_ptr, NULL);

	fclose(fp);

	if (png_get_color_type(png_ptr, info_ptr) != PNG_COLOR_TYPE_RGBA)
		err(1, "png_get_color_type error, not RGBA file");

	return row_pointers;
}

static void usage(char *cmd)
{
	char *slash = strrchr(cmd, '/');
	fprintf(stderr, "Usage: %s [-x <x>] [-y <y>] [-f <fps>] <file.png>\n", slash ? ++slash : cmd);
	exit(1);
}

int main(int argc, char *argv[])
{
	png_bytep *row_pointers;
	png_byte *row, *ptr;
	int csum, x, y, scan, c;
	int offset_x, offset_y, fps;
	struct tms dummy;
	long int tps;
	clock_t time;

	offset_x = offset_y = 0;
	fps = FPS;
	while ((c = getopt(argc, argv, "x:y:f:h")) != -1) {
		switch (c) {
		case 'x':
			offset_x = atoi(optarg);
			break;
		case 'y':
			offset_y = atoi(optarg);
			break;
		case 'f':
			fps = atoi(optarg);
			break;
		case 'h':
		case '?':
		default:
			usage(argv[0]);
		}
	}

	if (argv[optind] == NULL)
		usage(argv[0]);
	
	sock = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
	if (sock < 0)
		err(1, "socket error");

	csum = offsetof(struct icmp6_hdr, icmp6_cksum);
	if (setsockopt(sock, SOL_RAW, IPV6_CHECKSUM, &csum, sizeof(csum)) < 0)
		err(1, "setsockopt error");

	row_pointers = read_png(argv[optind]);
	width = min(offset_x + width, MAX_WIDTH) - offset_x;
	height = min(offset_y + height, MAX_HEIGHT) - offset_y;

	printf("draw %ux%u at %d,%d\n", (unsigned int)width, (unsigned int)height, offset_x, offset_y);

	scan = 0;
	tps = sysconf(_SC_CLK_TCK);
	while (1) {
		time = times(&dummy);
		for (y = scan; y < height; y += SCAN) {
			row = row_pointers[y];
			for (x = 0; x < width; x += 1) {
				ptr = &row[x*4];
				draw(offset_x + x, offset_y + y, ptr[0], ptr[1], ptr[2]);
			}
		}
		time = times(&dummy) - time;
		if (time >= 0 && time < tps/(fps*SCAN)) {
			int delay = 1000000 * (tps/(fps*SCAN) - time) / tps;
			usleep(delay);
		}
		if (++scan >= SCAN)
			scan = 0;
	}

	return 0;
}
