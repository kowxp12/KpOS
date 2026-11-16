#ifndef EDITOR_H
#define EDITOR_H

typedef void (*editor_exit_fn)(void);

/* Open a file in the editor. exit_fn is called when the user leaves. */
void editor_start(const char *name, editor_exit_fn exit_fn);

/* Returns 1 while the editor is open */
int editor_active(void);

/* Receives every key while the editor is open */
void editor_key(int key);

#endif