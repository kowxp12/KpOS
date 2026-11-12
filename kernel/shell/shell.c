#include "shell.h"
#include "vga.h"
#include "io.h"

#define LINE_MAX 128

static char line[LINE_MAX];
static unsigned int len = 0;

static int streq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int startswith(const char *s, const char *prefix)
{
    while (*prefix) {
        if (*s != *prefix)
            return 0;
        s++;
        prefix++;
    }
    return 1;
}

static void print_prompt(void)
{
    vga_print("> ");
}

static void reboot(void)
{
    vga_print("Rebooting...\n");

    /* Wait until the keyboard controller is ready, then send the reset command */
    while (inb(0x64) & 0x02)
        ;
    outb(0x64, 0xFE);

    /* If reset didn't happen, stop here */
    for (;;)
        __asm__ volatile ("cli; hlt");
}

static void shutdown(void)
{
    vga_print("Shutting down...\n");

    /* Power-off ports used by QEMU. They do nothing on real hardware. */
    outw(0x604, 0x2000);    /* QEMU q35 machine */
    outw(0xB004, 0x2000);   /* QEMU older machine types */

    /* If QEMU didn't power off, stop here */
    vga_print("QEMU did not power off. You can close the window.\n");
    for (;;)
        __asm__ volatile ("cli; hlt");
}

static void run_command(const char *cmd)
{
    if (cmd[0] == '\0')
        return;

    if (streq(cmd, "help")) {
        vga_print("Commands: help, clear, echo <text>, reboot, shutdown\n");
    } else if (streq(cmd, "clear")) {
        vga_clear();
    } else if (startswith(cmd, "echo ")) {
        vga_print(cmd + 5);
        vga_putchar('\n');
    } else if (streq(cmd, "echo")) {
        vga_putchar('\n');
    } else if (streq(cmd, "reboot")) {
        reboot();
    } else if (streq(cmd, "shutdown")) {
        shutdown();
    } else {
        vga_print("Unknown command: ");
        vga_print(cmd);
        vga_putchar('\n');
    }
}

void shell_init(void)
{
    vga_print("KpOS shell. Type 'help' for commands.\n");
    print_prompt();
}

void shell_input(char c)
{
    if (c == '\n') {
        vga_putchar('\n');
        line[len] = '\0';
        run_command(line);
        len = 0;
        print_prompt();
    } else if (c == '\b') {
        if (len > 0) {
            len--;
            vga_putchar('\b');
        }
    } else if (len < LINE_MAX - 1) {
        line[len++] = c;
        vga_putchar(c);
    }
}