#ifndef COMMAND_H
#define COMMAND_H

typedef void (*cmd_fn)(int argc, char **argv);

struct command {
    const char *name;
    const char *desc;
    cmd_fn      fn;
    int         hidden;     /* 1 = don't list in help */
};

#endif