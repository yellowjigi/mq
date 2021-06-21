#ifndef	_FILEIO_H_
#define	_FILEIO_H_

#include <stdio.h>

extern long	get_file_size(FILE *fp);
extern int	load_block(char *buffer, int size, FILE *fp);
extern int	load_error_info(int *buffer, int size, char *file_name);

#endif // _FILEIO_H_
