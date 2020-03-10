/*
* SPI NEOPixel 16x16 RGB LED display utility (using spidev driver)
* By R. Blansett
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
static const char *file = NULL;
static uint8_t mode;
static uint8_t bits = 8;
static uint32_t speed = 8000000;
static uint16_t delay;

// Space for 16x16 24-bit (8-bits per color) LEDs
// My set up uses each SPI byte to encode 2 bits of the LED data.
// Append REFRESH (zeros) to cause the freshly written data to be latched.
// Datasheet says it should be at least 280 us / at 8Mbs, that's 1 us per byte.
// Hence 280 additional bytes.

static const uint16_t GRID_WIDTH = 16;
static const uint16_t GRID_HEIGHT = 16;
static const uint16_t REFRESH_SIZE = 280;
static const uint16_t BITS_PER_SPI_BYTE = 2;
static const uint16_t SPI_BYTES_PER_BYTE = 4;

static const uint16_t txBuffer_SIZE = GRID_WIDTH * GRID_HEIGHT * 3 + 280;
static uint8_t txBuffer[txBuffer_SIZE] = {0, }; 
static uint8_t rxBuffer[sizeof(txBuffer)] = {0, };

struct rgbPixel_t
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a; // alpha (make it an even number)
} rgbGrid[GRID_WIDTH * GRID_HEIGHT];


struct spiRgbPixel_t
{
    // My scheme requires SPI 4 bytes to make 8 bits:
    // IMPORTANT: In the LED, Green is first 8 bits, so it's GRB
    // IMPORTANT: In the 16x16 LED panel, the even rows are order-reversed.
    uint8_t g[SPI_BYTES_PER_BYTE];
    uint8_t r[SPI_BYTES_PER_BYTE];
    uint8_t b[SPI_BYTES_PER_BYTE];
} spiGrid[GRID_WIDTH * GRID_HEIGHT];

static void spiGridClear()
{
    static const uint16_t GRID_END = sizeof(spiGrid);
    uint8_t * p2bits = &txBuffer[0];

    for (int i = 0; i < GRID_END; i++)
    {
        *p2bits++ = _0_0;
    }
    for (int i = 0; i < REFRESH_SIZE; i++)
    {
        *p2bits++ = REFRESH;
    }
}

// NOTE: You have to pass in the spiPixel for this to fill and return.
static struct spiRgbPixel_t& 
    makeSpiPixel(struct spiRgbPixel_t& spiPixel, struct rgbPixel_t& rgb)
{
    uint8_t mapBits [4] = {
        _0_0,	// 10001000 - represents 00
        _0_1, 	// 10001100 - represents 01
        _1_0, 	// 11001000 - represents 10
        _1_1, 	// 11001100 - represents 11
    };

    for (uint16_t bytePos = SPI_BYTES_PER_BYTE-1; bytePos >= 0; bytePos--)
    {
        // Needs 1 SPI byte per 2 bits of LED color data.
        spiPixel.r[bytePos] = mapBits[rgb.r & 0x03];
        rgb.r >>= 2;

        spiPixel.g[bytePos] = mapBits[rgb.g & 0x03];
        rgb.g >>= 2;

        spiPixel.b[bytePos] = mapBits[rgb.b & 0x03];
        rgb.b >>= 2;
    }

    return spiPixel;
}

static struct rgbPixel_t&
    makeRgbPixel(struct rgbPixel_t& pixel, uint8_t r, uint8_t g, uint8_t b)
{
    pixel.r = r;
    pixel.g = g;
    pixel.b = b;
    pixel.a = 0;

    return pixel;
}

static void rgbGridClear()
{
    static const uint16_t GRID_AREA = GRID_HEIGHT * GRID_WIDTH;
    struct rgbPixel_t * pPixel = &rgbGrid[0];

    struct rgbPixel_t black = makeRgbPixel(black, 0, 0, 0);

    for (int i = 0; i < GRID_AREA; i++)
    {
        *pPixel++ = black;
    }
}

static void rgbGridPattern(int pattern)
{
    switch(pattern)
    {
        case 0:
            // Fall through to default case:
        default:
            for (int x = 0; x < GRID_WIDTH; x++)
            {
                int row = x;
                int col = x;
                struct rgbPixel_t color;

                // Make a increasingly brighter BLUE:
                color = makeRgbPixel(color, 0, 0, 2*x);
                rgbGrid[row * GRID_WIDTH + col] = color;
            }
            break;
    }
}

static void copySpiGridBytes()
{
    static const uint16_t GRID_END = sizeof(spiGrid);
    static const uint16_t TBUF_END = sizeof(txBuffer);
    uint8_t * pBuf = &txBuffer[0];
    uint8_t * pSpiGrid = (uint8_t*)(&spiGrid[0]);

    // Note: Could use memcopy here.
    for (int i = 0; i < GRID_END; i++)
    {
        *pBuf++ = *pSpiGrid++;
    }
}

static void gridConvertBits()
{
    const int MAX_COL = GRID_WIDTH - 1;
    for (int row = 0; row < GRID_HEIGHT; row++)
    {
        for (int col = 0; col < GRID_WIDTH; col++)
        {
            struct spiRgbPixel_t spiPixel
                = makeSpiPixel(spiPixel, rgbGrid[row * GRID_WIDTH + col]);

            if (row & 1)
            {
                spiGrid[row * GRID_WIDTH + col] = spiPixel;
            }
            else
            {
                spiGrid[row * GRID_WIDTH + MAX_COL - col] = spiPixel;
            }
        }
    }
}

static void gridTransfer(int fd)
{
    int ret;

    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)txBuffer,
        .rx_buf = (unsigned long)rxBuffer,
        .len = sizeof(txBuffer),
        .speed_hz = speed,
        .delay_usecs = delay,
        .bits_per_word = bits,
    };

    // Convert the RGB grid to the SPI RGB grid.
    gridConvertBits();

    // Copy to SPI Transmit buffer:
    // (The REFRESH part of the txBuffer remains unmodified.)
    copySpiGridBytes();

    // SEND IT OUT:
    ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
    if (ret < 1)
        pabort("can't send spi message");

#if 0
    // LED is one-way device.
    for (ret = 0; ret < sizeof(tx); ret++) {
        if (!(ret % 16))
            puts("");
        printf("%.2X ", rx[ret]);
    }
    puts("");
#endif
}

static void print_usage(const char *prog)
{
    printf("Usage: %s [-Dsdf]\n", prog);
    puts("  -D --device   device to use (default /dev/spidev0.0)\n"
         "  -s --speed    max speed (Hz)\n"
         "  -d --delay    delay (use)\n"
         "  -f --file     file (BMP image to load)\n"
    );
    exit(1);
}

static void parse_opts(int argc, char *argv[])
{
    while (1) {
        static const struct option lopts[] = {
            { "device",  1, 0, 'D' },
            { "speed",   1, 0, 's' },
            { "delay",   1, 0, 'd' },
            { "file",    1, 0, 'f' },
            { NULL, 0, 0, 0 },
        };
        int c;

        c = getopt_long(argc, argv, "D:s:d:f", lopts, NULL);

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
        case 'f':
            file = optarg;
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

    // 1) Clear the RGB grid
    rgbGridClear();

    // 2) Plot a pattern to the RGB grid
    if (file == NULL)
    {
        printf("No image file selected. Using default pattern.\n");
        //rgbGridPattern(0);
    }
    else
    {
        printf("image file: %s\n", file);
    }

    // 3) Transfer the grid data out to the real RGB LED Grid.
    gridTransfer(fd);

    close(fd);

    return ret;
}
