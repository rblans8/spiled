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
static uint16_t pattern = 0;

static const uint16_t GRID_WIDTH = 16;
static const uint16_t GRID_HEIGHT = 16;
static const uint16_t REFRESH_SIZE = 280;
static const uint16_t BITS_PER_SPI_BYTE = 2;
static const uint16_t SPI_BYTES_PER_BYTE = 4;

// Space for 16x16 24-bit (8-bits per color) LEDs
// My set up uses each SPI byte to encode 2 bits of the LED data.
// Append REFRESH (zeros) to cause the freshly written data to be latched.
// Datasheet says it should be at least 280 us / at 8Mbs, that's 1 us per byte.
// Hence 280 additional bytes.

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
static spiRgbPixel_t& 
    makeSpiPixel(spiRgbPixel_t& spiPixel, const rgbPixel_t& rgb)
{
    uint8_t mapBits [4] = {
        _0_0,	// 10001000 - represents 00
        _0_1, 	// 10001100 - represents 01
        _1_0, 	// 11001000 - represents 10
        _1_1, 	// 11001100 - represents 11
    };

    // Get copies because shifting zeros the byte.
    uint8_t r = rgb.r;
    uint8_t g = rgb.g;
    uint8_t b = rgb.b;
    for (int8_t bytePos = SPI_BYTES_PER_BYTE-1; bytePos >= 0; bytePos--)
    {
        // Needs 1 SPI byte per 2 bits of LED color data.
        spiPixel.r[bytePos] = mapBits[r & 0x03];
        r >>= 2;

        spiPixel.g[bytePos] = mapBits[g & 0x03];
        g >>= 2;

        spiPixel.b[bytePos] = mapBits[b & 0x03];
        b >>= 2;
    }

    return spiPixel;
}

static rgbPixel_t&
    makeRgbPixel(rgbPixel_t& pixel, uint8_t r, uint8_t g, uint8_t b)
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
    rgbPixel_t * pPixel = &rgbGrid[0];

    rgbPixel_t black = makeRgbPixel(black, 0, 0, 0);

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
            for (int x = 0; x < GRID_WIDTH; x++)
            {
                // Make a increasingly brighter red on top row:
                int row = 0;
                int col = x;
                rgbPixel_t color = makeRgbPixel(color, 4*x + 1, 0, 0);
                rgbGrid[row * GRID_WIDTH + col] = color;
            }
            break;
            
        case 1:
            for (int x = 0; x < GRID_WIDTH; x++)
            {
                // Make a increasingly brighter green on 2nd row:
                int row = 1;
                int col = x;
                rgbPixel_t color = makeRgbPixel(color, 0, 4*x + 1, 0);
                rgbGrid[row * GRID_WIDTH + col] = color;
            }
            break;
            
        case 2:
            for (int x = 0; x < GRID_WIDTH; x++)
            {
                // Make a increasingly brighter green on 3rd row:
                int row = 2;
                int col = x;
                rgbPixel_t color = makeRgbPixel(color, 0, 4*x + 1, 0);
                rgbGrid[row * GRID_WIDTH + col] = color;
            }
            break;
            
        case 3:
            for (int x = 0; x < GRID_WIDTH; x++)
            {
                // Make a increasingly brighter green on 4th row:
                int row = 3;
                int col = x;
                rgbPixel_t color = makeRgbPixel(color, 0, 4*x + 1, 0);
                rgbGrid[row * GRID_WIDTH + col] = color;
            }
            break;
            
        case 4:
            for (int x = 0; x < GRID_WIDTH; x++)
            {
                // Make a increasingly brighter green on 5th row:
                int row = 4;
                int col = x;
                rgbPixel_t color = makeRgbPixel(color, 0, 4*x + 1, 0);
                rgbGrid[row * GRID_WIDTH + col] = color;
            }
            break;
            
        case 5:
            for (int x = 0; x < GRID_WIDTH; x++)
            {
                // Make a increasingly brighter green on 6th row:
                int row = 5;
                int col = x;
                rgbPixel_t color = makeRgbPixel(color, 0, 4*x + 1, 0);
                rgbGrid[row * GRID_WIDTH + col] = color;
            }
            break;
            
        case 6:
            for (int x = 0; x < GRID_WIDTH; x++)
            {
                // Make a increasingly brighter green on 7th row:
                int row = 6;
                int col = x;
                rgbPixel_t color = makeRgbPixel(color, 0, 4*x + 1, 0);
                rgbGrid[row * GRID_WIDTH + col] = color;
            }
            break;
            
        case 7:
            for (int x = 0; x < GRID_WIDTH; x++)
            {
                // Make a increasingly brighter green on 8th row:
                int row = 7;
                int col = x;
                rgbPixel_t color = makeRgbPixel(color, 0, 4*x + 1, 0);
                rgbGrid[row * GRID_WIDTH + col] = color;
            }
            break;
            
        case 15:
            for (int x = 0; x < GRID_WIDTH; x++)
            {
                // Make a increasingly brighter green on bottom row:
                int row = GRID_HEIGHT-1;
                int col = x;
                rgbPixel_t color = makeRgbPixel(color, 0, 4*x + 1, 0);
                rgbGrid[row * GRID_WIDTH + col] = color;
            }
            break;

        case -1:
        default:
            printf("UNKNOWN pattern: %d.  Using default: Blue diag.\n");
            for (int x = 0; x < GRID_WIDTH; x++)
            {
                // Make a increasingly brighter BLUE 
                // on diagonal from upper left to lower right:
                int row = x;
                int col = x;
                rgbPixel_t color = makeRgbPixel(color, 0, 0, 4*x + 1);
                rgbGrid[row * GRID_WIDTH + col] = color;
            }
            break;
    }
}

