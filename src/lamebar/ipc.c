#include "ipc.h"
#include "../base/log.h"
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/file.h>

// TODO: replace with /run/user/$UID/...
//       this will allow multiple users to run their own daemons
static const char *ipc_pipe_path = "/tmp/lamebar.pipe";
static int ipc_pipe_fd = -1;

void ipc_init(void) {
	unlink(ipc_pipe_path);
	int ret = mkfifo(ipc_pipe_path, 0600);
	log_assert(ret >= 0, "failed to create fifo");
	ipc_pipe_fd = open(ipc_pipe_path, O_RDWR | O_CREAT, 0600);
	log_assert(ipc_pipe_fd >= 0, "failed to open pipe");
}

void ipc_send(IPC_Cmd cmd) { 
	int fd = open(ipc_pipe_path, O_WRONLY);
	log_assert(fd >= 0, "failed to open pipe");
	switch (cmd) {
	case IPC_CMD_SHOW:
		write(fd, "1", 1);
		break;
	case IPC_CMD_HIDE:
		write(fd, "0", 1);
		break;
	default:
		log_crash("unknown cmd %d", cmd);
	}
}

IPC_Cmd ipc_recv(void) { 
	char c;
	int ret = read(ipc_pipe_fd, &c, 1);
	switch (c) {
	case '1': return IPC_CMD_SHOW;
	case '0': return IPC_CMD_HIDE;
	default: return IPC_CMD_NONE;
	}
}
