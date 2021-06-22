#ifndef _MSQ_H_
#define _MSQ_H_

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/types.h>

#define IPC_KEY_PATH		"/root"
#define IPC_KEY_PROJ_ID_ATOB	65
#define IPC_KEY_PROJ_ID_BTOA	66

extern int	msq_get(char *pathname, int project_id);
extern int	msq_rm(int msq_id);

#endif // _MSQ_H_
