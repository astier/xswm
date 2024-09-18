## Description

[xswm](https://github.com/astier/xswm) is a stacking and non-reparenting
window-manager for X and has only one task. Open every window maximized. Zero
configuration required. No built-in:

- Hotkeys
- Statusbar
- Window-Decoration
- Window-Switcher
- etc.

Due to its limited scope it is very minimal and performant (\~340 SLOC). Tries
to be
[ICCCM](https://www.x.org/releases/current/doc/xorg-docs/icccm/icccm.html) and
[EWMH](https://specifications.freedesktop.org/wm-spec/latest/) compliant
insofar it is necessary to make applications and windows work properly.

## Dependencies

- libx11

## Installation

```sh
make install
```

## Configuration

There is no configuration. xswm opens every window maximized and that's that.
Besides that the shell-script `$XDG_CONFIG_HOME/xswm/autostart.sh` can be used
to autostart programs.

## Remote-Control

xswm can be remotely controlled with `xswm <cmd>`.
The following commands are supported:

- `xswm close` to close the focused window
- `xswm last`  to focus the last window
- `xswm quit`  to quit xswm

## Recommended Programs

Since xswm is just a window-manager is should be used in combination with other
programs to make it usable. Some recommendations are:

- Hotkey-Daemon like [sxhkd](https://github.com/baskerville/sxhkd)
- Application-Launcher like [dmenu](https://tools.suckless.org/dmenu/)
- Window-Switcher like [alttab](https://github.com/sagb/alttab/)
- [xhidecursor](https://github.com/astier/xhidecursor) to hide the cursor when
  typing and unhide it when moving the mouse

**Note:** No status-bar, multi-monitor or -desktop support (at least for now).
