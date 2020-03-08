/*
 * SPI NEOPixel 16x16 RGB LED display utility (using spidev driver)
 * By Rob Blansett
 *
 * Initially derived from: 
 * SPI testing utility (using spidev driver)
 * Copyright (c) 2007  MontaVista Software, Inc.
 * Copyright (c) 2007  Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 */

#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define REFRESH 0x00	// 00000000 - represents "RESET"
#define _0_0	0x88	// 10001000 - represents 00
#define _0_1 	0x8C	// 10001100 - represents 01
#define _1_0 	0xC8	// 11001000 - represents 10
#define _1_1 	0xCC	// 11001100 - represents 11


static void pabort(const char *s)
{
	perror(s);
	abort();
}

static const char *device = "/dev/spidev0.0";
static uint8_t mode;
static uint8_t bits = 8;
static uint32_t speed = 8000000;
static uint16_t delay;

// Space for 16x16 24-bit (8-bits per color) LEDs
// My set up uses each SPI byte to encode 2 bits of the LED data.
// Append REFRESH (zeros) to cause the freshly written data to be latched.
// Datasheet says it should be at least 280 us / at 8Mbs, that's 1 us per byte.
// Hence 280 additional bytes.

static const uint8_t GRID_WIDTH = 16;
static const uint8_t GRID_HEIGHT = 16;
static const uint8_t REFRESH_WAIT = 280;
static const uint8_t BITS_PER_SPI_BYTE = 2;
static const uint8_t BYTES_PER_LED_BIT = 4;

uint8_t spiGrid[GRID_WIDTH * GRID_HEIGHT * 3 + 280]; 
uint8_t spiRbuf[ARRAY_SIZE(grid)];

struct rgbPixel 
{
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a; // alpha (make it an even number)
} rgbGrid[GRID_WIDTH * GRID_HEIGHT];


struct spiRgbPixel
{
	// NOTE: In the LED, Green is first 8 bits, so it's GRB
	// My scheme requires 4 bytes to make 8 bits:
	uint8_t g[BYTES_PER_LED_BIT];
	uint8_t r[BYTES_PER_LED_BIT];
	uint8_t b[BYTES_PER_LED_BIT];
};

static void makeSpiPixel(spiRgbPixel& pixel, uint8_t r, uint8_t g, uint8_t b)
{
	uint8_t mapBits [4] = {
		_0_0,	// 10001000 - represents 00
		_0_1, 	// 10001100 - represents 01
		_1_0, 	// 11001000 - represents 10
		_1_1, 	// 11001100 - represents 11
	};
	
	for (uint8_t bytePos = BYTES_PER_LED_BIT-1; bytePos > 0; bytePos--)
	{
		p.r[bytePos] = mapBits[g & 0x03];
		r >>= 2;
	
		p.g[bytePos] = mapBits[g & 0x03];
		g >>= 2;
	
		p.b[bytePos] = mapBits[g & 0x03];
		b >>= 2;	
	}
}

static void gridClear()
{
	static const uint16_t ROW_MAX = GRID_HEIGHT;
	static const uint16_t COL_MAX = GRID_WIDTH/BITS_PER_SPI_BYTE;
	static const uint16_t REFRESH_START = ROW_MAX * COL_MAX;
	uint8_t * p2bits = &grid[0];
	
	for (int i = 0; i < REFRESH_START; i++)
	{
		*p2bits++ = _0_0;
	}
	for (int i = 0; i < REFRESH_WAIT; i++)
	{
		*p2bits++ = REFRESH;
	}
}

static void gridPaint()
{
	static const uint16_t REFRESH_START = GRID_HEIGHT * GRID_WIDTH;
	uint8_t * p2bits = &grid[0];
	
	for (int row = 0; row < GRID_HEIGHT; row++)
	{
		for (int col = 0; col < GRID_WIDTH; col++)
		{
			*p2bits++ = _0_0;
			grid[row * COL_MAX + col] = _0_0;
		}
	}
	for (int i = 0; i < REFRESH_WAIT; i++)
	{
		grid[REFRESH_START + i] = REFRESH;
	}
}


