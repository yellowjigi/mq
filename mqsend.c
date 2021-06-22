#include <pthread.h>
#include <string.h>

#include "fileio.h"
#include "crc.h"
#include "queue.h"
#include "msq.h"

#define INPUT_FILE_NAME_NUM_MAX	3

#define MSQ_MSG_NUM_MAX		32
#define MSQ_MSG_BYTES_MAX	512

#define BUF_SIZE_DEFAULT	(16 * 1024 * 1024) // 16 MiB

#define ERROR_BYTES_NUM_MAX	1000

#define CONTROL_FLAG_ACK	1
#define CONTROL_FLAG_RE_TX	2

struct msqmsg_ds {
	long	mtype;
	char	mtext[MSQ_MSG_BYTES_MAX];
};

struct thread_parms {
	char		*file_name;
	char		*error_file_name;
	unsigned short	file_id;
	int		msq_id;
	struct queue	*queue;
};

// Sending (worker) thread function
void *send_fn(void *arg)
{
	unsigned short		block_offset;
	char			*buffer;
	int			buffer_e[ERROR_BYTES_NUM_MAX];
	int			bytes_sent;
	unsigned short		crc16;
	unsigned short		ctl_flag;
	int			error_count;
	long			file_size;
	FILE			*fp;
	int			i;
	int			j;
	unsigned short		msg_id;
	int			msq_id;
	struct msqmsg_ds	msq_msg_ds_buf;
	int			offset;
	int			read_size;
	long			remaining_size;
	struct thread_parms	*parms = (struct thread_parms *)arg;
	char			*pos;

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

	// Indicate the file identifier.
	msq_msg_ds_buf.mtype = (long)parms->file_id << 48;

	// 1. Send metadata (i.e., the file size).
	memcpy(msq_msg_ds_buf.mtext, &file_size, sizeof(long));

	if (msgsnd(parms->msq_id, &msq_msg_ds_buf, MSQ_MSG_BYTES_MAX, 0) != 0)
	{
		perror("msgsnd failed");
		pthread_exit((void *)1);
	}

	// Prepare the error info.
	if ((error_count = load_error_info(buffer_e, ERROR_BYTES_NUM_MAX, parms->error_file_name)) < 0)
	{
		fprintf(stderr, "load_error_info failed.\n");
		pthread_exit((void *)1);
	}

	read_size = BUF_SIZE_DEFAULT;
	bytes_sent = 0;
	j = 0;
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
		block_offset = 0;
		for (i = 0; i < read_size; i += MSQ_MSG_BYTES_MAX)
		{
			memcpy(msq_msg_ds_buf.mtext, pos + i, MSQ_MSG_BYTES_MAX);

			crc16 = compute_crc16(msq_msg_ds_buf.mtext, MSQ_MSG_BYTES_MAX);
			msq_msg_ds_buf.mtype |= crc16;
			msq_msg_ds_buf.mtype |= block_offset << 16;

			while (j < error_count && bytes_sent + MSQ_MSG_BYTES_MAX > buffer_e[j])
			{
				// We should generate error now!
				offset = buffer_e[j] % MSQ_MSG_BYTES_MAX;
				msq_msg_ds_buf.mtext[offset] = ~msq_msg_ds_buf.mtext[offset];

				j++;
			}

			if (msgsnd(parms->msq_id, &msq_msg_ds_buf, MSQ_MSG_BYTES_MAX, 0) != 0)
			{
				perror("msgsnd failed");
				pthread_exit((void *)1);
			}

			bytes_sent += MSQ_MSG_BYTES_MAX;
			block_offset++;

			msq_msg_ds_buf.mtype &= 0xFFFFFFFF00000000;
		}

		// Process the received messages for this block.
		while (1)
		{
			if ((msq_msg_ds_buf.mtype = dequeue(parms->queue)) != -1)
			{
#ifdef DEBUG
				printf("mtype: 0x%lx.\n", msq_msg_ds_buf.mtype);
#endif

				ctl_flag = (unsigned short)(msq_msg_ds_buf.mtype >> 32);
				if (ctl_flag & CONTROL_FLAG_ACK)
				{
					// If this is an ACK, move on to the next block.
					// Don't forget to reset the type field.
					msq_msg_ds_buf.mtype &= 0xFFFF000000000000;
					break;
				}

				// Otherwise, retransmit until we find an ACK.
				msg_id = (unsigned short)(msq_msg_ds_buf.mtype >> 16);

				// Reset the control flag & CRC.
				msq_msg_ds_buf.mtype &= 0xFFFF0000FFFF0000;

				offset = msg_id * MSQ_MSG_BYTES_MAX;

				memcpy(msq_msg_ds_buf.mtext, pos + offset, MSQ_MSG_BYTES_MAX);

				crc16 = compute_crc16(msq_msg_ds_buf.mtext, MSQ_MSG_BYTES_MAX);

				msq_msg_ds_buf.mtype |= crc16;
				msq_msg_ds_buf.mtype |= (long)CONTROL_FLAG_RE_TX << 32;

#ifdef DEBUG
				printf("Retransmission CRC: 0x%x.\n", crc16);
				printf("Retransmission mtype: 0x%lx.\n", msq_msg_ds_buf.mtype);
#endif

				if (msgsnd(parms->msq_id, &msq_msg_ds_buf, MSQ_MSG_BYTES_MAX, 0) != 0)
				{
					perror("msgsnd failed");
					pthread_exit((void *)1);
				}
			}
		}
	}

