#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void initFreq(unsigned int *freq)
{
    for (int i = 0; i < 256; i++)
    {
        freq[i] = 1;
    }
}

typedef struct
{
    unsigned char byteCache;
    unsigned int remainingBits;
    unsigned char *input;
    unsigned int inputPos;
    unsigned int maxSize;
} InputStream;
void initInputStream(InputStream *inputStream, unsigned char *data, unsigned int dataLen)
{
    inputStream->byteCache = 0;
    inputStream->input = data;
    inputStream->inputPos = 0;
    inputStream->maxSize = dataLen;
}

unsigned int readBits(int bitCount, InputStream *inputStream)
{
    unsigned result = 0;
    while (bitCount > 0)
    {
        if (inputStream->remainingBits == 0)
        {
            if (inputStream->inputPos == inputStream->maxSize)
            {
                inputStream->byteCache = 0;
                inputStream->remainingBits = 8;
            }
            else
            {
                inputStream->byteCache = inputStream->input[inputStream->inputPos];
                inputStream->remainingBits = 8;
                inputStream->inputPos++;
            }
        }
        result <<= 1;
        unsigned char bit = inputStream->byteCache >> 7;
        result += bit;
        inputStream->byteCache <<= 1;
        inputStream->remainingBits--;
        bitCount--;
    }

    return result;
}

typedef struct
{
    unsigned char byteCache;
    unsigned int cacheBitCount;
    unsigned char *output;
    unsigned int outPos;
} OutputStream;

void initOutputStream(OutputStream *outputStream, unsigned char *outputBuff)
{
    outputStream->cacheBitCount = 0;
    outputStream->byteCache = 0;
    outputStream->outPos = 0;
    outputStream->output = outputBuff;
}
void flushOutputStream(OutputStream *outputStream)
{
    outputStream->byteCache <<= (8 - outputStream->cacheBitCount);
    outputStream->output[outputStream->outPos] = outputStream->byteCache;
    outputStream->outPos++;
}
void writeBits(OutputStream *outputStream, unsigned int v, unsigned int count, int *hasDelay)
{
    if ((count > 0) && (*hasDelay == 1))
    {
        *hasDelay = 0;
        unsigned int bit = (v >> (count - 1)) & 1;
        count--;
        unsigned int tmp = (outputStream->byteCache + bit) << (8 - outputStream->cacheBitCount);
        outputStream->byteCache = (tmp & 0xFF) >> (8 - outputStream->cacheBitCount);
        if (tmp > 0xFF)
        {
            unsigned int index = outputStream->outPos - 1;
            while (1)
            {
                tmp = (unsigned int)(outputStream->output[index]) + 1;
                outputStream->output[index] = tmp & 0xFF;
                if (tmp > 0xFF)
                {
                    index--;
                }
                else
                {
                    break;
                }
            }
        }
    }

    while (count > 0)
    {
        unsigned char bit = (v >> (count - 1)) & 1;
        outputStream->byteCache = (outputStream->byteCache << 1) + bit;
        outputStream->cacheBitCount++;
        if (outputStream->cacheBitCount == 8)
        {
            outputStream->output[outputStream->outPos] = outputStream->byteCache;
            outputStream->byteCache = 0;
            outputStream->cacheBitCount = 0;
            outputStream->outPos++;
        }
        count--;
    }
}

void setLowFreq(unsigned char v, unsigned int *freq, unsigned int *lfreq)
{
    *lfreq = 0;
    for (int i = 0; i < v; i++)
    {
        *lfreq += freq[i];
    }
}

unsigned int getDelayBits(unsigned int low, unsigned int high, unsigned int *bitCount)
{
    unsigned int tmp, equalBitCount;
    unsigned int intBitCount = sizeof(unsigned int) * 8;
    *bitCount = 0;
    for (int i = 0; i < intBitCount; i++)
    {
        tmp = (low + (1 << i)) ^ high;
        equalBitCount = __builtin_clz(tmp);
        if (equalBitCount >= intBitCount - i)
        {
            *bitCount = intBitCount - i;
            return (low >> i) << i;
        }
    }
    return 0;
}

