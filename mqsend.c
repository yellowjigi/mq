#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/types.h>

#include "fileio.h"

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

struct thread_parms {
	char	*file_name;
	int	file_id;
	int	msq_id;
};

// Sending (worker) thread function
void *send_fn(void *arg)
{
	struct thread_parms	*parms = (struct thread_parms *)arg;
	char			*buffer;
	FILE			*fp;
	int			read_size;
	long			file_size;
	long			remaining_size;
	int			msq_id;
	struct msqid_ds		msq_id_ds_buf;
	struct msqmsg_ds	msq_msg_ds_buf;
	int			msq_msg_bytes_max;
	char			*pos;
	char			*tmp;
	int			i;
	int			cnt = 0;

	buffer = malloc(sizeof *buffer * BUF_SIZE_DEFAULT);

	if ((fp = fopen(parms->file_name, "rb")) == NULL)
	{
		perror("fopen failed");
		pthread_exit((void *)1);
	}

	if ((file_size = get_file_size(fp)) < 0)
	{
		fprintf(stderr, "get_file_size failed.\n");
		pthread_exit((void *)1);
	}

#ifdef DEBUG
	printf("Thread\n");
	printf("File name: %s.\n", parms->file_name);
	printf("File size: %ld.\n", file_size);
#endif

	// Indicate the file identifier.
	msq_msg_ds_buf.mtype = parms->file_id;

	// 1. Send metadata (i.e., the file size).
	memcpy(msq_msg_ds_buf.mtext, &file_size, sizeof(long));

	if (msgsnd(parms->msq_id, &msq_msg_ds_buf, MSQ_MSG_BYTES_MAX, 0) != 0)
	{
		perror("msgsnd failed");
		pthread_exit((void *)1);
	}

	read_size = BUF_SIZE_DEFAULT;
	for (remaining_size = file_size; remaining_size > 0; remaining_size -= read_size)
	{
		if (remaining_size < BUF_SIZE_DEFAULT)
		{
			read_size = remaining_size;
		}

		// Load a block from the file stream.
		if (load_block(buffer, read_size, fp) < 0)
		{
			fprintf(stderr, "load_block failed.\n");
			pthread_exit((void *)1);
		}

		// Push segments of the block into the message queue.
		pos = buffer;
		for (i = 0; i < read_size; i += MSQ_MSG_BYTES_MAX)
		{
#ifdef DEBUG
			cnt++;
#endif
			memcpy(msq_msg_ds_buf.mtext, pos + i, MSQ_MSG_BYTES_MAX);

			if (msgsnd(parms->msq_id, &msq_msg_ds_buf, MSQ_MSG_BYTES_MAX, 0) != 0)
			{
				perror("msgsnd failed");
				pthread_exit((void *)1);
			}
		}
	}
#ifdef DEBUG
	printf("total %d bytes sent.\n", cnt * MSQ_MSG_BYTES_MAX);
#endif

	if (fclose(fp) != 0)
	{
		perror("fclose failed");
		pthread_exit((void *)1);
	}
	
	pthread_exit((void *)0);
}

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
	pthread_t		worker_thread[INPUT_FILE_NAME_NUM_MAX];
	struct thread_parms	parms[INPUT_FILE_NAME_NUM_MAX];

	//parms.main_thread = pthread_self();
	//parms.running = 1;

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

	for (i = 0; i < INPUT_FILE_NAME_NUM_MAX; i++)
	{
		parms[i].file_name = file_name[i];
		parms[i].file_id = i + 1;
		parms[i].msq_id = msq_id;
	}

	if ((e = pthread_create(&worker_thread[0], NULL, send_fn, (void *)&parms[0])) != 0)
	{
		perror("perror_create failed");
		return 1;
	}

	if ((e = pthread_create(&worker_thread[1], NULL, send_fn, (void *)&parms[1])) != 0)
	{
		perror("perror_create failed");
		return 1;
	}

	if ((e = pthread_create(&worker_thread[2], NULL, send_fn, (void *)&parms[2])) != 0)
	{
		perror("perror_create failed");
		return 1;
	}

	if (pthread_join(worker_thread[0], NULL))
	{
		perror("pthread_join failed");
		return 1;
	}

	if (pthread_join(worker_thread[1], NULL))
	{
		perror("pthread_join failed");
		return 1;
	}

	if (pthread_join(worker_thread[2], NULL))
	{
		perror("pthread_join failed");
		return 1;
	}

	//if (msgctl(msq_id, IPC_STAT, &msq_id_ds_buf) != 0)
	//{
	//	perror("msgctl stat failed");

	//	if (msgctl(msq_id, IPC_RMID, NULL) != 0)
	//	{
	//		perror("msgctl rmid failed");
	//	}

	//	return 1;
	//}

