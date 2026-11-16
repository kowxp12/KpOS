#ifndef TOOLS_H
#define TOOLS_H

#include "command.h"

/* Each tool's function. Add a prototype here for every new tool. */
void cmd_wc(int argc, char **argv);
void cmd_cp(int argc, char **argv);

/* The registry the shell reads from */
extern const struct command tool_table[];
extern const unsigned int   tool_count;

#endif