int encode(unsigned char *src, unsigned int inSize, unsigned char *out)
{
    unsigned int freq[256];
    initFreq(freq);
    unsigned int low = 0, high, range = 0xFFFFFFFF, total = 256;
    unsigned int step, lfreq, hfreq;
    unsigned int intBitCount = sizeof(unsigned int);

    OutputStream outputStream;
    initOutputStream(&outputStream, out);

    int hasDelay = 0;

    for (int i = 0; i <= inSize; i++)
    {
        unsigned int tmp, bitsToOutput;
        int equalBitcount;

        step = range / total;
        if (i == inSize)
        {
            low = low + step * (total - 1);
            high = low + step;
        }
        else
        {
            setLowFreq(src[i], freq, &lfreq);
            low = low + step * lfreq;
            high = low + step * freq[src[i]];
            freq[src[i]]++;
            total++;
        }

        tmp = low ^ high;
        equalBitcount = __builtin_clz(tmp);
        bitsToOutput = low >> (intBitCount - equalBitcount);
        writeBits(&outputStream, bitsToOutput, equalBitcount, &hasDelay);
        low <<= equalBitcount;
        high <<= equalBitcount;
        if (high - low < total)
        {
            unsigned bitCount;
            unsigned int delay = getDelayBits(low, high, &bitCount);

            low = (low - delay) << (bitCount - 1);
            high = (high - delay) << (bitCount - 1);
            writeBits(&outputStream, delay >> (intBitCount - bitCount), bitCount, &hasDelay);
            hasDelay = 1;
        }
        range = high - low;
    }
    flushOutputStream(&outputStream);
    return outputStream.outPos;
}

int decode(unsigned char *in, unsigned int inSize, unsigned char *out, unsigned int srcSize)
{
    unsigned int freq[256];
    initFreq(freq);
    unsigned int low = 0, high, range = 0xFFFFFFFF, total = 256;
    unsigned int step, lfreq, hfreq;
    InputStream inputStream;
    initInputStream(&inputStream, in, inSize);
    unsigned int code = readBits(32, &inputStream);
    int intBitCount = sizeof(unsigned int) * 8;
    for (int i = 0; i < srcSize; i++)
    {
        step = range / total;
        unsigned int currentFreq = (code - low) / step;
        lfreq = hfreq = 0;
        for (int j = 0; j < 256; j++)
        {
            hfreq += freq[j];
            if (lfreq <= currentFreq && currentFreq < hfreq)
            {
                total++;
                unsigned int tmp, bitCount;
                int equalbitCount;
                out[i] = j;

                low = low + step * lfreq;
                high = low + step * freq[j];
                tmp = low ^ high;
                equalbitCount = __builtin_clz(tmp);

                low <<= equalbitCount;
                high <<= equalbitCount;
                code <= equalbitCount;
                code += readBits(equalbitCount, &inputStream);

                if (high - low < total)
                {
                    unsigned int delay = getDelayBits(low, high, &bitCount);

                    low = (low - delay) << (bitCount - 1);
                    high = (high - delay) << (bitCount - 1);
                    code = (code - delay) << (bitCount - 1);
                    code += readBits(bitCount - 1, &inputStream);
                }

                range = high - low;
                freq[j]++;
                break;
            }
            lfreq += freq[j];
        }
    }
    return srcSize;
}
int main(int argc,char ** argv)
{
    FILE* file = fopen(argv[1],"r");
    unsigned int fsize;
    fseek(file,0,SEEK_END);
    fsize = ftell(file);
    fseek(file,0,SEEK_SET);

    unsigned char* in = (unsigned char*)malloc(fsize);
    unsigned char* cbuf = (unsigned char*)malloc(2*fsize);
    unsigned char* dbuf = (unsigned char*)malloc(fsize);

    unsigned int cSize = encode(in,fsize,cbuf);
    unsigned int dSize = decode(cbuf,cSize,dbuf,fsize);

    if(memcmp(in,dbuf,fsize))
    {
        printf("failed");
    }
    else
    {
        printf("sucess");
    }
    


}