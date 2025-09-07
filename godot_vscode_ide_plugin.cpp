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
#include "core/io/json.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "editor/editor_data.h"
#include "editor/editor_interface.h"
#include "editor/editor_main_screen.h"
#include "editor/editor_node.h"
#include "editor/docks/inspector_dock.h"
#include "editor/docks/scene_tree_dock.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/scene/scene_tree_editor.h"
#include "editor/gui/editor_bottom_panel.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/control.h"
#include "scene/gui/label.h"
#include "core/object/script_language.h"
#ifdef TOOLS_ENABLED
#include "../gdterm/gdterm/terminal_plugin.h"
#endif

void GodotIDEPlugin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_refresh_webview"), &GodotIDEPlugin::_refresh_webview);
	ClassDB::bind_method(D_METHOD("_refresh_all_webviews"), &GodotIDEPlugin::_refresh_all_webviews);
	ClassDB::bind_method(D_METHOD("_update_url_from_settings"), &GodotIDEPlugin::_update_url_from_settings);
	ClassDB::bind_method(D_METHOD("_on_ipc_message_main", "message"), &GodotIDEPlugin::_on_ipc_message_main);
	ClassDB::bind_method(D_METHOD("_on_ipc_message_bottom", "message"), &GodotIDEPlugin::_on_ipc_message_bottom);
	ClassDB::bind_method(D_METHOD("_on_resource_selected", "p_res", "p_property"), &GodotIDEPlugin::_on_resource_selected);
	ClassDB::bind_method(D_METHOD("_on_script_open_request", "p_script"), &GodotIDEPlugin::_on_script_open_request);
	ClassDB::bind_method(D_METHOD("_toggle_bottom_panel"), &GodotIDEPlugin::_toggle_bottom_panel);
	ClassDB::bind_method(D_METHOD("_open_dev_tools"), &GodotIDEPlugin::_open_dev_tools);
}

GodotIDEPlugin::GodotIDEPlugin() {
	main_screen_web_view = nullptr;
	bottom_panel_holder = nullptr;
	bottom_panel_web_view = nullptr;
	bottom_panel_button = nullptr;
	main_loaded = false;
	bottom_loaded = false;
	bottom_panel_enabled = false;
	current_url = "";

	if (!EditorNode::get_singleton() || !EditorNode::get_singleton()->get_editor_main_screen()) {
		return;
	}

	if (!ProjectSettings::get_singleton()) {
		return;
	}

	if (!ClassDB::class_exists("WebView")) {
		ERR_PRINT_ONCE("GodotIDEPlugin: WebView class not found - godot_wry module not properly loaded");
		return;
	}

	if (!ProjectSettings::get_singleton()->has_setting("editor/ide/vscode_url")) {
		ProjectSettings::get_singleton()->set_setting("editor/ide/vscode_url", "https://vscode.dev");
	}
	if (!ProjectSettings::get_singleton()->has_setting("editor/ide/bottom_panel_enabled")) {
		ProjectSettings::get_singleton()->set_setting("editor/ide/bottom_panel_enabled", false);
	}
	if (!ProjectSettings::get_singleton()->has_setting("editor/ide/auto_start_tunnel")) {
		ProjectSettings::get_singleton()->set_setting("editor/ide/auto_start_tunnel", true);
	}

	ProjectSettings::get_singleton()->save();

	main_screen_web_view = Object::cast_to<Control>(ClassDB::instantiate("WebView"));
	if (!main_screen_web_view) {
		ERR_PRINT_ONCE("GodotIDEPlugin: Failed to instantiate WebView");
		return;
	}
	
	main_screen_web_view->set_name("IDE");

	_update_url_from_settings();

	main_screen_web_view->call("set_transparent", true);
	main_screen_web_view->call("set_zoom_hotkeys", true);
	main_screen_web_view->call("set_full_window_size", false);

	main_screen_web_view->set_focus_behavior_recursive(Control::FOCUS_BEHAVIOR_ENABLED);
	main_screen_web_view->set_focus_mode(Control::FOCUS_ALL);

	// Connect IPC message signal for focus management
	if (main_screen_web_view->has_signal("ipc_message")) {
		main_screen_web_view->connect("ipc_message", callable_mp(this, &GodotIDEPlugin::_on_ipc_message_main));
	}

	main_screen_web_view->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	EditorNode::get_singleton()->get_editor_main_screen()->get_control()->add_child(main_screen_web_view);
	main_screen_web_view->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	main_screen_web_view->hide();

	bottom_panel_enabled = ProjectSettings::get_singleton()->get_setting("editor/ide/bottom_panel_enabled", false);
	bottom_panel_holder = nullptr;
	bottom_panel_web_view = nullptr;
	bottom_panel_button = nullptr;

	if (bottom_panel_enabled) {
		_create_bottom_panel_webview();
	}

	if (EditorNode::get_singleton()) {
		add_tool_menu_item("Refresh all webviews", callable_mp(this, &GodotIDEPlugin::_refresh_all_webviews));
		add_tool_menu_item("Toggle VSCode bottom panel", callable_mp(this, &GodotIDEPlugin::_toggle_bottom_panel));
		add_tool_menu_item("Start VSCode tunnel", callable_mp(this, &GodotIDEPlugin::_start_code_tunnel));
		add_tool_menu_item("Open developer tools", callable_mp(this, &GodotIDEPlugin::_open_dev_tools));
		
		// Connect to resource selection signal from EditorInspector
		EditorInterface *editor_interface = EditorInterface::get_singleton();
		if (editor_interface) {
			EditorInspector *inspector = InspectorDock::get_inspector_singleton();
			if (inspector && inspector->has_signal("resource_selected")) {
				inspector->connect("resource_selected", callable_mp(this, &GodotIDEPlugin::_on_resource_selected));
			}
		}
		
		// Connect to script open signal from SceneTreeEditor
		if (SceneTreeDock::get_singleton() && SceneTreeDock::get_singleton()->get_tree_editor()) {
			SceneTreeEditor *scene_tree_editor = SceneTreeDock::get_singleton()->get_tree_editor();
			if (scene_tree_editor && scene_tree_editor->has_signal("open_script")) {
				scene_tree_editor->connect("open_script", callable_mp(this, &GodotIDEPlugin::_on_script_open_request));
			}
		}
		
		fully_initialized = true;
	} else {
		fully_initialized = false;
	}
	
	_start_code_tunnel_internal(true);
}

