#include "cli.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char cli_help[] =
"lamebar - a minimalistic status bar for Wayland compositors\n"
"Usage:\n"
"  lamebar [OPTIONS]           Start the status lamebar daemon\n"
"  lamebar show                Send a 'show' message to the running daemon\n"
"  lamebar hide                Send a 'hide' message to the running daemon\n"
"\n"
"Options (for daemon mode):\n"
"  -s, --scale <FACTOR>        Set UI scale factor (default: 1)\n"
"\n"
"General options:\n"
"  -h, --help                  Show this help message and exit\n";

__attribute__((noreturn))
static void
cli_print_help_and_exit(int exit_code)
{
	fprintf(stderr, "%s", cli_help);
	exit(exit_code);
}

__attribute__((noreturn))
static void
cli_fail(const char *msg)
{
	fprintf(stderr, "ERROR: %s\n", msg);
	cli_print_help_and_exit(1);
}

CLI_Args
cli_parse(int argc, char *argv[])
{
	CLI_Cmd cmd = CLI_CMD_NONE;
	u32 scale = 1;
	for (int i=1; i<argc; i++) {
		char *arg = argv[i];
		if (arg[0] != '-') {
			if (cmd != CLI_CMD_NONE) {
				cli_fail("too many positional arguments");
			}
			if (strcmp(arg, "show") == 0) {
				cmd = CLI_CMD_SHOW;
			} else if (strcmp(arg, "hide") == 0) {
				cmd = CLI_CMD_HIDE;
			} else {
				cli_fail("unknown command");
			}
		} else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
			cli_print_help_and_exit(0);
		} else if (strcmp(arg, "-s") == 0 || strcmp(arg, "--scale") == 0) {
			if (i+1 < argc) {
				scale = atoi(argv[i+1]);
				i++;
			} else {
				cli_fail("--scale requires an argument");
			}
		} else {
			cli_fail("unknown option");
		}
	}
	return (CLI_Args){ .cmd = cmd, .scale = scale };
}
