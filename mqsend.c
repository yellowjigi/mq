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
	int			e;
	char			*file_name[INPUT_FILE_NAME_NUM_MAX];
	long			file_size;
	FILE			*fp;
	int			i;
	key_t			ipc_key;
	int			j;
	int			msq_id;
	struct msqid_ds		msq_id_ds_buf;
	struct msqmsg_ds	msq_msg_ds_buf;
	int			msq_msg_bytes_max;
	char			*pos;
	char			*tmp;

	// Check if the input format is valid.
	if (argc < 2 || argc > 4)
	{
		printf("Usage: %s <file1> <file2> <file3>\n", argv[0]);
		return 0;
	}

	for (i = 0; i < INPUT_FILE_NAME_NUM_MAX; i++)
	{
		file_name[i] = argv[i + 1];
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

#ifdef DEBUG
	printf("msq_msg_bytes_max: %d.\n", msq_msg_bytes_max);
#endif

	tmp = malloc(sizeof *tmp * 64 * 1024 * 1024);
	for (i = 0; i < INPUT_FILE_NAME_NUM_MAX; i++)
	{
		if ((fp = fopen(file_name[i], "rb")) == NULL)
		{
			perror("fopen failed");
			break;
		}

		if (fseek(fp, 0L, SEEK_END) != 0)
		{
			perror("fseek failed");

			if (fclose(fp) != 0)
			{
				perror("fclose failed");
			}

			break;
		}

		if ((file_size = ftell(fp)) == -1)
		{
			perror("ftell failed");

			if (fclose(fp) != 0)
			{
				perror("fclose failed");
			}

			break;
		}

		// Indicate the file identifier.
		// mtype must be greater than 0.
		msq_msg_ds_buf.mtype = i + 1;

		// 1. Send metadata (i.e., the file size).
		memcpy(msq_msg_ds_buf.mtext, &file_size, sizeof(long));
#ifdef DEBUG
		printf("%ld ", file_size);
		printf("%hhd ", msq_msg_ds_buf.mtext[0]);
		printf("%hhd ", msq_msg_ds_buf.mtext[1]);
		printf("%hhd ", msq_msg_ds_buf.mtext[2]);
		printf("%hhd ", msq_msg_ds_buf.mtext[3]);
		printf("%hhd ", msq_msg_ds_buf.mtext[4]);
		printf("%hhd ", msq_msg_ds_buf.mtext[5]);
		printf("%hhd ", msq_msg_ds_buf.mtext[6]);
		printf("%hhd \n", msq_msg_ds_buf.mtext[7]);
#endif

		// The number of the buffers of the receiver is limited to
		// 32. Here, we handle this by always sending the maximum
		// size of the message (msq_msg_bytes_max) so that msgsnd
		// will take care of it internally (i.e., by blocking) when
		// the number of the messages on the queue reaches 32.
		if (msgsnd(msq_id, &msq_msg_ds_buf, msq_msg_bytes_max, 0) != 0)
		{
			perror("msgsnd failed");

			if (fclose(fp) != 0)
			{
				perror("fclose failed");
			}

			break;
		}

		// 2. Send the file data. Reset the file position first.
		if (fseek(fp, 0L, SEEK_SET) != 0)
		{
			perror("fseek failed");

			if (fclose(fp) != 0)
			{
				perror("fclose failed");
			}

			break;
		}

		e = 0;
		if (fread(tmp, file_size, 1, fp) != 1)
		{
			perror("fread failed");

			if (fclose(fp) != 0)
			{
				perror("fclose failed");
			}

			break;
		}

		//while (fread(msq_msg_ds_buf.mtext, msq_msg_bytes_max, 1, fp) == 1)
		pos = tmp;
		for (j = 0; j < file_size; j += msq_msg_bytes_max)
		{
			memcpy(msq_msg_ds_buf.mtext, pos + j, msq_msg_bytes_max);

			if (msgsnd(msq_id, &msq_msg_ds_buf, msq_msg_bytes_max, 0) != 0)
			{
				e = errno;
				perror("msgsnd failed");
				break;
			}
		}

		if (e)
		{
			if (fclose(fp) != 0)
			{
				perror("fclose failed");
			}

			break;
		}

		if (ferror(fp))
		{
			perror("fread failed");

			if (fclose(fp) != 0)
			{
				perror("fclose failed");
			}

			break;
		}

		// If we are here, EOF must have been reached.
		// Send the last portion of the file and then
		// move on to the next file.
		//memcpy(msq_msg_ds_buf.mtext, pos + i, msq_msg_bytes_max);
		//if (msgsnd(msq_id, &msq_msg_ds_buf, msq_msg_bytes_max, 0) != 0)
		//{
		//	perror("msgsnd failed");

		//	if (fclose(fp) != 0)
		//	{
		//		perror("fclose failed");
		//	}

		//	break;
		//}
		
		if (fclose(fp) != 0)
		{
			perror("fclose failed");
			break;
		}
	}

	//if (msgctl(msq_id, IPC_RMID, NULL) != 0)
	//{
	//	perror("msgctl rmid failed");
	//	return 1;
	//}

	//free(msq_msg_ds_buf.mtext);

	return 0;
}
