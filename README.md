# confined-floats

Prevent floating windows from being dragged off-screen.

<img width="480" height="313" alt="example" src="https://github.com/user-attachments/assets/2c84db49-58d4-4f94-abb4-60aad57763af" />

## Installation

### hyprpm

```bash
hyprpm add https://github.com/mennemann/hyprland-confined-floats
hyprpm enable confined-floats
```

### Manual

```bash
cmake -B build
cmake --build build
hyprctl plugin load /absolute/path/to/confined-floats/build/libconfined-floats.so
```

## Usage

The plugin adds the window rule `confined-floats:confine`. Only matching windows are confined – others move freely.

### Examples

```lua
-- confine firefox windows
hl.window_rule({
    match = {
        class = "^(firefox)$",
    },
    ["confined-floats:confine"] = true,
})
```

```lua
-- confine everything
hl.window_rule({
    match = { class = ".*" },
    ["confined-floats:confine"] = true,
})
```
