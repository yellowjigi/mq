#include "fileio.h"

long get_file_size(FILE *fp)
{
	long file_size;
	long orig_pos;

	// Save the original position.
	orig_pos = ftell(fp);

	if (fseek(fp, 0L, SEEK_END) != 0)
	{
		perror("fseek failed");
		return -1;
	}

	if ((file_size = ftell(fp)) == -1)
	{
		perror("ftell failed");
		return -1;
	}

	// Restore the original position.
	if (fseek(fp, orig_pos, SEEK_SET) != 0)
	{
		perror("fseek failed");
		return -1;
	}

	return file_size;
}

int load_block(char *buffer, int size, FILE *fp)
{
	if (fread(buffer, size, 1, fp) != 1)
	{
		perror("fread failed");
		return -1;
	}

	return 0;
}
