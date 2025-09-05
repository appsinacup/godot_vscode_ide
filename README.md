# Godot VSCode IDE

![example](./docs/example.png)

This repo embeds VSCode editor to the Godot Engine as a module (for now).

## How it works?

It creates a Webview node and loads the website `https://vscode.dev`. Then, it creates a tunnel from your local vscode by running `code tunnel` (automatically) and connects to that.

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

## Alternatives

There are also other addons that do similar thing to this:
- [Jenova Code IDE](https://github.com/Jenova-Framework/J.E.N.O.V.A): A full-spectrum integrated development environment for Godot built on VSCode Core and engineered for deep integration and modular control (Windows only, needs Jenova)
- [RedMser/godot-embed-external-editor](https://github.com/RedMser/godot-embed-external-editor): Embed an external script editor (e.g. VSCode) into the Godot editor
