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
	ClassDB::bind_method(D_METHOD("_on_terminal_output", "text"), &GodotIDEPlugin::_on_terminal_output);
	ClassDB::bind_method(D_METHOD("_extract_vscode_url", "text"), &GodotIDEPlugin::_extract_vscode_url);
	ClassDB::bind_method(D_METHOD("_on_webview_gui_input", "p_event"), &GodotIDEPlugin::_on_webview_gui_input);
	ClassDB::bind_method(D_METHOD("_on_webview_unhandled_input", "p_event"), &GodotIDEPlugin::_on_webview_unhandled_input);
	ClassDB::bind_method(D_METHOD("_on_webview_unhandled_key_input", "p_event"), &GodotIDEPlugin::_on_webview_unhandled_key_input);
	ClassDB::bind_method(D_METHOD("_toggle_bottom_panel"), &GodotIDEPlugin::_toggle_bottom_panel);
	ClassDB::bind_method(D_METHOD("_open_dev_tools"), &GodotIDEPlugin::_open_dev_tools);
}

GodotIDEPlugin::GodotIDEPlugin() {
	main_screen_web_view = nullptr;
	bottom_panel_web_view = nullptr;
	bottom_panel_button = nullptr;
	main_loaded = false;
	bottom_loaded = false;
	bottom_panel_enabled = false;
	distraction_free_enabled_by_us = false;
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
	if (!ProjectSettings::get_singleton()->has_setting("editor/ide/distraction_free_mode")) {
		ProjectSettings::get_singleton()->set_setting("editor/ide/distraction_free_mode", false);
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
	
	// Ensure the WebView captures all mouse and keyboard events
	main_screen_web_view->set_mouse_filter(Control::MOUSE_FILTER_STOP);
	main_screen_web_view->set_clip_contents(true);
	
	// Set higher processing priority to ensure WebView gets events first
	main_screen_web_view->set_process_mode(Node::PROCESS_MODE_ALWAYS);
	
	// Make WebView handle unhandled input to prevent event bubbling
	main_screen_web_view->set_process_unhandled_input(true);
	main_screen_web_view->set_process_unhandled_key_input(true);

	main_screen_web_view->connect("ipc_message", callable_mp(this, &GodotIDEPlugin::_on_ipc_message_main));
	main_screen_web_view->connect("gui_input", callable_mp(this, &GodotIDEPlugin::_on_webview_gui_input));

	main_screen_web_view->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	EditorNode::get_singleton()->get_editor_main_screen()->get_control()->add_child(main_screen_web_view);
	main_screen_web_view->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	main_screen_web_view->hide();

	bottom_panel_enabled = ProjectSettings::get_singleton()->get_setting("editor/ide/bottom_panel_enabled", false);
	bottom_panel_web_view = nullptr;
	bottom_panel_button = nullptr;

	if (bottom_panel_enabled) {
		_create_bottom_panel_webview();
	}

	EditorInterface *editor_interface = EditorInterface::get_singleton();
	if (editor_interface) {
		add_tool_menu_item("Refresh all webviews", callable_mp(this, &GodotIDEPlugin::_refresh_all_webviews));
		add_tool_menu_item("Toggle VSCode bottom panel", callable_mp(this, &GodotIDEPlugin::_toggle_bottom_panel));
		add_tool_menu_item("Start VSCode tunnel", callable_mp(this, &GodotIDEPlugin::_start_code_tunnel));
		add_tool_menu_item("Open developer tools", callable_mp(this, &GodotIDEPlugin::_open_dev_tools));
		
		// Connect to resource selection signal from EditorInspector
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
		ERR_PRINT_ONCE("GodotIDEPlugin: EditorInterface singleton not found");
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

	if (bottom_panel_web_view) {
		remove_control_from_bottom_panel(bottom_panel_web_view);
		bottom_panel_web_view->queue_free();
		bottom_panel_web_view = nullptr;
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
		
		// Should we manage automatic distraction-free mode toggling?
		bool distraction_free_setting = ProjectSettings::get_singleton()->get_setting("editor/ide/distraction_free_mode", false);
		if (distraction_free_setting && EditorInterface::get_singleton()) {
			if (p_visible) {
				// VSCode tab becoming visible
				if (!EditorInterface::get_singleton()->is_distraction_free_mode_enabled()) {
					// Only enable distraction-free mode if it wasn't already enabled
					EditorInterface::get_singleton()->set_distraction_free_mode(true);
					distraction_free_enabled_by_us = true;
				}
			} else {
				// VSCode tab becoming hidden - only disable if we enabled it
				if (distraction_free_enabled_by_us) {
					EditorInterface::get_singleton()->set_distraction_free_mode(false);
					distraction_free_enabled_by_us = false;
				}
			}
		}
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
	Ref<Script> selected_script = Object::cast_to<Script>(p_res.ptr());
	if (selected_script.is_valid()) {
		String script_path = selected_script->get_path();
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

void GodotIDEPlugin::_on_terminal_output(const String &text) {
	_extract_vscode_url(text);
}

void GodotIDEPlugin::_extract_vscode_url(const String &text) {
	// Look for VSCode tunnel URLs in the format: https://vscode.dev/tunnel/...
	if (text.contains("https://vscode.dev/tunnel/")) {
		int start_pos = text.find("https://vscode.dev/tunnel/");
		if (start_pos != -1) {
			// Find the end of the URL (space, newline, or end of string)
			int end_pos = text.length();
			for (int i = start_pos; i < text.length(); i++) {
				char32_t c = text[i];
				if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
					end_pos = i;
					break;
				}
			}
			
			String url = text.substr(start_pos, end_pos - start_pos);
			if (!url.is_empty()) {
				// Update the project setting with the detected URL
				ProjectSettings::get_singleton()->set_setting("editor/ide/vscode_url", url);
				ProjectSettings::get_singleton()->save();
				
				// Update the webview URL
				_update_url_from_settings();
			}
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
	// TODO call into vscode extension
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

	// Check if there's already a VSCode tunnel terminal
	for (int i = 0; i < terminal_plugin->get_tab_count(); i++) {
		String tab_name = terminal_plugin->get_tab_name(i);
		if (tab_name.contains("VSCode") || tab_name.contains("Tunnel")) {
			// Connect to this terminal's text output
			GDTerm *terminal = terminal_plugin->get_terminal(i);
			if (terminal && terminal->has_signal("text_output")) {
				// Make sure we're not already connected
				if (!terminal->is_connected("text_output", callable_mp(this, &GodotIDEPlugin::_on_terminal_output))) {
					terminal->connect("text_output", callable_mp(this, &GodotIDEPlugin::_on_terminal_output));
				}
			}
			terminal_plugin->run_command_in_tab(i, "code tunnel --accept-server-license-terms");
			return;
		}
	}

	// Create new terminal and connect to its output
	int tab_index = terminal_plugin->add_terminal_tab("VSCode Tunnel");
	GDTerm *terminal = terminal_plugin->get_terminal(tab_index);
	if (terminal && terminal->has_signal("text_output")) {
		terminal->connect("text_output", callable_mp(this, &GodotIDEPlugin::_on_terminal_output));
	}
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
	if (bottom_panel_web_view) {
		return;
	}
	
	ERR_FAIL_COND_MSG(!ClassDB::class_exists("WebView"), "WebView class not found - godot_wry module not properly loaded");
	bottom_panel_web_view = Object::cast_to<Control>(ClassDB::instantiate("WebView"));
	ERR_FAIL_NULL_MSG(bottom_panel_web_view, "Failed to instantiate WebView for bottom panel");
	
	bottom_panel_web_view->set_name("VSCode Bottom Panel");
	
	bottom_panel_web_view->call("set_url", current_url);
	bottom_panel_web_view->call("set_transparent", true);
	bottom_panel_web_view->call("set_zoom_hotkeys", true);
	bottom_panel_web_view->call("set_full_window_size", false);
	
	bottom_panel_web_view->set_focus_mode(Control::FOCUS_ALL);
	bottom_panel_web_view->set_focus_behavior_recursive(Control::FOCUS_BEHAVIOR_ENABLED);
	
	// Ensure the bottom panel WebView also captures events properly
	bottom_panel_web_view->set_mouse_filter(Control::MOUSE_FILTER_STOP);
	bottom_panel_web_view->set_clip_contents(true);
	bottom_panel_web_view->set_process_mode(Node::PROCESS_MODE_ALWAYS);
	
	// Make bottom panel WebView handle unhandled input to prevent event bubbling
	bottom_panel_web_view->set_process_unhandled_input(true);
	bottom_panel_web_view->set_process_unhandled_key_input(true);
	
	// Connect IPC message signal for focus management
	if (bottom_panel_web_view->has_signal("ipc_message")) {
		bottom_panel_web_view->connect("ipc_message", callable_mp(this, &GodotIDEPlugin::_on_ipc_message_bottom));
	}
	
	// Connect gui_input signal to handle input events
	if (bottom_panel_web_view->has_signal("gui_input")) {
		bottom_panel_web_view->connect("gui_input", callable_mp(this, &GodotIDEPlugin::_on_webview_gui_input));
	}
	
	// Connect unhandled input signals to prevent event bubbling
	if (bottom_panel_web_view->has_signal("unhandled_input")) {
		bottom_panel_web_view->connect("unhandled_input", callable_mp(this, &GodotIDEPlugin::_on_webview_unhandled_input));
	}
	if (bottom_panel_web_view->has_signal("unhandled_key_input")) {
		bottom_panel_web_view->connect("unhandled_key_input", callable_mp(this, &GodotIDEPlugin::_on_webview_unhandled_key_input));
	}
	
	bottom_panel_web_view->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	bottom_panel_web_view->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	bottom_panel_web_view->set_custom_minimum_size(Size2(0, 300) * EDSCALE);
	
	bottom_panel_button = add_control_to_bottom_panel(bottom_panel_web_view, "VSCode");
	
	bottom_panel_web_view->call("create_webview");
	bottom_loaded = true;
}

void GodotIDEPlugin::_destroy_bottom_panel_webview() {
	if (!bottom_panel_web_view) {
		return;
	}

	remove_control_from_bottom_panel(bottom_panel_web_view);
	
	bottom_panel_web_view->queue_free();
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

void GodotIDEPlugin::_on_webview_gui_input(const Ref<InputEvent> &event) {
	// Handle input events to ensure WebView captures them properly
	if (event.is_valid()) {
		// Accept all input events to prevent them from propagating to other controls
		// This helps prevent clicks near the WebView edge from triggering Scene dock actions
		get_viewport()->set_input_as_handled();
	}
}

void GodotIDEPlugin::_on_webview_unhandled_input(const Ref<InputEvent> &event) {
	// Capture unhandled input events to prevent them from reaching other controls
	if (event.is_valid()) {
		// Check main screen webview
		if (main_screen_web_view && main_screen_web_view->is_visible() && main_screen_web_view->has_focus()) {
			get_viewport()->set_input_as_handled();
			return;
		}
		
		// Check bottom panel webview
		if (bottom_panel_web_view && bottom_panel_web_view->is_visible() && bottom_panel_web_view->has_focus()) {
			get_viewport()->set_input_as_handled();
			return;
		}
	}
}

void GodotIDEPlugin::_on_webview_unhandled_key_input(const Ref<InputEvent> &event) {
	// Capture unhandled key events to prevent them from reaching other controls
	if (event.is_valid()) {
		// Check main screen webview
		if (main_screen_web_view && main_screen_web_view->is_visible() && main_screen_web_view->has_focus()) {
			get_viewport()->set_input_as_handled();
			return;
		}
		
		// Check bottom panel webview
		if (bottom_panel_web_view && bottom_panel_web_view->is_visible() && bottom_panel_web_view->has_focus()) {
			get_viewport()->set_input_as_handled();
			return;
		}
	}
}
