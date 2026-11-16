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

Config changes are applied in place: flakes already falling pick up the new size, speed and style, extra flakes drift in from the top, surplus ones finish their fall, and the piles stay.

## Follow the weather

[`contrib/hyprflurry-weather`](contrib/hyprflurry-weather) runs hyprflurry and keeps it in step with your local weather from [Open-Meteo](https://open-meteo.com) (free, no API key; needs `curl` and `jq`). Every 30 minutes it writes your config plus weather-derived overrides to `~/.cache/hyprflurry/hyprflurry.conf`, which hyprflurry reloads live:

- snow intensity sets the number of flakes and how fast piles grow; no snow means no flakes, and the piles melt
- wind speed and direction set `wind` and `gust` (wind from the west blows right)
- temperature sets the flake type: big wet clumps near freezing, small crystals in deep cold, faster melting above 0 °C

```ini
exec-once = LAT=43.65 LON=-79.38 ~/.local/bin/hyprflurry-weather
```

Use `--dry-run` to print the overrides for the current weather. `INTERVAL`, `BASE` and `OUT` change the check interval and the file paths; edits to your own config are picked up at the next check.