static void copySpiGridBytes()
{
    static const uint16_t GRID_END = sizeof(spiGrid);
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
//        printf("Row: %d\n", row);
        for (int col = 0; col < GRID_WIDTH; col++)
        {
//            printf("Making spiPixel # %d\n", row*GRID_WIDTH+col);
            spiRgbPixel_t spiPixel;
                spiPixel = makeSpiPixel(spiPixel, rgbGrid[row * GRID_WIDTH + col]);

            if (row & 1)
            {
                spiGrid[row * GRID_WIDTH + col] = spiPixel;
            }
            else
            {
                spiGrid[row * GRID_WIDTH - col + MAX_COL] = spiPixel;
            }
        }
    }
}

static void dumpSpiGrid()
{
    printf("Dumping SPI RGB Grid values:\n");
    
    const int MAX_COL = GRID_WIDTH - 1;
    for (int row = 0; row < GRID_HEIGHT; row++)
    {
        printf("Row: %d\n", row);
        for (int col = 0; col < GRID_WIDTH; col++)
        {
            spiRgbPixel_t pixel;
            
            if (row & 1)
            {
                pixel = spiGrid[row * GRID_WIDTH + col];
            }
            else
            {
                pixel = spiGrid[row * GRID_WIDTH - col + MAX_COL];
            }
            
            printf("%02X:%02X:%02X:%02X ", pixel.r[0], pixel.r[1], pixel.r[2], pixel.r[3] );
            printf("%02X:%02X:%02X:%02X ", pixel.g[0], pixel.g[1], pixel.g[2], pixel.g[3] );
            printf("%02X:%02X:%02X:%02X ", pixel.b[0], pixel.b[1], pixel.b[2], pixel.b[3] );
        }
        printf("\n");
    }
    printf("\n");
}

static void dumpRgbGrid()
{
    printf("Dumping RGB Grid values:\n");
    for (int row = 0; row < GRID_HEIGHT; row++)
    {
        printf("Row: %d\n", row);
        for (int col = 0; col < GRID_WIDTH; col++)
        {
            rgbPixel_t pixel = rgbGrid[row * GRID_WIDTH + col];
            printf("%02X:%02X:%02X ", pixel.r, pixel.g, pixel.b);
        }
        printf("\n");
    }
    printf("\n");
}

static void dumpTxBuffer()
{
    printf("Dumping SPI TX Buffer values:\n");
    uint8_t* pByte = &txBuffer[0];
    
    for (int row = 0; row < GRID_HEIGHT; row++)
    {
        printf("Row: %d\n", row);
        // (4 bytes per color) * (3 colors per pixel)
        for (int col = 0; col < GRID_WIDTH*SPI_BYTES_PER_BYTE*3; col += 4)
        {
            printf("%02X:%02X:%02X:%02X ", pByte[0], pByte[1], pByte[2], pByte[3]);
            pByte += 4;
        }
        printf("\n");
    }
    printf("\n");
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
         "  -p --pattern  pattern# to display\n"
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
            { "pattern", 1, 0, 'p' },
            { "file",    1, 0, 'f' },
            { NULL, 0, 0, 0 },
        };
        int c;

        c = getopt_long(argc, argv, "D:s:d:p:f", lopts, NULL);

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
        case 'p':
            pattern = atoi(optarg);
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

    // 1) Clear the RGB grid once.
    rgbGridClear();
    spiGridClear();

    // 2) Plot a pattern to the RGB grid
    if (file == NULL)
    {
        printf("No image file selected. Using pattern: %d\n", pattern);
        rgbGridPattern(pattern);
    }
    else
    {
        printf("image file: %s\n", file);
    }

    // 3) Transfer the grid data out to the real RGB LED Grid.
    gridTransfer(fd);
    
    // 4) Get some DEBUG OUT
    dumpRgbGrid();
    dumpSpiGrid();
    dumpTxBuffer();
    
    close(fd);

    return ret;
}
