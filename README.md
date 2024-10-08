## Description

[xswm](https://github.com/astier/xswm) is a minimal stacking and
non-reparenting window-manager for X with only one task. Open every window
maximized. Zero configuration required. Due to its limited scope it is very
minimal and performant (\~550 SLOC). No built-in:

- Hotkeys
- Notifications
- Statusbar
- Window-Decorations
- Window-Switcher
- etc.

Just a window-manager. Tries to be
[ICCCM](https://www.x.org/releases/current/doc/xorg-docs/icccm/icccm.html) and
[EWMH](https://specifications.freedesktop.org/wm-spec/latest/) compliant
insofar it makes sense, is necessary to make applications work properly and
aligns with the design-goals of xswm.

## Dependencies

- libx11

## Installation

Installs by default under `/usr/local/bin`

```sh
make install
```

To install somewhere else set `PREFIX` to the preferred installation-directory.

```sh
make install PREFIX=/some/where/else
```

## Configuration

`$XDG_CONFIG_HOME/xswm/autostart.sh` can be used to autostart programs.

## Remote-Control

xswm can be remotely controlled with `xswm <cmd>`. The following commands are
supported:

- `xswm close` to close the focused window
- `xswm last`  to focus the last window
- `xswm quit`  to quit xswm

## Recommended Programs

Since xswm is just a window-manager it should be used in combination with other
programs. Some recommendations are:

- Hotkey-Daemon like [sxhkd](https://github.com/baskerville/sxhkd)
- Application-Launcher like [dmenu](https://tools.suckless.org/dmenu/)
- Window-Switcher like [alttab](https://github.com/sagb/alttab/)
- [xhidecursor](https://github.com/astier/xhidecursor) to hide the cursor when
  typing and unhide it when moving the mouse

**Note:** No status-bar, multi-monitor or -desktop support (at least for now).
