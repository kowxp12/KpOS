#include "editor.h"
#include "vga.h"
#include "kpfs.h"

#define EDIT_MAX      2048
#define NAME_LEN      16
#define SCREEN_COLS   80
#define FOOTER_ROW    24
#define FOOTER_COLOR  0x70        /* black on light gray */
#define KEY_CTRL_S    0x13
#define KEY_CTRL_Q    0x11

static char buf[EDIT_MAX];
static unsigned int len = 0;
static char name[NAME_LEN];
static const char *msg = "";
static int active = 0;
static int dirty = 0;             /* unsaved changes exist */
static int armed = 0;             /* Ctrl+Q pressed once with unsaved changes */
static editor_exit_fn on_exit = 0;

static void copy_name(const char *s)
{
    unsigned int i = 0;
    while (i < NAME_LEN - 1 && s[i]) {
        name[i] = s[i];
        i++;
    }
    name[i] = '\0';
}

static void append(char *out, unsigned int *pos, const char *s)
{
    while (*s && *pos < SCREEN_COLS - 1)
        out[(*pos)++] = *s++;
}

static void append_num(char *out, unsigned int *pos, unsigned int v)
{
    char t[12];
    int i = 0;

    if (v == 0)
        t[i++] = '0';
    while (v > 0) {
        t[i++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (i > 0 && *pos < SCREEN_COLS - 1)
        out[(*pos)++] = t[--i];
}

/* Draw the footer bar on the last screen row. Does not move the cursor. */
static void draw_footer(void)
{
    char text[SCREEN_COLS];
    unsigned int pos = 0;

    append(text, &pos, " EDIT ");
    append(text, &pos, name);
    append(text, &pos, "   Ctrl+S save   Ctrl+Q quit   ");
    append_num(text, &pos, len);
    append(text, &pos, " bytes   ");
    append(text, &pos, msg);
    text[pos] = '\0';

    unsigned int i = 0;
    for (; i < SCREEN_COLS && text[i]; i++)
        vga_put_at(FOOTER_ROW * SCREEN_COLS + i, text[i], FOOTER_COLOR);
    for (; i < SCREEN_COLS; i++)
        vga_put_at(FOOTER_ROW * SCREEN_COLS + i, ' ', FOOTER_COLOR);
}

/* Redraw the whole file. The cursor ends up after the last character. */
static void redraw(void)
{
    vga_clear();
    for (unsigned int i = 0; i < len; i++)
        vga_putchar(buf[i]);
    draw_footer();
}

static void save_file(void)
{
    if (kpfs_write(name, buf, len) == 0) {
        dirty = 0;
        msg = "Saved.";
    } else {
        msg = "Save failed. The file is unchanged.";
    }
}

void editor_start(const char *file_name, editor_exit_fn exit_fn)
{
    copy_name(file_name);
    len = 0;
    dirty = 0;
    armed = 0;
    msg = "";
    active = 0;

    int r = kpfs_load(name, buf, EDIT_MAX, &len);
    if (r == -1)
        return;                 /* kpfs_load already printed the reason */
    if (r == 1) {
        len = 0;
        msg = "New file. Ctrl+S creates it.";
    }

    on_exit = exit_fn;
    active = 1;
    redraw();
}

int editor_active(void)
{
    return active;
}

void editor_key(int key)
{
    if (!active)
        return;

    if (key != KEY_CTRL_Q)
        armed = 0;
    if (key != KEY_CTRL_S && key != KEY_CTRL_Q)
        msg = "";

    switch (key) {
    case KEY_CTRL_S:
        save_file();
        break;

    case KEY_CTRL_Q:
        if (dirty && !armed) {
            armed = 1;
            msg = "Unsaved changes. Press Ctrl+Q again to discard.";
        } else {
            active = 0;
            on_exit();
            return;
        }
        break;

    case '\n':
        if (len < EDIT_MAX - 1) {
            buf[len++] = '\n';
            dirty = 1;
        }
        break;

    case '\b':
        if (len > 0) {
            len--;
            dirty = 1;
        }
        break;

    default:
        if (key >= 0x20 && key <= 0x7E && len < EDIT_MAX - 1) {
            buf[len++] = (char)key;
            dirty = 1;
        }
        break;
    }

    redraw();
}