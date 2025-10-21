#pragma once
#include "../base/base.h"

typedef u8 IPC_Cmd;
enum {
	IPC_CMD_NONE,
	IPC_CMD_HIDE,
	IPC_CMD_SHOW,
};

void ipc_init(void);
void ipc_send(IPC_Cmd cmd);
IPC_Cmd ipc_recv(void);