#ifdef DEBUG
	printf("File ID %02d: error count %hu.\n", parms->file_id, error_count);
	printf("total %d bytes sent.\n", bytes_sent);
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
	ssize_t			bytes;
	int			e;
	char			*error_file_name[INPUT_FILE_NAME_NUM_MAX];
	unsigned short		file_id;
	char			*file_name[INPUT_FILE_NAME_NUM_MAX];
	int			i;
	int			msq_id_tx;
	int			msq_id_rx;
	struct msqmsg_ds	msq_msg_ds_buf_rx;
	struct thread_parms	parms[INPUT_FILE_NAME_NUM_MAX];
	pthread_t		worker_thread[INPUT_FILE_NAME_NUM_MAX];

	if (argc < 2 || argc > 7)
	{
		printf("Usage: %s <file1> <file2> <file3> <error_file1> <error_file2> <error_file3>\n", argv[0]);
		return 0;
	}

	for (i = 0; i < INPUT_FILE_NAME_NUM_MAX; i++)
	{
		file_name[i] = argv[i + 1];
		error_file_name[i] = argv[i + 4];
	}

	if ((msq_id_tx = msq_get(IPC_KEY_PATH, IPC_KEY_PROJ_ID_ATOB)) < 0)
	{
		return 1;
	}

	// Prepare the lookup table for CRC-16.
	build_table_crc16();

	for (i = 0; i < INPUT_FILE_NAME_NUM_MAX; i++)
	{
		parms[i].file_name = file_name[i];
		parms[i].error_file_name = error_file_name[i];
		parms[i].file_id = i + 1;
		parms[i].msq_id = msq_id_tx;
		parms[i].queue = init_queue();

		if ((e = pthread_create(&worker_thread[i], NULL, send_fn, (void *)&parms[i])) != 0)
		{
			perror("perror_create failed");
			return 1;
		}
	}

	// Main thread will receive ACKs and distribute
	// them to the worker threads.
	if ((msq_id_rx = msq_get(IPC_KEY_PATH, IPC_KEY_PROJ_ID_BTOA)) < 0)
	{
		return 1;
	}
	
	while (1)
	{
		if ((bytes = msgrcv(msq_id_rx, &msq_msg_ds_buf_rx, MSQ_MSG_BYTES_MAX, 0, 0)) != MSQ_MSG_BYTES_MAX)
		{
			if (errno == EIDRM)
			{
				// The message queue has been removed.
				break;
			}
			else
			{
				perror("msgrcv failed");
				return 1;
			}
		}


		file_id = (unsigned short)(msq_msg_ds_buf_rx.mtype >> 48) - 1;

#ifdef DEBUG
		printf("File ID %02d: Received mtype 0x%lx.\n", file_id, msq_msg_ds_buf_rx.mtype);
#endif

		enqueue(parms[file_id].queue, msq_msg_ds_buf_rx.mtype);
#ifdef DEBUG
		print_queue(parms[file_id].queue);
#endif
	}

	for (i = 0; i < INPUT_FILE_NAME_NUM_MAX; i++)
	{
		if (pthread_join(worker_thread[i], NULL))
		{
			perror("pthread_join failed");
			return 1;
		}
	}

	// Release the lookup table for CRC-16.
	release_table_crc16();

	return 0;
}
