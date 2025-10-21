#include "ipc.h"
#include <stdio.h>
#include <unistd.h>

void ipc_init(void) { printf("ipc_init\n"); }
void ipc_send(IPC_Cmd cmd) { printf("ipc_send %d\n", cmd); }
IPC_Cmd ipc_recv(void) { 
	printf("ipc_recv\n"); 
	sleep(1);
	return IPC_CMD_SHOW;
}
