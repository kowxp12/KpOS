#include "shell.h"
#include "keyboard.h"
#include "vga.h"
#include "io.h"
#include "acpi.h"
#include "pmm.h"
#include "heap.h"
#include "irq.h"
#include "random.h"
#include "ata.h"
#include "block.h"
#include "kpfs.h"

#define LINE_MAX     128
#define MAX_ARGS     8
#define KPOS_VERSION "v0.0.5"
#define HIST_MAX     16
#define SCREEN_COLS  80
#define COL_NAME     12
#define COL_DESC     50

static char line[LINE_MAX];
static unsigned int len = 0;        /* characters in the line */
static unsigned int pos = 0;        /* cursor position inside the line */
static unsigned int drawn_len = 0;  /* cells currently drawn on screen */

static int streq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static unsigned int str_len(const char *s)
{
    unsigned int n = 0;
    while (s[n])
        n++;
    return n;
}

/* ---- CMOS clock helpers ---- */

static unsigned char cmos_read(unsigned char reg)
{
    outb(0x70, reg);
    return inb(0x71);
}

/* Convert BCD (e.g. 0x59) to binary (59) */
static unsigned int bcd(unsigned char v)
{
    return (v & 0x0F) + ((v >> 4) * 10);
}

static void print_two_digits(unsigned int v)
{
    vga_putchar('0' + v / 10);
    vga_putchar('0' + v % 10);
}

/* ---- Appearance settings ---- */
#define PROMPT_MAX 15
#define PEF_MAX    15

enum pef_mode { PEF_TEXT, PEF_CLOCK, PEF_REACTION, PEF_CHECK };

static char prompt_text[PROMPT_MAX + 1] = ">";
static char prompt_end[PEF_MAX + 1]     = " ";
static enum pef_mode pef_kind = PEF_TEXT;
static unsigned char prompt_color = 0x0F;   /* white */
static unsigned char input_color  = 0x0F;   /* white */
static char clock_stamp[9] = "00:00:00";

/* Where the typed line starts on screen, and the scroll count at that time */
static unsigned int input_row = 0;
static unsigned int input_col = 0;
static unsigned int input_scroll = 0;

/* Read the CMOS clock into clock_stamp as HH:MM:SS */
static void stamp_clock(void)
{
    while (cmos_read(0x0A) & 0x80)
        ;

    unsigned char sec  = cmos_read(0x00);
    unsigned char min  = cmos_read(0x02);
    unsigned char hour = cmos_read(0x04);
    unsigned char fmt  = cmos_read(0x0B);

    if (!(fmt & 0x04)) {
        sec  = bcd(sec);
        min  = bcd(min);
        hour = bcd(hour);
    }

    clock_stamp[0] = '0' + hour / 10;
    clock_stamp[1] = '0' + hour % 10;
    clock_stamp[2] = ':';
    clock_stamp[3] = '0' + min / 10;
    clock_stamp[4] = '0' + min % 10;
    clock_stamp[5] = ':';
    clock_stamp[6] = '0' + sec / 10;
    clock_stamp[7] = '0' + sec % 10;
    clock_stamp[8] = '\0';
}

/* Draw the prompt, then remember where the typed line begins */
static void print_prompt(void)
{
    stamp_clock();
    vga_cprint(prompt_text, prompt_color);

    switch (pef_kind) {
    case PEF_CLOCK:
        vga_cprint("[", prompt_color);
        vga_cprint(clock_stamp, prompt_color);
        vga_cprint("] ", prompt_color);
        break;

    case PEF_REACTION:
    case PEF_CHECK:
        vga_cprint(" ", prompt_color);   /* marks go on the command line */
        break;

    default:
        vga_cprint(prompt_end, prompt_color);
        break;
    }

    vga_get_cursor(&input_row, &input_col);
    input_scroll = vga_scroll_count();
}

/* Join argv[from..] into dst with single spaces, up to max characters */
static void join_args(char *dst, unsigned int max, int from, int argc, char **argv)
{
    unsigned int p = 0;

    for (int i = from; i < argc; i++) {
        for (const char *s = argv[i]; *s && p < max; s++)
            dst[p++] = *s;
        if (i + 1 < argc && p < max)
            dst[p++] = ' ';
    }
    dst[p] = '\0';
}

