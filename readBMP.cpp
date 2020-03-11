#include <stdio.h>
#include <stdlib.h>

struct bmp24_t
{
    int m_width;
    int m_height;
    unsigned char *m_data;

    bmp24_t():
      m_width(0),
      m_height(0),
      m_data(NULL) 
      { /* do nothing else */ }

    bmp24_t(int width, int height, unsigned char *data):
      m_width(width),
      m_height(height),
      m_data(data) 
      { /* do nothing else */ }
    
    ~bmp24_t()
    {
        if (m_data != NULL)
        {
            delete[] m_data;
            m_data = NULL;
            m_width = 0;
            m_height = 0;
        }
    }
};

bmp24_t* readBMP(char* filename)
{
    bmp24_t* pBmp = NULL;
    int i;
    FILE* f = fopen(filename, "rb");
    if (f)
    {
        unsigned char info[54];
        fread(info, sizeof(unsigned char), 54, f); // read the 54-byte header

        // extract image height and width from header
        int width = *(int*)&info[18];
        int height = *(int*)&info[22];

        int size = 3 * width * height;
        unsigned char* data = new unsigned char[size]; // allocate 3 bytes per pixel
        fread(data, sizeof(unsigned char), size, f); // read the rest of the data at once
        fclose(f);

        for(i = 0; i < size; i += 3)
        {
            // Swap the Red & Blue color positions:
            unsigned char tmp = data[i];
            data[i] = data[i+2];
            data[i+2] = tmp;
        }

        pBmp = new bmp24_t(width, height, data);
    }
    else
    {
        pBmp = new bmp24_t();
    }
    
    return pBmp;
}

int main(int argc, char * argv[])
{
    char * bmpFile = NULL;

    if (argc > 1)
    {
        printf("Arg 1 is: %s\n", argv[1]);
        bmpFile = argv[1];
    }
    else
    {
        printf("Missing required arg: filename of BMP file to read\n");
        exit(1);
    }

    bmp24_t * pBmp = readBMP(bmpFile);
    if (pBmp == NULL)
    {
        printf("Failed to read BMP file: %s\n", bmpFile);
        exit(2);
    }

    printf("Read bmp: width=%d, height=%d\n", pBmp->m_width, pBmp->m_height);
    
    unsigned char * pData = pBmp->m_data;
    for (int y = 0; y < pBmp->m_height; y++)
    {
        for (int x = 0; x < pBmp->m_width; x++)
        {
            printf("%02X:%02X:%02X ", pData[0], pData[1], pData[2]);
            pData += 3;
        }
        printf("\n");
    }
    printf("\n");

    return 0;
}