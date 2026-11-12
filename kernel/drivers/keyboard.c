#include "keyboard.h"
#include "shell.h"

#define RELEASE_BIT 0x80
#define SC_LSHIFT   0x2A
#define SC_RSHIFT   0x36
#define SC_CTRL     0x1D
#define SC_ALT      0x38
#define SC_EXTENDED 0xE0

static const char map_plain[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
    [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
    [0x0A] = '9', [0x0B] = '0', [0x0C] = '-', [0x0D] = '=',
    [0x0E] = '\b', [0x1C] = '\n', [0x39] = ' ',

    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
    [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
    [0x18] = 'o', [0x19] = 'p', [0x1A] = '[', [0x1B] = ']',

    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f',
    [0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
    [0x26] = 'l', [0x27] = ';', [0x28] = '\'', [0x29] = '`',
    [0x2B] = '\\',

    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v',
    [0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x33] = ',',
    [0x34] = '.', [0x35] = '/',
};

static const char map_shift[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
    [0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
    [0x0A] = '(', [0x0B] = ')', [0x0C] = '_', [0x0D] = '+',
    [0x0E] = '\b', [0x1C] = '\n', [0x39] = ' ',

    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R',
    [0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
    [0x18] = 'O', [0x19] = 'P', [0x1A] = '{', [0x1B] = '}',

    [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F',
    [0x22] = 'G', [0x23] = 'H', [0x24] = 'J', [0x25] = 'K',
    [0x26] = 'L', [0x27] = ':', [0x28] = '"', [0x29] = '~',
    [0x2B] = '|',

    [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V',
    [0x30] = 'B', [0x31] = 'N', [0x32] = 'M', [0x33] = '<',
    [0x34] = '>', [0x35] = '?',
};

static int extended = 0;     /* 1 after an 0xE0 prefix */
static int shift_held = 0;
static int ctrl_held = 0;
static int alt_held = 0;

void keyboard_handle(unsigned char scancode)
{
    if (scancode == SC_EXTENDED) {
        extended = 1;
        return;
    }

    int is_ext = extended;
    extended = 0;

    int released = scancode & RELEASE_BIT;
    unsigned char code = scancode & 0x7F;

    /* Modifier keys: track press and release */
    if (code == SC_CTRL) {
        ctrl_held = !released;
        return;
    }
    if (code == SC_ALT) {
        alt_held = !released;
        return;
    }
    if (!is_ext && (code == SC_LSHIFT || code == SC_RSHIFT)) {
        shift_held = !released;
        return;
    }

    /* Ignore all other key releases */
    if (released)
        return;

    /* Extended keys: arrows, Home, End, Delete, keypad Enter */
    if (is_ext) {
        switch (code) {
        case 0x48: shell_input(KEY_UP);    break;
        case 0x50: shell_input(KEY_DOWN);  break;
        case 0x4B: shell_input(KEY_LEFT);  break;
        case 0x4D: shell_input(KEY_RIGHT); break;
        case 0x47: shell_input(KEY_HOME);  break;
        case 0x4F: shell_input(KEY_END);   break;
        case 0x1C: shell_input('\n');      break;
        case 0x53:
            if (ctrl_held && alt_held)
                shell_input(KEY_CAD);
            else
                shell_input(KEY_DEL);
            break;
        default:
            break;
        }
        return;
    }

    if (code >= 128)
        return;

    char c = shift_held ? map_shift[code] : map_plain[code];
    if (!c)
        return;

    /* Ctrl + letter becomes a control code: Ctrl+C = 3, Ctrl+L = 12 */
    if (ctrl_held) {
        if (c >= 'A' && c <= 'Z')
            c += 32;
        if (c >= 'a' && c <= 'z')
            shell_input(c & 0x1F);
        return;
    }

    shell_input(c);
}