/* Parse a number from 0 to 15. Returns 1 on success, 0 on failure. */
static int parse_color(const char *s, unsigned char *out)
{
    unsigned int n = 0;

    if (!*s)
        return 0;

    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '9')
            return 0;
        n = n * 10 + (*p - '0');
        if (n > 15)
            return 0;
    }

    *out = (unsigned char)n;
    return 1;
}

/* ---- Line editing and history ---- */

static char history[HIST_MAX][LINE_MAX];
static unsigned int hist_count = 0;
static unsigned int hist_view = 0;   /* hist_count means "the new, empty line" */

/* Screen index where the typed line starts, corrected for any scrolling */
static unsigned int line_start(void)
{
    int scrolled = (int)(vga_scroll_count() - input_scroll);
    int row = (int)input_row - scrolled;

    if (row < 0)
        row = 0;
    return (unsigned int)row * SCREEN_COLS + input_col;
}

static void place_cursor(void)
{
    vga_set_cursor_index(line_start() + pos);
}

/* Redraw the whole typed line in place, then put the cursor at pos */
static void redraw(void)
{
    unsigned int start = line_start();
    unsigned int cells = (len > drawn_len ? len : drawn_len) + 1;

    for (unsigned int i = 0; i < cells; i++) {
        char ch = (i < len) ? line[i] : ' ';
        vga_put_at(start + i, ch, input_color);
    }

    drawn_len = len;
    place_cursor();
}

static void history_add(const char *s)
{
    if (!s[0])
        return;

    if (hist_count > 0 && streq(history[hist_count - 1], s)) {
        hist_view = hist_count;
        return;
    }

    if (hist_count == HIST_MAX) {
        for (unsigned int i = 1; i < HIST_MAX; i++)
            for (unsigned int j = 0; j < LINE_MAX; j++)
                history[i - 1][j] = history[i][j];
        hist_count = HIST_MAX - 1;
    }

    unsigned int i = 0;
    while (s[i] && i < LINE_MAX - 1) {
        history[hist_count][i] = s[i];
        i++;
    }
    history[hist_count][i] = '\0';
    hist_count++;
    hist_view = hist_count;
}

static void load_line(const char *s)
{
    len = 0;
    while (s[len] && len < LINE_MAX - 1) {
        line[len] = s[len];
        len++;
    }
    line[len] = '\0';
    pos = len;
    redraw();
}

static void history_prev(void)
{
    if (hist_view > 0) {
        hist_view--;
        load_line(history[hist_view]);
    }
}

static void history_next(void)
{
    if (hist_view < hist_count) {
        hist_view++;
        if (hist_view == hist_count)
            load_line("");
        else
            load_line(history[hist_view]);
    }
}

/* Put the reaction or check mark at the right edge of the line the user typed */
static void draw_status(void)
{
    int row = (int)input_row - (int)(vga_scroll_count() - input_scroll);

    if (pef_kind == PEF_REACTION) {
        vga_put_right(row, vga_error_seen() ? ":(" : ":)", prompt_color);
    } else if (pef_kind == PEF_CHECK) {
        if (vga_error_seen())
            vga_put_right(row, "X", VGA_COLOR_ERROR);
        else
            vga_put_right(row, "\xFB", VGA_COLOR_OK);
    }
}

/* ---- Command table ---- */

typedef void (*cmd_fn)(int argc, char **argv);

struct command {
    const char *name;
    const char *desc;
    cmd_fn      fn;
    int         hidden;   /* 1 = don't list in help */
};

