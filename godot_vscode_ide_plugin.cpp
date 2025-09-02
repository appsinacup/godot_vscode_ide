/**************************************************************************/
/*  godot_vscode_ide_plugin.cpp                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "godot_vscode_ide_plugin.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "editor/editor_main_screen.h"
#include "editor/editor_node.h"
#include "editor/gui/editor_bottom_panel.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/control.h"
#include "scene/gui/label.h"
#ifdef TOOLS_ENABLED
#include "../gdterm/gdterm/terminal_plugin.h"
#endif

void GodotIDEPlugin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_refresh_webview"), &GodotIDEPlugin::_refresh_webview);
	ClassDB::bind_method(D_METHOD("_update_url_from_settings"), &GodotIDEPlugin::_update_url_from_settings);
}

GodotIDEPlugin::GodotIDEPlugin() {
	// Don't initialize UI components in doctool mode
	if (!EditorNode::get_singleton() || !EditorNode::get_singleton()->get_editor_main_screen()) {
		print_line("GodotIDEPlugin: Skipping UI initialization - not in full editor mode");
		holder = nullptr;
		web_view = nullptr;
		loaded = false;
		tunnel_started = false;
		return;
	}

	// Set up project settings
	if (!ProjectSettings::get_singleton()->has_setting("editor/ide/vscode_url")) {
		ProjectSettings::get_singleton()->set_setting("editor/ide/vscode_url", "https://vscode.dev");
	}

	// Save settings to make them persistent
	ProjectSettings::get_singleton()->save();

	holder = memnew(Control);
	ERR_FAIL_COND_MSG(!ClassDB::class_exists("WebView"), "WebView class not found - godot_wry module not properly loaded");
	web_view = Object::cast_to<Control>(ClassDB::instantiate("WebView"));
	ERR_FAIL_NULL_MSG(web_view, "Failed to instantiate WebView");
	holder->add_child(web_view);
	web_view->set_name("IDE");

	// Load settings and configure WebView
	_update_url_from_settings();

	web_view->call("set_transparent", true);
	web_view->call("set_zoom_hotkeys", true);
	web_view->call("set_full_window_size", false);

	holder->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	EditorNode::get_singleton()->get_editor_main_screen()->get_control()->add_child(holder);
	holder->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	web_view->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	holder->hide();
	_start_code_tunnel();

	// Initialize tunnel state
	tunnel_started = false;
}

GodotIDEPlugin::~GodotIDEPlugin() {
	if (holder) {
		holder->queue_free();
		holder = nullptr;
		web_view = nullptr;
	}
}

void GodotIDEPlugin::make_visible(bool p_visible) {
	if (!holder) {
		// Plugin wasn't properly initialized (probably doctool mode)
		return;
	}

	// Check if URL changed since last time
	_update_url_from_settings();

	if (!loaded) {
		loaded = true;
		web_view->call("create_webview");
	}

	if (holder) {
		holder->set_visible(p_visible);
	} else {
		ERR_PRINT("web_view is null!");
	}
}

void GodotIDEPlugin::_refresh_webview() {
	if (web_view && loaded) {
		web_view->call("reload");
	}
}

void GodotIDEPlugin::_update_url_from_settings() {
	if (!web_view) {
		// Plugin wasn't properly initialized
		return;
	}

	String new_url = ProjectSettings::get_singleton()->get_setting("editor/ide/vscode_url", "https://vscode.dev");

	if (current_url != new_url) {
		current_url = new_url;
		if (web_view) {
			web_view->call("set_url", current_url);
			if (loaded) {
				// If webview is already loaded, navigate to the new URL
				web_view->call("load_url", current_url);
			}
		}
	}
}

void GodotIDEPlugin::_start_code_tunnel() {
#ifdef TOOLS_ENABLED
	// Only start tunnel once
	if (tunnel_started) {
		return;
	}

	// Get the terminal plugin singleton
	TerminalPlugin *terminal_plugin = TerminalPlugin::get_singleton();

	if (!terminal_plugin) {
		ERR_PRINT("Terminal plugin not found. Please make sure the GDTerm plugin is enabled.");
		return;
	}

	// Check if there's already a VSCode Tunnel tab
	for (int i = 0; i < terminal_plugin->get_tab_count(); i++) {
		String tab_name = terminal_plugin->get_tab_name(i);
		if (tab_name.contains("VSCode") || tab_name.contains("Tunnel")) {
			// Already exists, just run the command in this tab
			bool success = terminal_plugin->run_command_in_tab(i, "code tunnel");
			if (success) {
				tunnel_started = true;
			}
			return;
		}
	}

	// Create a new tab and run the command
	int tab_index = terminal_plugin->add_terminal_tab("VSCode Tunnel");
	bool success = terminal_plugin->run_command_in_tab(tab_index, "code tunnel");

	if (success) {
		tunnel_started = true;
	} else {
		ERR_PRINT("Failed to run VS Code tunnel command in terminal");
	}
#else
	ERR_PRINT("Terminal plugin not available in non-editor builds");
#endif
}

const Ref<Texture2D> GodotIDEPlugin::get_plugin_icon() const {
	if (!EditorNode::get_singleton()) {
		return Ref<Texture2D>();
	}

	Ref<Theme> theme = EditorNode::get_singleton()->get_editor_theme();
	if (!theme.is_valid()) {
		return Ref<Texture2D>();
	}

	return theme->get_icon(SNAME("Script"), SNAME("EditorIcons"));
}
