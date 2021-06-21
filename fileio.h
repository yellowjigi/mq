#ifndef	_FILEIO_H_
#define	_FILEIO_H_

#include <stdio.h>

extern long	get_file_size(FILE *fp);
extern int	load_block(char *buffer, int size, FILE *fp);
#endif // _FILEIO_H_
