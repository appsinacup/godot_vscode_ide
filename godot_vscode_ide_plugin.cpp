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
	ClassDB::bind_method(D_METHOD("_refresh_all_webviews"), &GodotIDEPlugin::_refresh_all_webviews);
	ClassDB::bind_method(D_METHOD("_update_url_from_settings"), &GodotIDEPlugin::_update_url_from_settings);
	ClassDB::bind_method(D_METHOD("_toggle_bottom_panel"), &GodotIDEPlugin::_toggle_bottom_panel);
	ClassDB::bind_method(D_METHOD("_open_dev_tools"), &GodotIDEPlugin::_open_dev_tools);
}

GodotIDEPlugin::GodotIDEPlugin() {
	// Initialize all pointers to nullptr first
	main_screen_holder = nullptr;
	main_screen_web_view = nullptr;
	bottom_panel_holder = nullptr;
	bottom_panel_web_view = nullptr;
	bottom_panel_button = nullptr;
	main_loaded = false;
	bottom_loaded = false;
	tunnel_started = false;
	bottom_panel_enabled = false;
	current_url = "";

	// Don't initialize UI components in doctool mode or when editor isn't ready
	if (!EditorNode::get_singleton() || !EditorNode::get_singleton()->get_editor_main_screen()) {
		print_line("GodotIDEPlugin: Skipping UI initialization - not in full editor mode");
		return;
	}

	// Additional safety check for ProjectSettings
	if (!ProjectSettings::get_singleton()) {
		print_line("GodotIDEPlugin: ProjectSettings not available");
		return;
	}

	// Check if WebView class exists before trying to use it
	if (!ClassDB::class_exists("WebView")) {
		print_line("GodotIDEPlugin: WebView class not found - godot_wry module not properly loaded");
		return;
	}

	// Set up project settings
	if (!ProjectSettings::get_singleton()->has_setting("editor/ide/vscode_url")) {
		ProjectSettings::get_singleton()->set_setting("editor/ide/vscode_url", "https://vscode.dev");
	}
	if (!ProjectSettings::get_singleton()->has_setting("editor/ide/bottom_panel_enabled")) {
		ProjectSettings::get_singleton()->set_setting("editor/ide/bottom_panel_enabled", false);
	}

	// Save settings to make them persistent
	ProjectSettings::get_singleton()->save();

	// Initialize main screen webview
	main_screen_holder = memnew(Control);
	main_screen_web_view = Object::cast_to<Control>(ClassDB::instantiate("WebView"));
	if (!main_screen_web_view) {
		print_line("GodotIDEPlugin: Failed to instantiate WebView");
		if (main_screen_holder) {
			main_screen_holder->queue_free();
			main_screen_holder = nullptr;
		}
		return;
	}
	
	main_screen_holder->add_child(main_screen_web_view);
	main_screen_web_view->set_name("IDE");

	// Load settings and configure main WebView
	_update_url_from_settings();

	main_screen_web_view->call("set_transparent", true);
	main_screen_web_view->call("set_zoom_hotkeys", true);
	main_screen_web_view->call("set_full_window_size", false);

	main_screen_holder->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	EditorNode::get_singleton()->get_editor_main_screen()->get_control()->add_child(main_screen_holder);
	main_screen_holder->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	main_screen_web_view->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	main_screen_holder->hide();

	// Initialize bottom panel state
	bottom_panel_enabled = ProjectSettings::get_singleton()->get_setting("editor/ide/bottom_panel_enabled", false);
	bottom_panel_holder = nullptr;
	bottom_panel_web_view = nullptr;
	bottom_panel_button = nullptr;

	// Create bottom panel if enabled
	if (bottom_panel_enabled) {
		_create_bottom_panel_webview();
	}

	// Add menu items to Project -> Tools
	add_tool_menu_item("Refresh all webviews", callable_mp(this, &GodotIDEPlugin::_refresh_all_webviews));
	add_tool_menu_item("Toggle VSCode bottom panel", callable_mp(this, &GodotIDEPlugin::_toggle_bottom_panel));
	add_tool_menu_item("Open developer tools", callable_mp(this, &GodotIDEPlugin::_open_dev_tools));
	
	// Mark plugin as fully initialized
	fully_initialized = true;
	
	_start_code_tunnel();

	// Initialize tunnel state
	tunnel_started = false;
}

GodotIDEPlugin::~GodotIDEPlugin() {
	// Remove Project -> Tools menu items only if fully initialized
	if (fully_initialized) {
		remove_tool_menu_item("Refresh all webviews");
		remove_tool_menu_item("Toggle VSCode bottom panel");
		remove_tool_menu_item("Open developer tools");
	}

	if (main_screen_holder) {
		main_screen_holder->queue_free();
		main_screen_holder = nullptr;
		main_screen_web_view = nullptr;
	}

	if (bottom_panel_holder) {
		remove_control_from_bottom_panel(bottom_panel_holder);
		bottom_panel_holder->queue_free();
		bottom_panel_holder = nullptr;
		bottom_panel_web_view = nullptr;
		bottom_panel_button = nullptr;
	}
}

void GodotIDEPlugin::make_visible(bool p_visible) {
	if (!main_screen_holder) {
		// Plugin wasn't properly initialized (probably doctool mode)
		return;
	}

	// Check if URL changed since last time
	_update_url_from_settings();

	if (!main_loaded) {
		main_loaded = true;
		main_screen_web_view->call("create_webview");
	}

	if (main_screen_holder) {
		main_screen_holder->set_visible(p_visible);
	} else {
		ERR_PRINT("main_screen_web_view is null!");
	}
}