static void cmd_help(int argc, char **argv);
static void cmd_clear(int argc, char **argv);
static void cmd_echo(int argc, char **argv);
static void cmd_version(int argc, char **argv);
static void cmd_meminfo(int argc, char **argv);
static void cmd_uptime(int argc, char **argv);
static void cmd_cpuinfo(int argc, char **argv);
static void cmd_date(int argc, char **argv);
static void cmd_reboot(int argc, char **argv);
static void cmd_shutdown(int argc, char **argv);
static void cmd_halt(int argc, char **argv);
static void cmd_color(int argc, char **argv);
static void cmd_beep(int argc, char **argv);
static void cmd_fortune(int argc, char **argv);
static void cmd_roll(int argc, char **argv);
static void cmd_about(int argc, char **argv);
static void cmd_history(int argc, char **argv);
static void cmd_prompt(int argc, char **argv);
static void cmd_promptcolor(int argc, char **argv);
static void cmd_inputcolor(int argc, char **argv);
static void cmd_pef(int argc, char **argv);
static void cmd_disks(int argc, char **argv);
static void cmd_diskread(int argc, char **argv);
static void cmd_diskwrite(int argc, char **argv);
static void cmd_format(int argc, char **argv);
static void cmd_ls(int argc, char **argv);
static void cmd_write(int argc, char **argv);
static void cmd_cat(int argc, char **argv);
static void cmd_rm(int argc, char **argv);
static void cmd_mkdir(int argc, char **argv);
static void cmd_cd(int argc, char **argv);
static void cmd_pwd(int argc, char **argv);
static void cmd_mv(int argc, char **argv);
static void cmd_rename(int argc, char **argv);

/* Add a new command by adding one line here */
static const struct command commands[] = {
    { "help",        "list all commands",                 cmd_help,              0 },
    { "clear",       "clear the screen",                  cmd_clear,             0 },
    { "echo",        "print text: echo <text>",           cmd_echo,              0 },
    { "version",     "show the KpOS version",             cmd_version,           1 },
    { "meminfo",     "show free memory",                  cmd_meminfo,           1 },
    { "uptime",      "show time since boot",              cmd_uptime,            0 },
    { "cpuinfo",     "show the CPU vendor",               cmd_cpuinfo,           1 },
    { "date",        "show the clock time",               cmd_date,              0 },
    { "history",     "list previous commands",            cmd_history,           1 },
    { "reboot",      "restart the machine",               cmd_reboot,            0 },
    { "shutdown",    "power off the machine",             cmd_shutdown,          0 },
    { "halt",        "stop the CPU",                      cmd_halt,              1 },
    { "color",       "change text color: color <0-15>",   cmd_color,             0 },
    { "beep",        "play a short tone",                 cmd_beep,              0 },
    { "fortune",     "print a random quote",              cmd_fortune,           0 },
    { "roll",        "roll a six-sided die",              cmd_roll,              0 },
    { "about",       "show the KpOS banner",              cmd_about,             0 },
    { "prompt",      "change the prompt text",            cmd_prompt,            1 },
    { "prt",         "change the prompt text",            cmd_prompt,            0 },
    { "promptcolor", "change the prompt color",           cmd_promptcolor,       1 },
    { "prc",         "change the prompt color",           cmd_promptcolor,       0 },
    { "inputcolor",  "change the typed text color",       cmd_inputcolor,        1 },
    { "pic",         "change the typed text color",       cmd_inputcolor,        0 },
    { "pef",         "prompt end: text, clock, reaction, check, reset", cmd_pef, 0 },
    { "disks",       "check for a disk drive",            cmd_disks,             0 },
    { "diskread",    "show a sector: diskread <sector>",  cmd_diskread,          1 },
    { "diskwrite",   "save text: diskwrite <sector> <text>", cmd_diskwrite,      1 },
    { "format",      "erase and format the disk (format yes)", cmd_format,       0 },
    { "ls",          "list files",                        cmd_ls,                0 },
    { "write",       "save a file: write <name> <text>",  cmd_write,             0 },
    { "cat",         "show a file: cat <name>",           cmd_cat,               0 },
    { "rm",          "delete: rm <name>, rm -y <folder>", cmd_rm,                0 },
    { "mkdir",       "make a folder: mkdir <name>",       cmd_mkdir,             0 },
    { "cd",          "change folder: cd <name>, cd .., cd /", cmd_cd,            0 },
    { "pwd",         "show the current folder",           cmd_pwd,               0 },
    { "mv",          "move: mv <name> <folder>",          cmd_mv,                0 },
    { "rename",      "rename: rename <old> <new>",        cmd_rename,            0 },
};

#define NUM_COMMANDS (sizeof(commands) / sizeof(commands[0]))

