# hyprflurry

Snow falling on your Hyprland desktop: above the wallpaper, behind your windows.

- Uses the `wlr-layer-shell` protocol (bottom layer, clicks pass through)
- Draws with OpenGL ES 3: every flake in a single draw call
- Redraws only when the compositor asks for a frame, with an fps cap; about 2% of one CPU core at 60 fps
- Configured in the Hyprland config format (hyprlang); changes apply as soon as you save
- Snow piles up along the bottom of the screen and on top of windows; moving or closing a window shakes its snow off
- Pauses entirely while a window is fullscreen
- Multi-monitor, HiDPI aware

## Build

Dependencies: `wayland`, `wayland-protocols`, `mesa` (EGL + GLESv2), `hyprlang`, `cmake`, a C++23 compiler.

```sh
make                # build into ./build
make run            # build and run with the example config
sudo make install   # installs to /usr/local (PREFIX=/usr to change)
```

Other targets: `make debug`, `make clean`, `sudo make uninstall`.

## Run

```sh
cp hyprflurry.conf ~/.config/hypr/hyprflurry.conf
hyprflurry
```

Autostart from `hyprland.conf`:

```ini
exec-once = hyprflurry
```

Toggle snow on/off with a keybind:

```ini
bind = SUPER, F12, exec, pkill -USR1 hyprflurry
```

| Signal    | Effect             |
|-----------|--------------------|
| `SIGUSR1` | toggle pause       |
| `SIGHUP`  | reload the config  |

Window positions come from Hyprland's IPC socket. Under other wlroots-based compositors, snow still falls and piles at the bottom of the screen.

See [`hyprflurry.conf`](hyprflurry.conf) for every option.
