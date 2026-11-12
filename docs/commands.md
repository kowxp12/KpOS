# KpOS Command Reference

Commands are typed at the prompt and run when you press Enter. Type `help` to see the list.

Arguments are separated by spaces. Extra spaces are ignored. Commands are case-sensitive, so type them in lowercase.

Short names (like `prt`) run the same command as the long name (like `prompt`). Both work.

## General

### `help`
Shows a table of all visible commands and what they do.

### `version`
Shows the KpOS version.

### `about`
Shows the KpOS banner and version.

### `clear`
Clears the screen. New text uses the current text color.

### `echo <text>`
Prints the text that follows, with single spaces between words.
```
> echo hello   world
hello world
```

### `history`
Lists the commands you've run in this session, up to the last 16.

## Text color

### `color <0-15>`
Changes the color of normal text. The background stays black. See `color-dock.md` for the color numbers.
```
> color 10
Color changed.
```

## System information

### `meminfo`
Shows free physical memory in pages and KB, and free heap memory in bytes.

### `uptime`
Shows the seconds since boot. This is approximate, based on the system timer.

### `cpuinfo`
Shows the CPU vendor name, such as `GenuineIntel` or `AuthenticAMD`.

### `date`
Shows the clock time from the computer's clock chip, as `YYYY-MM-DD HH:MM:SS`. Some firmware stores this in UTC, so the time may be off by a few hours.

## Fun

### `beep`
Plays a short tone through the PC speaker. Some machines may have the speaker disabled.

### `fortune`
Prints a random quote.

### `roll`
Rolls a six-sided die.

## Prompt and input

### `prt <text>` (or `prompt`)
Changes the text at the start of the prompt. `prt reset` restores `>`.
```
> prt kpos
kpos > 
```

### `prc <0-15>` (or `promptcolor`)
Changes the color of the prompt.

### `pic <0-15>` (or `inputcolor`)
Changes the color of the characters you type.

### `pef <text> | clock | reaction | check | reset`
Changes what appears between the prompt and what you type.

- `pef <text>` prints your own text, up to 15 characters.
- `pef clock` shows the time when the prompt was drawn, such as `[14:32:05]`. It updates after every Enter.
- `pef reaction` puts `:)` at the right edge of the line you typed if the command worked, or `:(` if it failed.
- `pef check` puts a check mark at the right edge if the command worked, or `X` if it failed.
- `pef reset` restores the default space.

A command counts as failed if it prints a warning, an error, or a usage hint.

## Power

### `reboot`
Restarts the machine. Ctrl+Alt+Delete does the same thing.

### `shutdown`
Powers off the machine using ACPI. Works in QEMU and should work on machines with ACPI support. If it fails, KpOS shows an error and the window can be closed manually.

## Hidden

Hidden commands don't appear in `help`, but they still work if typed.

### `halt`
Stops the CPU. The keyboard stops responding until you close the window. Use `shutdown` to turn the machine off.

## Keyboard shortcuts

| Key | Action |
|-----|--------|
| Up / Down | Move through command history |
| Left / Right | Move the cursor within the line |
| Home / End | Jump to the start or end of the line |
| Backspace | Delete the character before the cursor |
| Delete | Delete the character under the cursor |
| Ctrl+C | Cancel the current line |
| Ctrl+L | Clear the screen and keep the line you're typing |
| Ctrl+Alt+Del | Reboot |

In QEMU, Ctrl+Alt+Delete may release the mouse instead. Use **View > Send Key > Ctrl+Alt+Delete** from the QEMU menu.