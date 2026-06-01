# confined-floats

A Hyprland plugin that prevents floating windows from being dragged off-screen.

<img width="480" height="313" alt="example" src="https://github.com/user-attachments/assets/2c84db49-58d4-4f94-abb4-60aad57763af" />

## Installation

### hyprpm

```bash
hyprpm add https://github.com/mennemann/hyprland-confined-floats
hyprpm enable confined-floats
```

Also make sure you execute `hyprpm reload` somewhere in your autostarts.

### Manual

```bash
cmake -B build
cmake --build build
hyprctl plugin load /absolute/path/to/confined-floats/build/libconfined-floats.so
```

## Usage

The plugin adds the window rule `confined-floats:confine`. Only matching windows are confined – others move freely.

### Tight confine (default)

```lua
if hl.plugin.confined_floats ~= nil then
    hl.window_rule({
        match = { class = "^(firefox)$" },
        ["confined-floats:confine"] = true,
    })
end
```

### Offsets

The rule also accepts a value to control how much the window is pushed away from (or allowed past) the desktop edges.

- **Positive value** → push the window _inward_, away from the edges
- **Negative value** → allow the window to go _off-screen_ by that amount

Values follow CSS shorthand: 1 value (all sides), 2 values (top/bottom, left/right), 3 values (top, left/right, bottom), or 4 values (top, right, bottom, left). Append `%` for a percentage of the window's own width/height.

#### Examples

```lua
-- allow 10px off-screen on all sides (looser confine)
["confined-floats:confine"] = -10,

-- push 20px inward from top/bottom, 30px from left/right (tighter confine)
["confined-floats:confine"] = "20 30",

-- allow 5% of window size off-screen left/right, but tight on top/bottom
["confined-floats:confine"] = "0 -5% 0 -5%",
```

> [!CAUTION]
> Positive percentage offsets can cause the window to bounce far away from edges and are not recommended. Prefer negative offsets, or use pixel values for positive offsets.

#### Legacy config

```ini
# tight confine
windowrulev2 = confined-floats:confine, class:^(firefox)$

# allow 10px off-screen on all sides
windowrulev2 = confined-floats:confine = -10, class:.*

# push 20px inward from top/bottom, 30px from left/right
windowrulev2 = confined-floats:confine = 20 30, class:.*
```

## Compatibility

This plugin targets the latest stable release of [hyprwm/Hyprland](https://github.com/hyprwm/Hyprland). Git snapshots or older releases may not be compatible.