static void print_udec(unsigned int value)
{
    char buf[12];
    int i = 0;

    if (value == 0)
        buf[i++] = '0';
    while (value > 0) {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    }
    while (i > 0)
        vga_putchar(buf[--i]);
}

static int parse_uint(const char *s, unsigned int *out)
{
    unsigned int n = 0;

    if (!*s)
        return 0;
    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '9')
            return 0;
        n = n * 10 + (*p - '0');
    }
    *out = n;
    return 1;
}

static unsigned char sector_buf[512];

/* Split the line in place into words. Returns the word count. */
static int tokenize(char *s, char **argv)
{
    int argc = 0;

    while (*s) {
        while (*s == ' ')
            *s++ = '\0';
        if (!*s || argc == MAX_ARGS)
            break;

        argv[argc++] = s;
        while (*s && *s != ' ')
            s++;
    }
    return argc;
}

/* Not a command. Stops the CPU after reboot or a failed power-off. */
static void stop_cpu(void)
{
    for (;;)
        __asm__ volatile ("cli; hlt");
}

/* Used by the reboot command and by Ctrl+Alt+Delete */
static void reboot_now(void)
{
    vga_print("Rebooting...\n");

    while (inb(0x64) & 0x02)
        ;
    outb(0x64, 0xFE);

    stop_cpu();
}

static void run_command(char *cmd)
{
    char *argv[MAX_ARGS];
    int argc = tokenize(cmd, argv);

    if (argc == 0)
        return;

    for (unsigned int i = 0; i < NUM_COMMANDS; i++) {
        if (streq(argv[0], commands[i].name)) {
            commands[i].fn(argc, argv);
            return;
        }
    }

    vga_eprint("Unknown command: ");
    vga_cprint(argv[0], VGA_COLOR_WARN);
    vga_lprint(" (type 'help')\n");
}

/* ---- Command implementations ---- */

/* Draw a horizontal border like +------+------+ */
static void print_rule(void)
{
    vga_print("+");
    for (unsigned int i = 0; i < COL_NAME + 2; i++)
        vga_putchar('-');
    vga_print("+");
    for (unsigned int i = 0; i < COL_DESC + 2; i++)
        vga_putchar('-');
    vga_print("+\n");
}

/* One cell: a space, the text, then spaces up to the column width, then a space */
static void print_cell(const char *s, unsigned int width, unsigned char color)
{
    unsigned int n = str_len(s);

    vga_putchar(' ');
    vga_cprint(s, color);
    for (unsigned int i = n; i < width; i++)
        vga_putchar(' ');
    vga_putchar(' ');
}

/* One table row: | name | description | */
static void print_row(const char *name, const char *desc,
                      unsigned char name_color, unsigned char desc_color)
{
    vga_print("|");
    print_cell(name, COL_NAME, name_color);
    vga_print("|");
    print_cell(desc, COL_DESC, desc_color);
    vga_print("|\n");
}

static void cmd_help(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    print_rule();
    print_row("Command", "Description", 15, 15);
    print_rule();

    for (unsigned int i = 0; i < NUM_COMMANDS; i++) {
        if (commands[i].hidden)
            continue;
        print_row(commands[i].name, commands[i].desc, 14, 7);
    }

    print_rule();
}

static void cmd_clear(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    vga_clear();
}

static void cmd_echo(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        vga_print(argv[i]);
        if (i + 1 < argc)
            vga_putchar(' ');
    }
    vga_putchar('\n');
}

static void cmd_version(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    vga_print("KpOS " KPOS_VERSION "\n");
}

static void cmd_meminfo(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    vga_print("Free pages: ");
    print_udec(pmm_free_pages());
    vga_print(" (");
    print_udec(pmm_free_pages() * 4);
    vga_print(" KB)\n");

    vga_print("Heap free:  ");
    print_udec(heap_free_bytes());
    vga_print(" bytes\n");
}

static void cmd_uptime(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    /* The PIT runs at about 18.2 Hz by default, so 18 ticks is roughly one second */
    unsigned int secs = irq_ticks() / 18;

    vga_print("Uptime: ");
    print_udec(secs);
    vga_print(" seconds\n");
}

