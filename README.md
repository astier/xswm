## Description

[xswm](https://github.com/astier/xswm) is a stacking and non-reparanting
window-manager for X and has only one task. Open every window maximized. Zero
configuration required. Due to its limited scope it is very minimal and
performant (\~320 SLOC). No built-in hotkeys, statusbar, tags, etc. Just a
window-manager. Tries to be
[ICCCM](https://www.x.org/releases/current/doc/xorg-docs/icccm/icccm.html) and
[EWMH](https://specifications.freedesktop.org/wm-spec/latest/) compliant insofar
it is necessary to make applications and windows work properly.

## Installation

Installs by default under `$HOME/.local/bin`

```sh
make install
```

To install somewhere else set `PREFIX` to the preferred installation-directory.

```sh
make install PREFIX=/some/where/else
```

## Configuration

There is no configuration. xswm opens every window maximized and that's that.
Besides that the shell-script `$XDG_CONFIG_HOME/xswm/autostart.sh` can be used
to autostart programs. To extend its capabilities use xswm in combination with
other programs. The minimum recommendations to make xswm usable are:

- Hotkey-Daemon like [sxhkd](https://github.com/baskerville/sxhkd)
- Application-Launcher like [dmenu](https://tools.suckless.org/dmenu/)
- Window-Switcher like [alttab](https://github.com/sagb/alttab/)

No status-bar, multi-monitor or -desktop support.

## Remote-Control

xswm can be remotely controlled with `xswm <cmd>`.
Currently only two commands are supported:

- `xswm delete` to close focused window
- `xswm last`   to focus the last window

To quit send the `SIGTERM` signal with `pkill xswm`. xswm will exit gracefully.
