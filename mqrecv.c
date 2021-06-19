#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/types.h>

//#define DEBUG

#define INPUT_FILE_NAME_NUM_MAX	3

#define IPC_KEY_PATH		"/root"
#define IPC_KEY_PROJ_ID		65

#define MSQ_MSG_NUM_MAX		32

struct msqmsg_ds {
	long	mtype;
	char	mtext[512];
};

int main(int argc, char *argv[])
{
	ssize_t			bytes;
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
	key_t			ipc_key;
	int			msq_id;
	struct msqid_ds		msq_id_ds_buf;
	struct msqmsg_ds	msq_msg_ds_buf;
	int			msq_msg_bytes_max;
	char			*pos;
	long			remaining_size[INPUT_FILE_NAME_NUM_MAX] = { 0, 0, 0 };
	char			*tmp;
	int			write_size;

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

	//msq_msg_bytes_max = msq_id_ds_buf.msg_qbytes / MSQ_MSG_NUM_MAX;
	msq_msg_bytes_max = 512;
	//msq_msg_ds_buf.mtext = (char *)malloc(sizeof(char) * msq_msg_bytes_max); // Normally 512.

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

			//free(msq_msg_ds_buf.mtext);

			return 1;
		}
	}
#ifdef DEBUG
		printf("%d) remaining_size[file_id]: %ld.\n", __LINE__, remaining_size[0]);
#endif

#ifdef DEBUG
	printf("fopen completed.\n");
#endif
	tmp = malloc(sizeof *tmp * 64 * 1024 * 1024);
	while ((bytes = msgrcv(msq_id, &msq_msg_ds_buf, msq_msg_bytes_max, 0, 0)) == msq_msg_bytes_max)
	{
		// Retrieve the file identifier. mtype must be
		// greater than 0, so the receiver subtract 1 here.
		file_id = (int)msq_msg_ds_buf.mtype - 1;
#ifdef DEBUG
		printf("bytes: %ld.\n", bytes);
		printf("file_id: %d.\n", file_id);
		//printf("remaining_size[file_id]: %ld.\n", remaining_size[file_id]);
		//printf("%ld ", file_size);
		printf("%hhd ", msq_msg_ds_buf.mtext[0]);
		printf("%hhd ", msq_msg_ds_buf.mtext[1]);
		printf("%hhd ", msq_msg_ds_buf.mtext[2]);
		printf("%hhd ", msq_msg_ds_buf.mtext[3]);
		printf("%hhd ", msq_msg_ds_buf.mtext[4]);
		printf("%hhd ", msq_msg_ds_buf.mtext[5]);
		printf("%hhd ", msq_msg_ds_buf.mtext[6]);
		printf("%hhd \n", msq_msg_ds_buf.mtext[7]);
#endif

		if (remaining_size[file_id] == 0)
		{
			// This is the first received metadata for file_id.

			// Retrieve the metadata (i.e., the file size).
			memcpy(&remaining_size[file_id], msq_msg_ds_buf.mtext, sizeof(long));
			pos = tmp;
			file_size[file_id] = remaining_size[file_id];
		}
		else
		{
#ifdef DEBUG
			printf("%d.\n", __LINE__);
#endif
			// This is the file data for file_id.

			if (remaining_size[file_id] - msq_msg_bytes_max < 0)
			{
				write_size = remaining_size[file_id];
			}
			else
			{
				write_size = msq_msg_bytes_max;
			}

			// 1. Write the file data to the corresponding file stream.
			memcpy(pos, msq_msg_ds_buf.mtext, write_size);
			pos += write_size;
			//if (fwrite(msq_msg_ds_buf.mtext, write_size, 1, fp[file_id]) != 1)
			//{
			//	if (ferror(fp[file_id]))
			//	{
			//		perror("fwrite failed");
			//		break;
			//	}
			//}

			// 2. Now calculate remaining_size.
			remaining_size[file_id] -= write_size;

			// If it becomes 0, the file copy has been completed
			// for file_id. Update the flag to mark it as completed.
			if (remaining_size[file_id] == 0)
			{
				if (fwrite(tmp, file_size[file_id], 1, fp[file_id]) != 1)
				{
					if (ferror(fp[file_id]))
					{
						perror("fwrite failed");
						break;
					}
				}

				flag -= (1 << file_id);
			}

			// 3. Check if all the file transfer has been completed.
			if (flag == 0)
			{
				break;
			}
		}
	}
	
	//free(msq_msg_ds_buf.mtext);

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

	return 0;
}