static void put_u32_chars(char *dst, unsigned int v)
{
    dst[0] = v & 0xFF;
    dst[1] = (v >> 8) & 0xFF;
    dst[2] = (v >> 16) & 0xFF;
    dst[3] = (v >> 24) & 0xFF;
}

static void cmd_cpuinfo(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    /* CPUID leaf 0 returns the vendor name in EBX, EDX, ECX */
    unsigned int eax, ebx, ecx, edx;
    __asm__ volatile ("cpuid"
                      : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                      : "a"(0));

    char vendor[13];
    put_u32_chars(&vendor[0], ebx);
    put_u32_chars(&vendor[4], edx);
    put_u32_chars(&vendor[8], ecx);
    vendor[12] = '\0';

    vga_print("CPU vendor: ");
    vga_print(vendor);
    vga_putchar('\n');
}

static void cmd_date(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    while (cmos_read(0x0A) & 0x80)
        ;

    unsigned char sec   = cmos_read(0x00);
    unsigned char min   = cmos_read(0x02);
    unsigned char hour  = cmos_read(0x04);
    unsigned char day   = cmos_read(0x07);
    unsigned char month = cmos_read(0x08);
    unsigned char year  = cmos_read(0x09);
    unsigned char cent  = cmos_read(0x32);
    unsigned char fmt   = cmos_read(0x0B);

    /* Bit 2 of register 0x0B means binary; otherwise values are BCD */
    if (!(fmt & 0x04)) {
        sec   = bcd(sec);
        min   = bcd(min);
        hour  = bcd(hour);
        day   = bcd(day);
        month = bcd(month);
        year  = bcd(year);
        cent  = bcd(cent);
    }

    print_two_digits(cent);
    print_two_digits(year);
    vga_putchar('-');
    print_two_digits(month);
    vga_putchar('-');
    print_two_digits(day);
    vga_putchar(' ');
    print_two_digits(hour);
    vga_putchar(':');
    print_two_digits(min);
    vga_putchar(':');
    print_two_digits(sec);
    vga_putchar('\n');
}

static void cmd_history(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (hist_count == 0) {
        vga_print("No history yet.\n");
        return;
    }

    for (unsigned int i = 0; i < hist_count; i++) {
        print_udec(i + 1);
        vga_print(". ");
        vga_print(history[i]);
        vga_putchar('\n');
    }
}

static void cmd_reboot(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    reboot_now();
}

static void cmd_shutdown(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    vga_print("Shutting down...\n");

    acpi_poweroff();

    /* Fallback for QEMU if ACPI didn't work */
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);

    vga_eprint("Power-off failed. Close the window to exit.\n");
}

static void cmd_halt(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    vga_print("Easter egg! The CPU is now asleep. Restart your computer :D\n");
    stop_cpu();
}

static void cmd_color(int argc, char **argv)
{
    if (argc < 2) {
        vga_lprint("Usage: color <0-15>\n");
        return;
    }

    unsigned char n;
    if (!parse_color(argv[1], &n)) {
        vga_wprint("Color must be a number from 0 to 15\n");
        return;
    }

    vga_set_color(n);
    vga_print("Color changed.\n");
}

/* Busy-wait so the tone lasts a short while */
static void delay(unsigned int loops)
{
    for (volatile unsigned int i = 0; i < loops; i++)
        ;
}

static void cmd_beep(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    /* Program PIT channel 2 to an 880 Hz square wave: 1193180 / 880 */
    unsigned int divisor = 1356;
    outb(0x43, 0xB6);
    outb(0x42, divisor & 0xFF);
    outb(0x42, (divisor >> 8) & 0xFF);

    /* Turn the speaker on by setting bits 0 and 1 of port 0x61 */
    unsigned char port = inb(0x61);
    outb(0x61, port | 0x03);

    delay(20000000);

    /* Turn the speaker off again */
    outb(0x61, port & ~0x03);
}

static void cmd_fortune(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    static const char *quotes[] = {
        "Every expert was once a beginner.",
        "Build it, break it, learn from it.",
        "Small steps still reach the top of the mountain.",
        "A kernel is just a program that trusts no one.",
        "Simplicity is the ultimate sophistication.",
    };
    const unsigned int count = sizeof(quotes) / sizeof(quotes[0]);

    vga_cprint(quotes[random_range(count)], 14);
    vga_putchar('\n');
}

