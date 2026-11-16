#include "tools.h"

const struct command tool_table[] = {
    { "wc", "count lines, words, bytes: wc <name>", cmd_wc,            0 },
    { "cp", "copy a file: cp <name> <new name>",    cmd_cp,            0 },
    { "cp", "copy: cp <name> <new>, cp -r <folder> <new>", cmd_cp,     0 },
};

const unsigned int tool_count = sizeof(tool_table) / sizeof(tool_table[0]);