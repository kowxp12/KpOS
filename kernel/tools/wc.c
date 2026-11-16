#include "tools.h"
#include "kpfs.h"
#include "vga.h"
#include "util.h"

#define TOOL_BUF 2048

static char buf[TOOL_BUF];

void cmd_wc(int argc, char **argv)
{
    if (argc < 2) {
        vga_lprint("Usage: wc <name>\n");
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

    unsigned int lines = 0;
    unsigned int words = 0;
    int in_word = 0;

    for (unsigned int i = 0; i < len; i++) {
        char c = buf[i];

        if (c == '\n')
            lines++;

        if (c == ' ' || c == '\n' || c == '\t') {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            words++;
        }
    }

    print_udec(lines);
    vga_print(" lines, ");
    print_udec(words);
    vga_print(" words, ");
    print_udec(len);
    vga_print(" bytes\n");
}