static void cmd_roll(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    unsigned int result = random_range(6) + 1;

    vga_cprint("You rolled: ", 2);
    vga_putchar('0' + result);
    vga_putchar('\n');
}

static void cmd_about(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    vga_print("                            \n");
    vga_cprint("       ##############       \n", 7);

    vga_cprint("   ######", 7);
    vga_cprint("++++++++++", 5);
    vga_cprint("######   \n", 7);

    vga_cprint("  ###", 7);
    vga_cprint("++++++++++++++++++", 5);
    vga_cprint("###  \n", 7);

    vga_cprint("  #", 7);
    vga_cprint("++++++", 5);
    vga_cprint("##########", 7);
    vga_cprint("++++++", 5);
    vga_cprint("#  \n", 7);

    vga_cprint("  #", 7);
    vga_cprint("++++", 5);
    vga_cprint("####      ####", 7);
    vga_cprint("++++", 5);
    vga_cprint("#  \n", 7);

    vga_cprint("  #", 7);
    vga_cprint("++++", 5);
    vga_cprint("##          ##", 7);
    vga_cprint("++++", 5);
    vga_cprint("#  \n", 7);

    vga_cprint("  #", 7);
    vga_cprint("++++", 5);
    vga_cprint("##          ##", 7);
    vga_cprint("++++", 5);
    vga_cprint("#  \n", 7);

    vga_cprint("  ##", 7);
    vga_cprint("+++", 5);
    vga_cprint("####      ####", 7);
    vga_cprint("+++", 5);
    vga_cprint("##  \n", 7);

    vga_cprint("   ##", 7);
    vga_cprint("++++", 5);
    vga_cprint("###    ###", 7);
    vga_cprint("++++", 5);
    vga_cprint("##   \n", 7);

    vga_cprint("   ####", 7);
    vga_cprint("++++", 5);
    vga_cprint("#    #", 7);
    vga_cprint("++++", 5);
    vga_cprint("####   \n", 7);

    vga_cprint("  ###", 7);
    vga_cprint("++++++", 5);
    vga_cprint("#    #", 7);
    vga_cprint("++++++", 5);
    vga_cprint("###  \n", 7);

    vga_cprint("  #", 7);
    vga_cprint("++++++", 5);
    vga_cprint("###    ###", 7);
    vga_cprint("++++++", 5);
    vga_cprint("#  \n", 7);

    vga_cprint("  #########      #########  \n", 7);

    vga_print("                            \n");
    vga_cprint("        KpOS  " KPOS_VERSION "       \n", 5);
    vga_print("                            \n");
}

static void cmd_prompt(int argc, char **argv)
{
    if (argc < 2) {
        vga_lprint("Usage: prompt <text> or prompt reset\n");
        return;
    }

    if (streq(argv[1], "reset")) {
        prompt_text[0] = '>';
        prompt_text[1] = '\0';
        vga_print("Prompt reset.\n");
        return;
    }

    join_args(prompt_text, PROMPT_MAX, 1, argc, argv);
    vga_print("Prompt changed.\n");
}

static void cmd_promptcolor(int argc, char **argv)
{
    unsigned char c;

    if (argc < 2 || !parse_color(argv[1], &c)) {
        vga_lprint("Usage: promptcolor <0-15>\n");
        return;
    }

    prompt_color = c;
    vga_print("Prompt color changed.\n");
}

static void cmd_inputcolor(int argc, char **argv)
{
    unsigned char c;

    if (argc < 2 || !parse_color(argv[1], &c)) {
        vga_lprint("Usage: inputcolor <0-15>\n");
        return;
    }

    input_color = c;
    vga_print("Input color changed.\n");
}

