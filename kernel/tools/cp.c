#include "tools.h"
#include "kpfs.h"
#include "vga.h"
#include "util.h"

#define TOOL_BUF 2048

static char buf[TOOL_BUF];

void cmd_cp(int argc, char **argv)
{
    /* cp -r <folder> <new name> copies a whole folder */
    if (argc >= 2 && str_eq(argv[1], "-r")) {
        if (argc < 4) {
            vga_lprint("Usage: cp -r <folder> <new name>\n");
            return;
        }
        kpfs_copy_tree(argv[2], argv[3]);
        return;
    }

    if (argc < 3) {
        vga_lprint("Usage: cp <name> <new name>  or  cp -r <folder> <new name>\n");
        return;
    }

    if (kpfs_exists(argv[2]) == 1) {
        vga_wprint("A file or folder named ");
        vga_wprint(argv[2]);
        vga_wprint(" already exists. Delete it first, or pick another name.\n");
        return;
    }

    unsigned int len = 0;
    int r = kpfs_load(argv[1], buf, TOOL_BUF, &len);
    if (r == 1) {
        vga_wprint("No file named ");
        vga_wprint(argv[1]);
        vga_putchar('\n');
        return;
    }
    if (r != 0)
        return;

    kpfs_write(argv[2], buf, len);
}