#include <stdio.h>
#include <stdlib.h>

#include "crc.h"

static unsigned short *_crc_table(unsigned short *new_crc_table)
{
	static unsigned short	*crc_table = NULL;

	if (new_crc_table)
	{
		crc_table = new_crc_table;
	}

	return crc_table;
}

void build_table_crc16()
{
	const unsigned short	generator = 0x1021;
	unsigned short		*crc_table;
	int			dividend;
	unsigned short		cur_byte;
	char			bit;

	crc_table = malloc(sizeof *crc_table * 256);

	for (dividend = 0; dividend < 256; dividend++)
	{
		cur_byte = (unsigned short)(dividend << 8);

		for (bit = 0; bit < 8; bit++)
		{
			if ((cur_byte & 0x8000) != 0)
			{
				cur_byte <<= 1;
				cur_byte ^= generator;
			}
			else
			{
				cur_byte <<= 1;
			}
		}

		crc_table[dividend] = cur_byte;
	}

	_crc_table(crc_table);
}

unsigned short compute_crc16(char *bytes, size_t size)
{
	unsigned short	crc = 0;
	int		i;
	unsigned char	pos;
	unsigned short	*crc_table = _crc_table(NULL);

	for (i = 0; i < size; i++)
	{
		pos = (unsigned char)((crc >> 8) ^ bytes[i]);
		crc = (unsigned short)((crc << 8) ^ (unsigned short)crc_table[pos]);
	}

	return crc;
}

void release_table_crc16()
{
	unsigned short	*crc_table = _crc_table(NULL);

	free(crc_table);
}
