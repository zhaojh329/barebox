#ifndef DECOMPRESS_UNLZMA_H
#define DECOMPRESS_UNLZMA_H

int decompress_unlzma(unsigned char *inbuf, int len,
	int(*fill)(void*, unsigned int),
	int(*flush)(void*, unsigned int),
	unsigned char *output,
	int *pos,
	void(*error)(char *x));
#endif