static void cmd_pef(int argc, char **argv)
{
    if (argc < 2) {
        vga_lprint("Usage: pef <text> | clock | reaction | check | reset\n");
        return;
    }

    if (streq(argv[1], "reset")) {
        pef_kind = PEF_TEXT;
        prompt_end[0] = ' ';
        prompt_end[1] = '\0';
        vga_print("Prompt end reset.\n");
    } else if (streq(argv[1], "clock")) {
        pef_kind = PEF_CLOCK;
        vga_print("Prompt end: clock.\n");
    } else if (streq(argv[1], "reaction")) {
        pef_kind = PEF_REACTION;
        vga_print("Prompt end: reaction.\n");
    } else if (streq(argv[1], "check")) {
        pef_kind = PEF_CHECK;
        vga_print("Prompt end: check.\n");
    } else {
        pef_kind = PEF_TEXT;
        join_args(prompt_end, PEF_MAX, 1, argc, argv);
        vga_print("Prompt end changed.\n");
    }
}

static void cmd_disks(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (block_init() != 0) {
        vga_wprint("No disk found.\n");
        return;
    }

    vga_print("Disk: ");
    vga_print_u64(block_sectors());
    vga_print(" sectors, ");
    vga_print_u64(block_sectors() >> 11);   /* 2048 sectors = 1 MB */
    vga_print(" MB\n");

    vga_print("Filesystem: ");
    vga_print(kpfs_probe() ? "KPFS v2 (formatted)\n" : "not formatted\n");
}

static void cmd_diskread(int argc, char **argv)
{
    unsigned int lba;

    if (argc < 2 || !parse_uint(argv[1], &lba)) {
        vga_lprint("Usage: diskread <sector>\n");
        return;
    }

    if (ata_read(lba, sector_buf)) {
        vga_eprint("Read failed. Is a disk attached?\n");
        return;
    }

    const char *hex = "0123456789ABCDEF";

    /* Show the first 64 bytes: 16 per row, hex on the left, text on the right */
    for (unsigned int row = 0; row < 4; row++) {
        for (unsigned int i = 0; i < 16; i++) {
            unsigned char b = sector_buf[row * 16 + i];
            vga_putchar(hex[b >> 4]);
            vga_putchar(hex[b & 0x0F]);
            vga_putchar(' ');
        }
        vga_print(" |");
        for (unsigned int i = 0; i < 16; i++) {
            unsigned char b = sector_buf[row * 16 + i];
            vga_putchar((b >= 0x20 && b <= 0x7E) ? b : '.');
        }
        vga_print("|\n");
    }
}

static void cmd_diskwrite(int argc, char **argv)
{
    unsigned int lba;

    if (argc < 3 || !parse_uint(argv[1], &lba)) {
        vga_lprint("Usage: diskwrite <sector> <text>\n");
        return;
    }

    /* Clear the sector, then copy the text in. Spaces between words are kept as single spaces. */
    for (unsigned int i = 0; i < 512; i++)
        sector_buf[i] = 0;
    join_args((char *)sector_buf, 511, 2, argc, argv);

    if (ata_write(lba, sector_buf)) {
        vga_eprint("Write failed. Is a disk attached?\n");
        return;
    }
    vga_print("Saved to sector ");
    print_udec(lba);
    vga_putchar('\n');
}

static void cmd_format(int argc, char **argv)
{
    /* Formatting erases the disk, so it needs an explicit confirmation word */
    if (argc < 2 || !streq(argv[1], "yes")) {
        vga_wprint("format erases everything on the disk. To continue, type: format yes\n");
        return;
    }

    kpfs_format();
}

static void cmd_ls(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    kpfs_list();
}

static void cmd_write(int argc, char **argv)
{
    if (argc < 2) {
        vga_lprint("Usage: write <name> <text>\n");
        return;
    }

    /* Text is everything after the name. Words are joined with single spaces. */
    static char text[LINE_MAX];
    unsigned int len = 0;

    if (argc >= 3) {
        join_args(text, LINE_MAX - 1, 2, argc, argv);
        len = str_len(text);
    }

    kpfs_write(argv[1], text, len);
}

static void cmd_cat(int argc, char **argv)
{
    if (argc < 2) {
        vga_lprint("Usage: cat <name>\n");
        return;
    }
    kpfs_read(argv[1]);
}

static void cmd_rm(int argc, char **argv)
{
    int force = 0;
    int i = 1;

    if (argc >= 2 && streq(argv[1], "-y")) {
        force = 1;
        i = 2;
    }

    if (argc <= i) {
        vga_lprint("Usage: rm <name> or rm -y <name>\n");
        return;
    }
    kpfs_remove(argv[i], force);
}

