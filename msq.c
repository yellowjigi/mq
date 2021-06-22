#include "msq.h"

int msq_get(char *pathname, int project_id)
{
	key_t	ipc_key;
	int	msq_id;

	if ((ipc_key = ftok(pathname, project_id)) == -1)
	{
		perror("ftok failed\n");
		return -1;
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
			return -1;
		}
	}

	return msq_id;
}

int msq_rm(int msq_id)
{
	if (msgctl(msq_id, IPC_RMID, NULL) != 0)
	{
		perror("msgctl rmid failed");
		return -1;
	}

	return 0;
}
