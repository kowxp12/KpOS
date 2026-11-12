#ifndef KEYBOARD_H
#define KEYBOARD_H

/* Special keys sent to shell_input(). Normal characters use their ASCII value. */
#define KEY_UP     0x80
#define KEY_DOWN   0x81
#define KEY_LEFT   0x82
#define KEY_RIGHT  0x83
#define KEY_HOME   0x84
#define KEY_END    0x85
#define KEY_DEL    0x86
#define KEY_CAD    0x87   /* Ctrl+Alt+Delete */

void keyboard_handle(unsigned char scancode);

#endif