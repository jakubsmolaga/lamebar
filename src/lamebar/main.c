#include "cli.h"
#include "ipc.h"
#include "wl.h"
#include "core.h"
#include "../base/arena.h"
#include <stdio.h>

int
main(int argc, char *argv[])
{
	CLI_Args args = cli_parse(argc, argv);
	if (args.cmd == CLI_CMD_SHOW) {
		ipc_send(IPC_CMD_SHOW);
		return 0;
	}
	if (args.cmd == CLI_CMD_HIDE) {
		ipc_send(IPC_CMD_HIDE);
		return 0;
	}
	printf("starting daemon\n");
	wl_init();
	ipc_init();
	Arena arena = arena_create();
	while (1) {
		arena_clear(&arena);
		IPC_Cmd cmd = ipc_recv();
		switch (cmd) {
		case IPC_CMD_HIDE: {
			wl_hide();
			break;
		}
		case IPC_CMD_SHOW: {
			PixelBuf pixels = next_frame(&arena);
			wl_show(pixels, args.scale);
			break;
		}
		default: {
			break;
		}
		}
	}
}