static void cmd_mkdir(int argc, char **argv)
{
    if (argc < 2) {
        vga_lprint("Usage: mkdir <name>\n");
        return;
    }
    kpfs_mkdir(argv[1]);
}

static void cmd_cd(int argc, char **argv)
{
    /* With no name, cd goes to the root folder */
    kpfs_cd(argc < 2 ? "/" : argv[1]);
}

static void cmd_pwd(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    kpfs_pwd();
}

static void cmd_mv(int argc, char **argv)
{
    if (argc < 3) {
        vga_lprint("Usage: mv <name> <folder>   (folder can be .., / or a folder name)\n");
        return;
    }
    kpfs_move(argv[1], argv[2]);
}

static void cmd_rename(int argc, char **argv)
{
    if (argc < 3) {
        vga_lprint("Usage: rename <old> <new>\n");
        return;
    }
    kpfs_rename(argv[1], argv[2]);
}

/* ---- Shell input and startup ---- */

void shell_init(void)
{
    random_seed();
    
    cmd_about(0, 0);

    vga_cprint("KpOS Kernel\n", 5);
    vga_cprint("System initialized.\n", 5);
    
    vga_cprint("KpOS " KPOS_VERSION " shell. Type 'help' for commands.\n", 5);

    print_prompt();
}

/* Run the typed line, save it to history, and start a new prompt */
static void submit_line(void)
{
    line[len] = '\0';
    vga_putchar('\n');

    history_add(line);
    vga_reset_error();
    run_command(line);   /* this modifies line, which is why history was saved first */
    draw_status();

    len = 0;
    pos = 0;
    drawn_len = 0;
    hist_view = hist_count;
    print_prompt();
}

/* Ctrl+C: throw away the current line */
static void cancel_line(void)
{
    vga_cprint("^C", VGA_COLOR_WARN);
    vga_putchar('\n');

    len = 0;
    pos = 0;
    drawn_len = 0;
    hist_view = hist_count;
    vga_reset_error();
    print_prompt();
}

/* Ctrl+L: clear the screen but keep the typed line */
static void clear_keep_line(void)
{
    vga_clear();
    print_prompt();
    redraw();
}

void shell_input(int key)
{
    switch (key) {
    case KEY_UP:
        history_prev();
        return;

    case KEY_DOWN:
        history_next();
        return;

    case KEY_LEFT:
        if (pos > 0) {
            pos--;
            place_cursor();
        }
        return;

    case KEY_RIGHT:
        if (pos < len) {
            pos++;
            place_cursor();
        }
        return;

    case KEY_HOME:
        pos = 0;
        place_cursor();
        return;

    case KEY_END:
        pos = len;
        place_cursor();
        return;

    case KEY_DEL:
        if (pos < len) {
            for (unsigned int i = pos; i + 1 < len; i++)
                line[i] = line[i + 1];
            len--;
            line[len] = '\0';
            redraw();
        }
        return;

    case KEY_CAD:
        reboot_now();
        return;

    case 0x03:          /* Ctrl+C */
        cancel_line();
        return;

    case 0x0C:          /* Ctrl+L */
        clear_keep_line();
        return;

    case '\n':
        submit_line();
        return;

    case '\b':
        if (pos > 0) {
            for (unsigned int i = pos - 1; i + 1 < len; i++)
                line[i] = line[i + 1];
            len--;
            pos--;
            line[len] = '\0';
            redraw();
        }
        return;

    default:
        break;
    }

    /* Only printable characters are typed. Other control codes are ignored. */
    if (key < 0x20 || key > 0x7E)
        return;
    if (len >= LINE_MAX - 1)
        return;

    if (pos == len) {
        /* Typing at the end: draw one character and let the screen handle scrolling */
        line[len++] = (char)key;
        line[len] = '\0';
        pos = len;
        vga_cprint((const char[]){ (char)key, '\0' }, input_color);
        drawn_len = len;
    } else {
        /* Typing in the middle: shift the rest right, then redraw */
        for (unsigned int i = len; i > pos; i--)
            line[i] = line[i - 1];
        line[pos] = (char)key;
        len++;
        pos++;
        line[len] = '\0';
        redraw();
    }
}