void GodotIDEPlugin::_refresh_webview() {
	if (main_screen_web_view && main_loaded) {
		main_screen_web_view->call("reload");
	}
}

void GodotIDEPlugin::_refresh_all_webviews() {
	print_line("VSCode IDE: Refreshing all webviews and browsers");
	
	// Refresh main screen webview
	if (main_screen_web_view && main_loaded) {
		main_screen_web_view->call("reload");
		print_line("VSCode IDE: Refreshed main screen webview");
	}

	// Refresh bottom panel webview
	if (bottom_panel_web_view && bottom_loaded) {
		bottom_panel_web_view->call("reload");
		print_line("VSCode IDE: Refreshed bottom panel webview");
	}

	print_line("VSCode IDE: All webviews refreshed");
}

void GodotIDEPlugin::_update_url_from_settings() {
	// Additional safety checks
	if (!main_screen_web_view || !ProjectSettings::get_singleton()) {
		// Plugin wasn't properly initialized or ProjectSettings not available
		return;
	}

	String new_url = ProjectSettings::get_singleton()->get_setting("editor/ide/vscode_url", "https://vscode.dev");

	if (current_url != new_url) {
		current_url = new_url;
		
		// Update main screen webview
		if (main_screen_web_view) {
			main_screen_web_view->call("set_url", current_url);
			if (main_loaded) {
				// If webview is already loaded, navigate to the new URL
				main_screen_web_view->call("load_url", current_url);
			}
		}
		
		// Update bottom panel webview if it exists
		if (bottom_panel_web_view) {
			bottom_panel_web_view->call("set_url", current_url);
			if (bottom_loaded) {
				// If webview is already loaded, navigate to the new URL
				bottom_panel_web_view->call("load_url", current_url);
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

void GodotIDEPlugin::_toggle_bottom_panel() {
	bottom_panel_enabled = !bottom_panel_enabled;
	
	// Save the setting
	ProjectSettings::get_singleton()->set_setting("editor/ide/bottom_panel_enabled", bottom_panel_enabled);
	ProjectSettings::get_singleton()->save();
	
	if (bottom_panel_enabled) {
		print_line("VSCode IDE: Enabling bottom panel");
		_create_bottom_panel_webview();
	} else {
		print_line("VSCode IDE: Disabling bottom panel");
		_destroy_bottom_panel_webview();
	}
}

void GodotIDEPlugin::_create_bottom_panel_webview() {
	if (bottom_panel_holder) {
		// Already exists
		return;
	}
	
	print_line("VSCode IDE: Creating bottom panel webview");
	
	// Create bottom panel webview
	bottom_panel_holder = memnew(Control);
	ERR_FAIL_COND_MSG(!ClassDB::class_exists("WebView"), "WebView class not found - godot_wry module not properly loaded");
	bottom_panel_web_view = Object::cast_to<Control>(ClassDB::instantiate("WebView"));
	ERR_FAIL_NULL_MSG(bottom_panel_web_view, "Failed to instantiate WebView for bottom panel");
	
	bottom_panel_holder->add_child(bottom_panel_web_view);
	bottom_panel_web_view->set_name("VSCode Bottom Panel");
	
	// Configure bottom panel webview
	bottom_panel_web_view->call("set_url", current_url);
	bottom_panel_web_view->call("set_transparent", true);
	bottom_panel_web_view->call("set_zoom_hotkeys", true);
	bottom_panel_web_view->call("set_full_window_size", false);
	
	bottom_panel_holder->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	bottom_panel_web_view->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	bottom_panel_holder->set_custom_minimum_size(Size2(0, 300) * EDSCALE);
	
	// Add to bottom panel
	bottom_panel_button = add_control_to_bottom_panel(bottom_panel_holder, "VSCode");
	
	// Create the webview 
	bottom_panel_web_view->call("create_webview");
	bottom_loaded = true;
	
	print_line("VSCode IDE: Bottom panel webview created successfully");
}

void GodotIDEPlugin::_destroy_bottom_panel_webview() {
	if (!bottom_panel_holder) {
		// Doesn't exist
		return;
	}
	
	print_line("VSCode IDE: Destroying bottom panel webview");
	
	// Remove from bottom panel
	remove_control_from_bottom_panel(bottom_panel_holder);
	
	// Clean up
	bottom_panel_holder->queue_free();
	bottom_panel_holder = nullptr;
	bottom_panel_web_view = nullptr;
	bottom_panel_button = nullptr;
	bottom_loaded = false;
	
	print_line("VSCode IDE: Bottom panel webview destroyed");
}

void GodotIDEPlugin::_open_dev_tools() {
	print_line("VSCode IDE: Opening developer tools for main screen webview");
	
	if (!main_screen_web_view) {
		ERR_PRINT("VSCode IDE: Main screen webview not available");
		return;
	}
	
	if (!main_loaded) {
		ERR_PRINT("VSCode IDE: Main screen webview not loaded yet");
		return;
	}
	
	// Use the correct method name from godot_wry module
	if (main_screen_web_view->has_method("open_devtools")) {
		main_screen_web_view->call("open_devtools");
		print_line("VSCode IDE: Developer tools opened successfully");
	} else {
		print_line("VSCode IDE: open_devtools() method not found in WebView class");
		print_line("VSCode IDE: Available WebView methods:");
		
		// Try to get a list of available methods for debugging
		List<MethodInfo> methods;
		main_screen_web_view->get_method_list(&methods);
		for (const MethodInfo &method : methods) {
			if (method.name.begins_with("dev") || method.name.begins_with("debug") || method.name.begins_with("inspect") || method.name.begins_with("open")) {
				print_line("VSCode IDE: Found method: " + method.name);
			}
		}
	}
}
