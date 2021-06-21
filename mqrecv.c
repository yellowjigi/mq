#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/types.h>

#include "crc.h"

#define DEBUG

#define INPUT_FILE_NAME_NUM_MAX	3

#define IPC_KEY_PATH		"/root"
#define IPC_KEY_PROJ_ID		65

#define MSQ_MSG_NUM_MAX		32
#define MSQ_MSG_BYTES_MAX	512

#define BUF_SIZE_DEFAULT	(16 * 1024 * 1024) // 16 MiB

struct msqmsg_ds {
	long	mtype;
	char	mtext[MSQ_MSG_BYTES_MAX];
};

int main(int argc, char *argv[])
{
	clock_t			begin;
	ssize_t			bytes;
	double			elapsed;
	clock_t			end;
	int			file_id;
	const char		*file_name[INPUT_FILE_NAME_NUM_MAX] = {
					"RecevedFileA.bin",
					"RecevedFileB.bin",
					"RecevedFileC.bin"
				};
	long			file_size[INPUT_FILE_NAME_NUM_MAX];
	int			flag;
	FILE			*fp[INPUT_FILE_NAME_NUM_MAX];
	int			i;
	int			offset[INPUT_FILE_NAME_NUM_MAX];
	key_t			ipc_key;
	int			msq_id;
	struct msqid_ds		msq_id_ds_buf;
	struct msqmsg_ds	msq_msg_ds_buf;
	int			msq_msg_bytes_max;
	char			*pos[INPUT_FILE_NAME_NUM_MAX];
	long			remaining_size[INPUT_FILE_NAME_NUM_MAX] = { 0, 0, 0 };
	char			*buffer[INPUT_FILE_NAME_NUM_MAX];
	int			write_size[INPUT_FILE_NAME_NUM_MAX];

	int			cnt[INPUT_FILE_NAME_NUM_MAX] = { 0, 0, 0 };
	unsigned short		crc16_received;
	unsigned short		crc16_computed;

	begin = clock();
	// Check if the input format is valid.
	if (argc > 1)
	{
		printf("Usage: %s\n", argv[0]);
		return 0;
	}

	if ((ipc_key = ftok(IPC_KEY_PATH, IPC_KEY_PROJ_ID)) == -1)
	{
		perror("ftok failed\n");
		return 1;
	}

	if ((msq_id = msgget(ipc_key, IPC_CREAT | IPC_EXCL | 0644)) == -1)
	{
		if (errno == EEXIST)
		{
			// The message queue already exists.
			// Simply retrieve the ID of it.
			msq_id = msgget(ipc_key, 0);
		}
		else
		{
			perror("msgget failed");
			return 1;
		}
	}

	if (msgctl(msq_id, IPC_STAT, &msq_id_ds_buf) != 0)
	{
		perror("msgctl stat failed");

		if (msgctl(msq_id, IPC_RMID, NULL) != 0)
		{
			perror("msgctl rmid failed");
			return 1;
		}

		return 1;
	}

#ifdef DEBUG
	printf("msg_qnum: %d.\n", (int)msq_id_ds_buf.msg_qnum);
	printf("msg_qbytes: %d.\n", (int)msq_id_ds_buf.msg_qbytes);
#endif

	// Set the flag to check later if all the transfer has been completed.
	flag = (1 << INPUT_FILE_NAME_NUM_MAX) - 1; // 0b111

	// Open the file streams with the corresponding file names.
	for (i = 0; i < INPUT_FILE_NAME_NUM_MAX; i++)
	{
		if ((fp[i] = fopen(file_name[i], "wb")) == NULL)
		{
			perror("fopen failed");

			if (msgctl(msq_id, IPC_RMID, NULL) != 0)
			{
				perror("msgctl rmid failed");
			}

			return 1;
		}
	}

	// Prepare a lookup table to calculate CRC-16.
	build_table_crc16();

	while ((bytes = msgrcv(msq_id, &msq_msg_ds_buf, MSQ_MSG_BYTES_MAX, 0, 0)) == MSQ_MSG_BYTES_MAX)
	{
		// Retrieve the file identifier. mtype must be
		// greater than 0, so the receiver subtract 1 here.
		file_id = (int)(msq_msg_ds_buf.mtype >> 16) - 1;
#ifdef DEBUG
		printf("File ID: %d.\n", file_id);
#endif

		if (remaining_size[file_id] == 0)
		{
			// This is the first received metadata for file_id.

			// 1. Create a memory pool.
			buffer[file_id] = malloc(sizeof *buffer[file_id] * BUF_SIZE_DEFAULT);

			// 2. Retrieve the metadata (i.e., the file size).
			memcpy(&remaining_size[file_id], msq_msg_ds_buf.mtext, sizeof(long));
#ifdef DEBUG
			printf("File size: %ld.\n", remaining_size[file_id]);
#endif

			if (remaining_size[file_id] < BUF_SIZE_DEFAULT)
			{
				write_size[file_id] = remaining_size[file_id];
			}
			else
			{
				write_size[file_id] = BUF_SIZE_DEFAULT;
			}

			pos[file_id] = buffer[file_id];
			offset[file_id] = 0;
		}
		else
		{
			crc16_computed = compute_crc16(msq_msg_ds_buf.mtext, MSQ_MSG_BYTES_MAX);
			crc16_received = (unsigned short)(msq_msg_ds_buf.mtype & 0xFFFF);
#ifdef DEBUG
			//printf("CRC 16 received: 0x%x.\n", crc16_received);
			//printf("CRC 16 computed: 0x%x.\n", crc16_computed);

			//printf("Byte #01: 0x%x.\n", msq_msg_ds_buf.mtext[0]);
			//printf("Byte #02: 0x%x.\n", msq_msg_ds_buf.mtext[1]);
			//printf("Byte #03: 0x%x.\n", msq_msg_ds_buf.mtext[2]);
			//printf("Byte #05: 0x%x.\n", msq_msg_ds_buf.mtext[4]);
			//printf("Byte #10: 0x%x.\n", msq_msg_ds_buf.mtext[9]);
			if (crc16_computed == crc16_received)
			{
				printf("CRC 16 verified.\n");
			}
			else
			{
				printf("CRC 16 different.\n");
				cnt[file_id]++;
			}
#endif
			// This is the file data for file_id.

			// 1. Write the file data to the corresponding file stream.
			memcpy(pos[file_id] + offset[file_id], msq_msg_ds_buf.mtext, MSQ_MSG_BYTES_MAX);
			offset[file_id] += MSQ_MSG_BYTES_MAX;
			if (offset[file_id] >= write_size[file_id])
			{
				// Buffer is ready. Write it to the file
				// stream and reset the pointer offset.
#ifdef DEBUG
				printf("File ID %02d: %d written.\n", file_id, write_size[file_id]);
#endif
				if (fwrite(buffer[file_id], write_size[file_id], 1, fp[file_id]) != 1)
				{
					if (ferror(fp[file_id]))
					{
						perror("fwrite failed");
						break;
					}
				}

				// 2. Now calculate remaining_size.
				remaining_size[file_id] -= write_size[file_id];

				// If it becomes 0, the file copy has been completed
				// for file_id. Update the flag to mark it as completed.
				if (remaining_size[file_id] == 0)
				{
					flag -= (1 << file_id);

					free(buffer[file_id]);
					break;

					// 3. Check if all the file transfer has been completed.
					if (flag == 0)
					{
						break;
					}

				}
				else
				{
					if (remaining_size[file_id] < BUF_SIZE_DEFAULT)
					{
						write_size[file_id] = remaining_size[file_id];
					}
					else
					{
						write_size[file_id] = BUF_SIZE_DEFAULT;
					}
				}

				offset[file_id] = 0;
			}
		}
	}
#ifdef DEBUG
	printf("File 01: #different CRC: %d.\n", cnt[0]);
	printf("File 02: #different CRC: %d.\n", cnt[1]);
	printf("File 03: #different CRC: %d.\n", cnt[2]);
#endif

	if (bytes == -1)
	{
		perror("msgrcv failed");

		if (msgctl(msq_id, IPC_RMID, NULL) != 0)
		{
			perror("msgctl rmid failed");
		}

		return 1;
	}

	// Close the file streams with the corresponding file names.
	for (i = 0; i < INPUT_FILE_NAME_NUM_MAX; i++)
	{
		if (fclose(fp[i]) != 0)
		{
			perror("fclose failed");

			if (msgctl(msq_id, IPC_RMID, NULL) != 0)
			{
				perror("msgctl rmid failed");
			}

			return 1;
		}
	}


	if (msgctl(msq_id, IPC_RMID, NULL) != 0)
	{
		perror("msgctl rmid failed");
		return 1;
	}

	end = clock();
	elapsed = (double)(end - begin) / CLOCKS_PER_SEC;
	printf("%lf seconds.\n", elapsed);
	return 0;
}
