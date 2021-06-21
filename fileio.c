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

int load_error_info(int *buffer, int size, char *file_name)
{
	FILE	*fp;
	int	i;
	int	read_count;

	if ((fp = fopen(file_name, "r")) == NULL)
	{
		perror("fopen failed");
		return -1;
	}

	read_count = 0;
	for (i = 0; i < size; i++)
	{
		if (fscanf(fp, "%d", &buffer[i]) == EOF)
		{
			break;
		}

		buffer[i]--;

		read_count++;
	}

	if (fclose(fp) != 0)
	{
		perror("fclose failed");
		return -1;
	}

	return read_count;
}