GodotIDEPlugin::~GodotIDEPlugin() {
	if (fully_initialized && EditorNode::get_singleton()) {
		remove_tool_menu_item("Refresh all webviews");
		remove_tool_menu_item("Toggle VSCode bottom panel");
		remove_tool_menu_item("Start VSCode tunnel");
		remove_tool_menu_item("Open developer tools");
	}

	if (main_screen_web_view) {
		main_screen_web_view->queue_free();
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
	if (!main_screen_web_view) {
		return;
	}

	_update_url_from_settings();

	if (!main_loaded) {
		main_loaded = true;
		main_screen_web_view->call("create_webview");
	}

	if (main_screen_web_view) {
		main_screen_web_view->set_visible(p_visible);
		main_screen_web_view->grab_click_focus();
		main_screen_web_view->grab_focus();
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
	if (main_screen_web_view && main_loaded) {
		main_screen_web_view->call("reload");
	}

	if (bottom_panel_web_view && bottom_loaded) {
		bottom_panel_web_view->call("reload");
	}
}

void GodotIDEPlugin::_on_ipc_message_main(const String &message) {
	if (main_screen_web_view && main_loaded) {
		main_screen_web_view->grab_click_focus();
		main_screen_web_view->grab_focus();
	}
}

void GodotIDEPlugin::_on_ipc_message_bottom(const String &message) {
	if (bottom_loaded && bottom_panel_web_view) {
		bottom_panel_web_view->call_deferred("grab_focus");
		bottom_panel_web_view->call_deferred("grab_click_focus");
	}
}

void GodotIDEPlugin::_on_resource_selected(const Ref<Resource> &p_res, const String &p_property) {
	// Check if the selected resource is a script
	Ref<Script> script = Object::cast_to<Script>(p_res.ptr());
	if (script.is_valid()) {
		String script_path = script->get_path();
		if (!script_path.is_empty()) {
			_open_script_in_vscode(script_path);
		}
	}
}

void GodotIDEPlugin::_on_script_open_request(const Ref<Script> &p_script) {
	if (p_script.is_valid()) {
		String script_path = p_script->get_path();
		if (!script_path.is_empty()) {
			_open_script_in_vscode(script_path);
		}
	}
}

void GodotIDEPlugin::_open_script_in_vscode(const String &script_path) {
	print_line("Attempting to open script in VSCode: " + script_path);
	if (!main_screen_web_view || script_path.is_empty()) {
		return;
	}
	
	// Convert the script path to a format that VSCode can understand
	String project_path = ProjectSettings::get_singleton()->globalize_path("res://");
	String full_script_path = ProjectSettings::get_singleton()->globalize_path(script_path);
	
	// Create a message to send to the VSCode webview to open the file
	Dictionary message;
	message["type"] = "open_file";
	message["path"] = full_script_path;
	message["project_path"] = project_path;
	
	// Convert to JSON and send via IPC
	String json_message = JSON::stringify(message);
	main_screen_web_view->call("run_javascript", "window.postMessage(" + json_message + ", '*');");
	// print_line("Sent message to VSCode webview: " + json_message);
}

void GodotIDEPlugin::_update_url_from_settings() {
	if (!main_screen_web_view || !ProjectSettings::get_singleton()) {
		return;
	}

	String new_url = ProjectSettings::get_singleton()->get_setting("editor/ide/vscode_url", "https://vscode.dev");

	if (current_url != new_url) {
		current_url = new_url;
		
		if (main_screen_web_view) {
			main_screen_web_view->call("set_url", current_url);
			if (main_loaded) {
				main_screen_web_view->call("load_url", current_url);
			}
		}
		
		if (bottom_panel_web_view) {
			bottom_panel_web_view->call("set_url", current_url);
			if (bottom_loaded) {
				bottom_panel_web_view->call("load_url", current_url);
			}
		}
	}
}

void GodotIDEPlugin::_start_code_tunnel() {
	if (!fully_initialized) {
		return;
	}
	_start_code_tunnel_internal(false);
}

void GodotIDEPlugin::_start_code_tunnel_internal(bool auto_start_only) {
#ifdef TOOLS_ENABLED
	if (auto_start_only) {
		bool auto_start = ProjectSettings::get_singleton()->get_setting("editor/ide/auto_start_tunnel", true);
		if (!auto_start) {
			return;
		}
	}

	TerminalPlugin *terminal_plugin = TerminalPlugin::get_singleton();

	if (!terminal_plugin) {
		ERR_PRINT("Terminal plugin not found. Please make sure the GDTerm plugin is enabled.");
		return;
	}

	for (int i = 0; i < terminal_plugin->get_tab_count(); i++) {
		String tab_name = terminal_plugin->get_tab_name(i);
		if (tab_name.contains("VSCode") || tab_name.contains("Tunnel")) {
			terminal_plugin->run_command_in_tab(i, "code tunnel --accept-server-license-terms");
			return;
		}
	}

	int tab_index = terminal_plugin->add_terminal_tab("VSCode Tunnel");
	terminal_plugin->run_command_in_tab(tab_index, "code tunnel --accept-server-license-terms");
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
	
	ProjectSettings::get_singleton()->set_setting("editor/ide/bottom_panel_enabled", bottom_panel_enabled);
	ProjectSettings::get_singleton()->save();
	
	if (bottom_panel_enabled) {
		_create_bottom_panel_webview();
	} else {
		_destroy_bottom_panel_webview();
	}
}

void GodotIDEPlugin::_create_bottom_panel_webview() {
	if (bottom_panel_holder) {
		return;
	}
	
	bottom_panel_holder = memnew(Control);
	ERR_FAIL_COND_MSG(!ClassDB::class_exists("WebView"), "WebView class not found - godot_wry module not properly loaded");
	bottom_panel_web_view = Object::cast_to<Control>(ClassDB::instantiate("WebView"));
	ERR_FAIL_NULL_MSG(bottom_panel_web_view, "Failed to instantiate WebView for bottom panel");
	
	bottom_panel_holder->add_child(bottom_panel_web_view);
	bottom_panel_web_view->set_name("VSCode Bottom Panel");
	
	bottom_panel_web_view->call("set_url", current_url);
	bottom_panel_web_view->call("set_transparent", true);
	bottom_panel_web_view->call("set_zoom_hotkeys", true);
	bottom_panel_web_view->call("set_full_window_size", false);
	
	bottom_panel_web_view->set_focus_mode(Control::FOCUS_ALL);
	bottom_panel_web_view->set_focus_behavior_recursive(Control::FOCUS_BEHAVIOR_ENABLED);
	
	// Connect IPC message signal for focus management
	if (bottom_panel_web_view->has_signal("ipc_message")) {
		bottom_panel_web_view->connect("ipc_message", callable_mp(this, &GodotIDEPlugin::_on_ipc_message_bottom));
	}
	
	bottom_panel_holder->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	bottom_panel_web_view->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	bottom_panel_holder->set_custom_minimum_size(Size2(0, 300) * EDSCALE);
	
	bottom_panel_button = add_control_to_bottom_panel(bottom_panel_holder, "VSCode");
	
	bottom_panel_web_view->call("create_webview");
	bottom_loaded = true;
}

void GodotIDEPlugin::_destroy_bottom_panel_webview() {
	if (!bottom_panel_holder) {
		return;
	}

	remove_control_from_bottom_panel(bottom_panel_holder);
	
	bottom_panel_holder->queue_free();
	bottom_panel_holder = nullptr;
	bottom_panel_web_view = nullptr;
	bottom_panel_button = nullptr;
	bottom_loaded = false;
}

void GodotIDEPlugin::_open_dev_tools() {
	if (!main_screen_web_view) {
		ERR_PRINT("VSCode IDE: Main screen webview not available");
		return;
	}
	
	if (!main_loaded) {
		ERR_PRINT("VSCode IDE: Main screen webview not loaded yet");
		return;
	}
	
	main_screen_web_view->call("open_devtools");
}
