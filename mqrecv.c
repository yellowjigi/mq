#include <string.h>
#include <time.h>

#include "crc.h"
#include "msq.h"

#define INPUT_FILE_NAME_NUM_MAX	3

#define MSQ_MSG_NUM_MAX		32
#define MSQ_MSG_BYTES_MAX	512

#define BUF_SIZE_DEFAULT	(16 * 1024 * 1024) // 16 MiB

#define CONTROL_FLAG_ACK	1
#define CONTROL_FLAG_RE_TX	2

struct msqmsg_ds {
	long	mtype;
	char	mtext[MSQ_MSG_BYTES_MAX];
};

int main(int argc, char *argv[])
{
	clock_t			begin;
	char			*buffer[INPUT_FILE_NAME_NUM_MAX];
	ssize_t			bytes;
	unsigned short		corrupted_count[INPUT_FILE_NAME_NUM_MAX];
	unsigned short		crc16_received;
	unsigned short		crc16_computed;
	unsigned short		ctl_flag;
	double			elapsed;
	clock_t			end;
	unsigned short		file_id;
	const char		*file_name[INPUT_FILE_NAME_NUM_MAX] = {
					"RecevedFileA.bin",
					"RecevedFileB.bin",
					"RecevedFileC.bin"
				};
	int			flag;
	FILE			*fp[INPUT_FILE_NAME_NUM_MAX];
	int			i;
	key_t			ipc_key;
	unsigned short		msg_id;
	int			msq_id_rx;
	int			msq_id_tx;
	struct msqmsg_ds	msq_msg_ds_buf;
	struct msqmsg_ds	msq_msg_ds_buf_tx;
	int			offset[INPUT_FILE_NAME_NUM_MAX];
	char			*pos[INPUT_FILE_NAME_NUM_MAX];
	long			remaining_size[INPUT_FILE_NAME_NUM_MAX] = { 0, 0, 0 };
	int			total_progress[INPUT_FILE_NAME_NUM_MAX];
	int			write_size[INPUT_FILE_NAME_NUM_MAX];

	begin = clock();

	// Check if the input format is valid.
	if (argc > 1)
	{
		printf("Usage: %s\n", argv[0]);
		return 0;
	}

	// For a receiving channel
	if ((msq_id_rx = msq_get(IPC_KEY_PATH, IPC_KEY_PROJ_ID_ATOB)) < 0)
	{
		fprintf(stderr, "msq_get failed.\n");
		exit(EXIT_FAILURE);
	}

	// For a transmitting channel
	if ((msq_id_tx = msq_get(IPC_KEY_PATH, IPC_KEY_PROJ_ID_BTOA)) < 0)
	{
		fprintf(stderr, "msq_get failed.\n");
		exit(EXIT_FAILURE);
	}

	// Set the flag to check later if all the transfer has been completed.
	flag = (1 << INPUT_FILE_NAME_NUM_MAX) - 1; // 0b111

	// Open the file streams with the corresponding file names.
	for (i = 0; i < INPUT_FILE_NAME_NUM_MAX; i++)
	{
		if ((fp[i] = fopen(file_name[i], "wb")) == NULL)
		{
			perror("fopen failed");

			if (msgctl(msq_id_rx, IPC_RMID, NULL) != 0)
			{
				perror("msgctl rmid failed");
			}

			return 1;
		}
	}

	// Prepare a lookup table to calculate CRC-16.
	build_table_crc16();

	// Main thread receives the messages and then
	// distributes them to the corresponding threads.
	while ((bytes = msgrcv(msq_id_rx, &msq_msg_ds_buf, MSQ_MSG_BYTES_MAX, 0, 0)) == MSQ_MSG_BYTES_MAX)
	{
		// Retrieve the file identifier.
		// For convenient access to arrays, subtract 1 here.
		file_id = (unsigned short)(msq_msg_ds_buf.mtype >> 48) - 1;
		msg_id = (unsigned short)(msq_msg_ds_buf.mtype >> 16);
		ctl_flag = (unsigned short)(msq_msg_ds_buf.mtype >> 32);
#ifdef DEBUG
		if (ctl_flag == 2)
		{
			printf("file_id: %hu.\n", file_id);
			printf("msg_id: %hu.\n", msg_id);
			printf("ctl_flag: 0x%x.\n", ctl_flag);
		}
#endif
		offset[file_id] = msg_id * MSQ_MSG_BYTES_MAX;

		if (remaining_size[file_id] == 0)
		{
			// This is the first received metadata for file_id.

			// 1. Create a memory pool.
			buffer[file_id] = malloc(sizeof *buffer[file_id] * BUF_SIZE_DEFAULT);

			// 2. Retrieve the metadata (i.e., the file size).
			memcpy(&remaining_size[file_id], msq_msg_ds_buf.mtext, sizeof(long));

			if (remaining_size[file_id] < BUF_SIZE_DEFAULT)
			{
				write_size[file_id] = remaining_size[file_id];
			}
			else
			{
				write_size[file_id] = BUF_SIZE_DEFAULT;
			}

			pos[file_id] = buffer[file_id];
			total_progress[file_id] = 0;
			corrupted_count[file_id] = 0;
		}
		else
		{
			// This is the file data for file_id.

			// 1. First calculate the CRC-16.

			crc16_computed = compute_crc16(msq_msg_ds_buf.mtext, MSQ_MSG_BYTES_MAX);
			crc16_received = (unsigned short)(msq_msg_ds_buf.mtype & 0xFFFF);

#ifdef DEBUG
			printf("CRC 16 computed: 0x%x.\n", crc16_computed);
			printf("CRC 16 received: 0x%x.\n", crc16_received);
#endif

			if (crc16_computed == crc16_received)
			{
#ifdef DEBUG
				printf("CRC 16 verified.\n");
#endif

				// 1-1. Succeeded.
				// Aggregate the file data to the buffer.
				memcpy(pos[file_id] + offset[file_id], msq_msg_ds_buf.mtext, MSQ_MSG_BYTES_MAX);

				if (ctl_flag & CONTROL_FLAG_RE_TX)
				{
					// This message has been retransmitted.
					corrupted_count[file_id]--;
				}
			}
			else
			{
#ifdef DEBUG
				printf("CRC 16 different.\n");
#endif

				// 1-2. Failed.
				// Send a message to request a retransmission.
				msq_msg_ds_buf_tx.mtype = msq_msg_ds_buf.mtype;
				
				if (msgsnd(msq_id_tx, &msq_msg_ds_buf_tx, MSQ_MSG_BYTES_MAX, 0) != 0)
				{
					perror("msgsnd failed");
					break;
				}

				corrupted_count[file_id]++;
			}

			if (total_progress[file_id] < write_size[file_id])
			{
				total_progress[file_id] += MSQ_MSG_BYTES_MAX;
			}

			if (total_progress[file_id] >= write_size[file_id])
			{
#ifdef DEBUG
				printf("corrupted count: %d.\n", corrupted_count[file_id]);
#endif
				// 3. Buffer is ready. Write it to the file
				// stream and reset total_progress & offset
				// for the buffer.

				// Buf before, we need to receive
				// retransmitted messages, if any.
				if (corrupted_count[file_id] > 0)
				{
					continue;
				}

				if (fwrite(buffer[file_id], write_size[file_id], 1, fp[file_id]) != 1)
				{
					if (ferror(fp[file_id]))
					{
						perror("fwrite failed");
						break;
					}
				}

#ifdef DEBUG
				printf("File ID %02d: %d written.\n", file_id, write_size[file_id]);
#endif

				// 4. Send an ACK to the sender.
				msq_msg_ds_buf_tx.mtype = msq_msg_ds_buf.mtype;
				msq_msg_ds_buf_tx.mtype |= (long)CONTROL_FLAG_ACK << 32;
				if (msgsnd(msq_id_tx, &msq_msg_ds_buf_tx, MSQ_MSG_BYTES_MAX, 0) != 0)
				{
					perror("msgsnd failed");
					break;
				}

#ifdef DEBUG
				printf("File ID %02d: ACK sent.\n", file_id);
#endif

				// 5.. Now calculate the remaining size.
				remaining_size[file_id] -= write_size[file_id];

				// If it becomes 0, the file copy has been completed
				// for file_id. Update the flag to mark it as completed.
				if (remaining_size[file_id] == 0)
				{
					flag -= (1 << file_id);

					free(buffer[file_id]);

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

				total_progress[file_id] = 0;
			}
		}
	}

	// Release the lookup table for CRC-16.
	release_table_crc16();

	if (bytes == -1)
	{
		perror("msgrcv failed");

		if (msgctl(msq_id_rx, IPC_RMID, NULL) != 0)
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

			if (msgctl(msq_id_rx, IPC_RMID, NULL) != 0)
			{
				perror("msgctl rmid failed");
			}

			return 1;
		}
	}

	if (msq_rm(msq_id_rx) < 0)
	{
		fprintf(stderr, "msq_rm failed.\n");
		exit(EXIT_FAILURE);
	}

	if (msq_rm(msq_id_tx) < 0)
	{
		fprintf(stderr, "msq_rm failed.\n");
		exit(EXIT_FAILURE);
	}

	end = clock();
	elapsed = (double)(end - begin) / CLOCKS_PER_SEC;
	printf("%lf seconds.\n", elapsed);

	return 0;
}