static void gridTransfer(int fd)
{
	int ret;

	struct spi_ioc_transfer tr = {
		.tx_buf = (unsigned long)grid,
		.rx_buf = (unsigned long)rbuf,
		.len = ARRAY_SIZE(grid),
		.delay_usecs = delay,
		.speed_hz = speed,
		.bits_per_word = bits,
	};

	ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
	if (ret < 1)
		pabort("can't send spi message");

#if 0
    // LED is one-way device.
	for (ret = 0; ret < ARRAY_SIZE(tx); ret++) {
		if (!(ret % 16))
			puts("");
		printf("%.2X ", rx[ret]);
	}
	puts("");
#endif	
}

static void print_usage(const char *prog)
{
	printf("Usage: %s [-DsbdlHOLC3]\n", prog);
	puts("  -D --device   device to use (default /dev/spidev1.1)\n"
	     "  -s --speed    max speed (Hz)\n"
	     "  -d --delay    delay (usec)\n"
	     "  -b --bpw      bits per word \n"
	     "  -l --loop     loopback\n"
	     "  -H --cpha     clock phase\n"
	     "  -O --cpol     clock polarity\n"
	     "  -L --lsb      least significant bit first\n"
	     "  -C --cs-high  chip select active high\n"
	     "  -3 --3wire    SI/SO signals shared\n");
	exit(1);
}

static void parse_opts(int argc, char *argv[])
{
	while (1) {
		static const struct option lopts[] = {
			{ "device",  1, 0, 'D' },
			{ "speed",   1, 0, 's' },
			{ "delay",   1, 0, 'd' },
			{ "bpw",     1, 0, 'b' },
			{ "loop",    0, 0, 'l' },
			{ "cpha",    0, 0, 'H' },
			{ "cpol",    0, 0, 'O' },
			{ "lsb",     0, 0, 'L' },
			{ "cs-high", 0, 0, 'C' },
			{ "3wire",   0, 0, '3' },
			{ "no-cs",   0, 0, 'N' },
			{ "ready",   0, 0, 'R' },
			{ NULL, 0, 0, 0 },
		};
		int c;

		c = getopt_long(argc, argv, "D:s:d:b:lHOLC3NR", lopts, NULL);

		if (c == -1)
			break;

		switch (c) {
		case 'D':
			device = optarg;
			break;
		case 's':
			speed = atoi(optarg);
			break;
		case 'd':
			delay = atoi(optarg);
			break;
		case 'b':
			bits = atoi(optarg);
			break;
		case 'l':
			mode |= SPI_LOOP;
			break;
		case 'H':
			mode |= SPI_CPHA;
			break;
		case 'O':
			mode |= SPI_CPOL;
			break;
		case 'L':
			mode |= SPI_LSB_FIRST;
			break;
		case 'C':
			mode |= SPI_CS_HIGH;
			break;
		case '3':
			mode |= SPI_3WIRE;
			break;
		case 'N':
			mode |= SPI_NO_CS;
			break;
		case 'R':
			mode |= SPI_READY;
			break;
		default:
			print_usage(argv[0]);
			break;
		}
	}
}

int main(int argc, char *argv[])
{
	int ret = 0;
	int fd;

	parse_opts(argc, argv);

	fd = open(device, O_RDWR);
	if (fd < 0)
		pabort("can't open device");

	/*
	 * spi mode
	 */
	ret = ioctl(fd, SPI_IOC_WR_MODE, &mode);
	if (ret == -1)
		pabort("can't set spi mode");

	ret = ioctl(fd, SPI_IOC_RD_MODE, &mode);
	if (ret == -1)
		pabort("can't get spi mode");

	/*
	 * bits per word
	 */
	ret = ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
	if (ret == -1)
		pabort("can't set bits per word");

	ret = ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &bits);
	if (ret == -1)
		pabort("can't get bits per word");

	/*
	 * max speed hz
	 */
	ret = ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
	if (ret == -1)
		pabort("can't set max speed hz");

	ret = ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed);
	if (ret == -1)
		pabort("can't get max speed hz");

	printf("spi mode: %d\n", mode);
	printf("bits per word: %d\n", bits);
	printf("max speed: %d Hz (%d KHz)\n", speed, speed/1000);

	gridClear();
	gridTransfer(fd);

	close(fd);

	return ret;
}
