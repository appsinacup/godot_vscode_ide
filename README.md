<p align="center">
    <h1 align="center">Godot VSCode</h1> 

![example](./docs/example.png)
</p>

|[Website](https://appsinacup.com)|[Discord](https://discord.gg/56dMud8HYn)
|-|-|

<p align="center">
        <img src="https://img.shields.io/badge/Godot-4.2-%23478cbf?logo=godot-engine&logoColor=white" />
</p>

-----

<p align = "center">
<b>VSCode</b> running <i>inside</i> the Godot game engine.</i>
</p>

-----

# Godot VSCode

This repo embeds VSCode editor to the Godot Engine as an addon through a webview.

## How to install

Download addons folder (Download Zip option from GitHub) and put in your project. You can also install from Godot Asset Library [Godot VSCode](https://godotengine.org/asset-library/asset/4747).

Then, enable it by going to `Project Settings` -> `Plugins` -> `Godot VSCode` (Enabled ON).

## Star History

[![Star History Chart](https://api.star-history.com/svg?repos=appsinacup/godot_vscode_ide&type=date&legend=top-left)](https://www.star-history.com/#appsinacup/godot_vscode_ide&type=date&legend=top-left)

## Setup

There are 2 ways to use the editor:
- **Tunnel (Recommended)**: For this, make sure the `Project -> Project Settings -> editor/ide/auto_start_tunnel` property is on. The tunnel will start automatically in the **Terminal Tab**. After you get an URL for it (From the terminal tab), simply copy that URL and put it in the: `Project -> Project Settings -> editor/ide/vscode_url`. Now it will always connect automatically to the tunnel (locally). You may need to authenticate with **GitHub**.
- **External Folder**: For this, simply load an external folder in Code.

If you want to hide the old `Script` editor, go to:


- Editor ->
    - Manage Editor Features... ->
        - (Disable) Script Editor

Note:

    This will hide the previous Script editor in favour of the new `Code` based one.

## How it works?

It creates a Webview node and loads the website `https://vscode.dev`. Then, it creates a tunnel from your local vscode by running `code tunnel` (automatically) and connects to that.

[![VSCode Presentation](https://img.youtube.com/vi/CZbAj8Zv41E/0.jpg)](https://www.youtube.com/watch?v=CZbAj8Zv41E)

## Project Settings

The plugin adds the following project settings under `editor/ide/`:

- **`vscode_url`** (String, default: `"https://vscode.dev"`): The URL to load in the VSCode webview
- **`auto_start_tunnel`** (bool, default: `true`): Whether to automatically start the VSCode tunnel when the editor starts

You can disable the auto-start tunnel and manually start it using "Project → Tools → Start VSCode tunnel" from the menu.

## Dependencies

- [appsinacup/godot_wry](https://github.com/appsinacup/godot_wry): Fork of godot_wry (Webview component) that fixes some sizing issue, adds option to open new tab, drag-and-drop handler, updates to latest version of wry, etc.
