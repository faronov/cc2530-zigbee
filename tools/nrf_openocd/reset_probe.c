/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Links the actual OpenOCD objects; no copied or rewritten driver functions. */
#include "config.h"
#include <helper/command.h>
#include <jtag/interface.h>
#include <jtag/swd.h>
#include <stdio.h>
#include <string.h>

extern struct adapter_driver jlink_adapter_driver;
int openocd_main(int argc, char *argv[]);

int main(void)
{
	char name[] = "reset-probe";
	char option[] = "-c";
	char script[] = "noinit; adapter driver jlink; transport select swd; "
		"jlink preserve_reset on; shutdown";
	char *argv[] = { name, option, script, NULL };
	if (openocd_main(3, argv))
		return 5;
	log_init();
	const struct command_registration *cmd = jlink_adapter_driver.commands[0].chain;
	while (cmd->name && strcmp(cmd->name, "preserve_reset"))
		++cmd;
	if (!cmd->name || cmd->mode != COMMAND_CONFIG)
		return 1;
	const char *arg = "on";
	struct command_invocation call = { .argc = 1, .argv = &arg };
	if (cmd->handler(&call) != ERROR_OK || !jlink_adapter_driver.reset_pins_forbidden)
		return 2;
	if (jlink_adapter_driver.swd_ops->init() != ERROR_OK ||
			jlink_adapter_driver.swd_ops->switch_seq(LINE_RESET) != ERROR_OK)
		return 3;
	for (int trst = -1; trst <= 1; ++trst)
		for (int srst = -1; srst <= 1; ++srst)
			if (jlink_adapter_driver.reset(trst, srst) == ERROR_OK)
				return 4;
	if (adapter_assert_reset() == ERROR_OK || adapter_deassert_reset() == ERROR_OK)
		return 7;
	puts("9 reset hook rejections with a nonempty real SWD queue");
	fflush(stdout);
	arg = "off";
	if (cmd->handler(&call) != ERROR_OK || jlink_adapter_driver.reset_pins_forbidden ||
			jlink_adapter_driver.reset(0, 0) != ERROR_OK)
		return 6;
	return 0;
}
