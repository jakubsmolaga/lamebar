#pragma once
#include "../base/base.h"

typedef u8 CLI_Cmd;
enum {
	CLI_CMD_NONE,
	CLI_CMD_HIDE,
	CLI_CMD_SHOW,
};

typedef struct CLI_Args CLI_Args;
struct CLI_Args {
	CLI_Cmd cmd;
	u32 scale; // --scale, default=1
};

CLI_Args cli_parse(int argc, char **argv);
