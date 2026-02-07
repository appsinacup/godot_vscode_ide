<p align="center">
    <h1 align="center">Godot VSCode</h1> 

![example](./docs/example.png)
</p>

|[Website](https://appsinacup.com)|[Discord](https://discord.gg/56dMud8HYn)
|-|-|

<p align="center">
        <img src="https://img.shields.io/badge/Godot-4.4.1-%23478cbf?logo=godot-engine&logoColor=white" />
</p>

-----

<p align = "center">
<b>VSCode</b> running <i>inside</i> the Godot game engine.</i>
</p>

-----

# Godot VSCode

This repo embeds VSCode editor to the Godot Engine as a module through a webview. In future, it will also have a gdextension variant.

**TODO**:
- open file when clicking in editor
- fix build for linux

## Setup

There are 2 ways to use the editor:
- **Tunnel (Recommended)**: For this, make sure the `Project -> Project Settings -> editor/ide/auto_start_tunnel` property is on. The tunnel will start automatically in the **Terminal Tab**. After you get an URL for it (From the terminal tab), simply copy that URL and put it in the: `Project -> Project Settings -> editor/ide/vscode_url`. Now it will always connect automatically to the tunnel (locally). You may need to authenticate with **GitHub**.
- **External Folder**: For this, simply load an external folder in vscode.

If you want to hide the old `Script` editor, go to:


- Editor ->
    - Manage Editor Features... ->
        - (Disable) Script Editor

Note:

    This will hide the previous Script editor in favour of the new `VSCode` based one.

## How it works?

It creates a Webview node and loads the website `https://vscode.dev`. Then, it creates a tunnel from your local vscode by running `code tunnel` (automatically) and connects to that.

[![IMAGE ALT TEXT HERE](https://img.youtube.com/vi/CZbAj8Zv41E/0.jpg)](https://www.youtube.com/watch?v=CZbAj8Zv41E)

## Project Settings

The plugin adds the following project settings under `editor/ide/`:

- **`vscode_url`** (String, default: `"https://vscode.dev"`): The URL to load in the VSCode webview
- **`bottom_panel_enabled`** (bool, default: `false`): Whether to show VSCode in a bottom panel in addition to the main screen
- **`auto_start_tunnel`** (bool, default: `true`): Whether to automatically start the VSCode tunnel when the editor starts

You can disable the auto-start tunnel and manually start it using "Project → Tools → Start VSCode tunnel" from the menu.

## Dependencies

- [appsinacup/godot_wry](https://github.com/appsinacup/godot_wry): Fork of godot_wry (Webview component) that fixes some sizing issue.
- [appsinacup/gdterm](https://github.com/appsinacup/gdterm): Fork of gdterm (terminal) that adds more tabs and a singleton for running commands and creating tabs.

## How to install

Download [gonuts](https://github.com/appsinacup/gonuts) and run it.

Or:

Build locally godot as you would after cloning this to the modules folder.

```sh
scons
```

## GDExtension (experimental)

This repository now includes an experimental GDExtension entrypoint and a template config under `addons/godot_vscode_ide/godot_vscode_ide.gdextension`.

- Build a shared library for the extension (for example `libgodot_vscode_ide.dylib` on macOS) using your preferred toolchain (godot-cpp, CMake, or SCons), and place it under `addons/godot_vscode_ide/bin/`.
- Update the paths in `addons/godot_vscode_ide/godot_vscode_ide.gdextension` to point to the built library for your platform.
- In your project, enable the extension by copying the `addons/godot_vscode_ide` folder into the `addons/` directory of the project and opening the project in Godot.

Note: This is a minimal conversion to GDExtension; you may need to adapt the build configuration for your platform and toolchain. Feedback and PRs are welcome.

## GDScript addon (recommended for easy install)

You can also use a pure GDScript addon variant which is easier to install and works without rebuilding Godot.

1. Copy `addons/godot_vscode_ide` into your project's `addons/` folder.
2. Open the project in Godot and enable the plugin at `Project -> Project Settings -> Plugins` (it appears as `Godot VSCode`).
3. The plugin will create a `VSCode` tab in the main editor. The `auto_start_tunnel` option still attempts to run `code tunnel` via `OS.execute` but GDScript cannot capture process stdout for non-blocking processes. If you need automatic URL parsing from the tunnel output, prefer the GDExtension variant.

Note: The GDScript addon attempts to mirror the original module functionality where possible, but some editor-only singletons and process pipes are not available from GDScript. See the `addons/godot_vscode_ide/plugin.gd` for details.