//#ifdef DEBUG
//	printf("msg_qnum: %d.\n", (int)msq_id_ds_buf.msg_qnum);
//	printf("msg_qbytes: %d.\n", (int)msq_id_ds_buf.msg_qbytes);
//#endif
//
//	//msq_msg_bytes_max = msq_id_ds_buf.msg_qbytes / MSQ_MSG_NUM_MAX;
//	//msq_msg_bytes_max = 512;
//	//msq_msg_ds_buf.mtext = (char *)malloc(sizeof(char) * msq_msg_bytes_max); // Normally 512.
//
//#ifdef DEBUG
//	printf("msq_msg_bytes_max: %d.\n", msq_msg_bytes_max);
//#endif
//
//	//tmp = malloc(sizeof *tmp * 64 * 1024 * 1024);
//	for (i = 0; i < INPUT_FILE_NAME_NUM_MAX; i++)
//	{
//		if ((fp = fopen(file_name[i], "rb")) == NULL)
//		{
//			perror("fopen failed");
//			break;
//		}
//
//		if (fseek(fp, 0L, SEEK_END) != 0)
//		{
//			perror("fseek failed");
//
//			if (fclose(fp) != 0)
//			{
//				perror("fclose failed");
//			}
//
//			break;
//		}
//
//		if ((file_size = ftell(fp)) == -1)
//		{
//			perror("ftell failed");
//
//			if (fclose(fp) != 0)
//			{
//				perror("fclose failed");
//			}
//
//			break;
//		}
//
//		// Indicate the file identifier.
//		// mtype must be greater than 0.
//		msq_msg_ds_buf.mtype = i + 1;
//
//		// 1. Send metadata (i.e., the file size).
//		memcpy(msq_msg_ds_buf.mtext, &file_size, sizeof(long));
//#ifdef DEBUG
//		printf("%ld ", file_size);
//#endif

		// The number of the buffers of the receiver is limited to
		// 32. Here, we handle this by always sending the maximum
		// size of the message (msq_msg_bytes_max) so that msgsnd
		// will take care of it internally (i.e., by blocking) when
		// the number of the messages on the queue reaches 32.
		//if (msgsnd(msq_id, &msq_msg_ds_buf, msq_msg_bytes_max, 0) != 0)
		//{
		//	perror("msgsnd failed");

		//	if (fclose(fp) != 0)
		//	{
		//		perror("fclose failed");
		//	}

		//	break;
		//}

		// 2. Send the file data. Reset the file position first.
		//if (fseek(fp, 0L, SEEK_SET) != 0)
		//{
		//	perror("fseek failed");

		//	if (fclose(fp) != 0)
		//	{
		//		perror("fclose failed");
		//	}

		//	break;
		//}

		//e = 0;
		//if (fread(tmp, file_size, 1, fp) != 1)
		//{
		//	perror("fread failed");

		//	if (fclose(fp) != 0)
		//	{
		//		perror("fclose failed");
		//	}

		//	break;
		//}

		//while (fread(msq_msg_ds_buf.mtext, msq_msg_bytes_max, 1, fp) == 1)
	//	pos = tmp;
	//	for (j = 0; j < file_size; j += msq_msg_bytes_max)
	//	{
	//		memcpy(msq_msg_ds_buf.mtext, pos + j, msq_msg_bytes_max);

	//		if (msgsnd(msq_id, &msq_msg_ds_buf, msq_msg_bytes_max, 0) != 0)
	//		{
	//			e = errno;
	//			perror("msgsnd failed");
	//			break;
	//		}
	//	}

	//	if (e)
	//	{
	//		if (fclose(fp) != 0)
	//		{
	//			perror("fclose failed");
	//		}

	//		break;
	//	}

	//	if (ferror(fp))
	//	{
	//		perror("fread failed");

	//		if (fclose(fp) != 0)
	//		{
	//			perror("fclose failed");
	//		}

	//		break;
	//	}

	//	// If we are here, EOF must have been reached.
	//	// Send the last portion of the file and then
	//	// move on to the next file.
	//	//memcpy(msq_msg_ds_buf.mtext, pos + i, msq_msg_bytes_max);
	//	//if (msgsnd(msq_id, &msq_msg_ds_buf, msq_msg_bytes_max, 0) != 0)
	//	//{
	//	//	perror("msgsnd failed");

	//	//	if (fclose(fp) != 0)
	//	//	{
	//	//		perror("fclose failed");
	//	//	}

	//	//	break;
	//	//}
	//	
	//	if (fclose(fp) != 0)
	//	{
	//		perror("fclose failed");
	//		break;
	//	}
	//}

	//if (msgctl(msq_id, IPC_RMID, NULL) != 0)
	//{
	//	perror("msgctl rmid failed");
	//	return 1;
	//}

	//free(msq_msg_ds_buf.mtext);

	return 0;
}
