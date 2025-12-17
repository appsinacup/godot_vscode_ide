/**************************************************************************/
/*  godot_vscode_ide_plugin.cpp                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           GODOT VSCODE IDE                             */
/**************************************************************************/
/* Copyright (c) 2025 Dragos Daian                                        */
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

#include <godot_cpp/godot.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/editor_inspector.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/theme.hpp>

using namespace godot;

void GodotIDEPlugin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_refresh_webview"), &GodotIDEPlugin::_refresh_webview);
	ClassDB::bind_method(D_METHOD("_on_ipc_message_main", "message"), &GodotIDEPlugin::_on_ipc_message_main);
	ClassDB::bind_method(D_METHOD("_on_resource_selected", "p_res", "p_property"), &GodotIDEPlugin::_on_resource_selected);
	ClassDB::bind_method(D_METHOD("_on_script_open_request", "p_script"), &GodotIDEPlugin::_on_script_open_request);
	ClassDB::bind_method(D_METHOD("_process_tunnel_output"), &GodotIDEPlugin::_process_tunnel_output);
	ClassDB::bind_method(D_METHOD("_on_webview_gui_input", "p_event"), &GodotIDEPlugin::_on_webview_gui_input);
	ClassDB::bind_method(D_METHOD("_open_dev_tools"), &GodotIDEPlugin::_open_dev_tools);
}

GodotIDEPlugin::GodotIDEPlugin() {
	main_screen_web_view = nullptr;
	main_loaded = false;
	distraction_free_enabled_by_us = false;
	current_url = "";
	tunnel_started = false;
	output_timer = nullptr;

	if (!EditorInterface::get_singleton() || !EditorInterface::get_singleton()->get_editor_main_screen() || !ProjectSettings::get_singleton()) {
		return;
	}

	if (!ClassDB::class_exists("WebView")) {
		ERR_PRINT_ONCE("GodotIDEPlugin: WebView class not found - godot_wry module not properly loaded");
		return;
	}

	if (!ProjectSettings::get_singleton()->has_setting("editor/ide/vscode_url")) {
		ProjectSettings::get_singleton()->set_setting("editor/ide/vscode_url", "https://vscode.dev");
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
	main_screen_web_view->call("set_forward_input_events", false);

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
	// Add to main screen using EditorInterface (GDExtension API)
	if (EditorInterface::get_singleton() && EditorInterface::get_singleton()->get_editor_main_screen()) {
		EditorInterface::get_singleton()->get_editor_main_screen()->add_child(main_screen_web_view);
	}
	main_screen_web_view->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	main_screen_web_view->hide();

	EditorInterface *editor_interface = EditorInterface::get_singleton();
	if (editor_interface) {
		add_tool_menu_item("Open developer tools", callable_mp(this, &GodotIDEPlugin::_open_dev_tools));
		add_tool_menu_item("Refresh VSCode view", callable_mp(this, &GodotIDEPlugin::_refresh_webview));
		
		// Try to get the editor inspector via EditorInterface. The old InspectorDock/SceneTreeDock singletons are not available in GDExtension.
		EditorInspector *inspector = editor_interface->get_inspector();
		if (inspector) {
			// EditorInspector does not expose the same 'resource_selected' signal in GDExtension; skip connecting for now.
		}

		// Scene tree signals (open_script) were provided via SceneTreeDock in module API. Skipping automatic connection in GDExtension for now.
		
		fully_initialized = true;
	} else {
		ERR_PRINT_ONCE("GodotIDEPlugin: EditorInterface singleton not found");
		fully_initialized = false;
	}
	bool auto_start = ProjectSettings::get_singleton()->get_setting("editor/ide/auto_start_tunnel", true);
	if (auto_start) {
		_start_code_tunnel();
	}
}

GodotIDEPlugin::~GodotIDEPlugin() {
	_cleanup_tunnel();
	
	if (fully_initialized && EditorInterface::get_singleton()) {
		remove_tool_menu_item("Open developer tools");
		remove_tool_menu_item("Refresh VSCode view");
	}

	if (main_screen_web_view) {
		main_screen_web_view->queue_free();
		main_screen_web_view = nullptr;
	}
}

void GodotIDEPlugin::_make_visible(bool p_visible) {
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

void GodotIDEPlugin::_on_ipc_message_main(const String &message) {
	if (main_screen_web_view && main_loaded) {
		main_screen_web_view->grab_click_focus();
		main_screen_web_view->grab_focus();
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

void GodotIDEPlugin::_process_tunnel_output() {
	if (!tunnel_stdio.is_valid()) {
		return;
	}
	
	String text = tunnel_stdio->get_as_text();
	print_line("[VSCode]: " + text);
	if (!text.is_empty()) {
		for (auto line_split : text.split("\r\n", true)) {
			for (auto line_split_element : line_split.split("\n", true)) {
				_extract_vscode_url(line_split_element);
			}
		}
	}
}

void GodotIDEPlugin::_cleanup_tunnel() {
	// Clean up timer first
	if (output_timer) {
		output_timer->stop();
		output_timer->queue_free();
		output_timer = nullptr;
	}
	
	if (tunnel_started && tunnel_process.has("pid")) {
		int64_t pid = tunnel_process["pid"];
		print_line("[VSCode] Killing tunnel process with PID: " + itos(pid));
		OS::get_singleton()->kill(pid);
	}
	
	if (tunnel_stdio.is_valid()) {
		tunnel_stdio.unref();
	}
	
	tunnel_process.clear();
	tunnel_started = false;
}

void GodotIDEPlugin::_extract_vscode_url(const String &text) {
	static bool in_url = false;
	static String building_url;

	String chunk = text.strip_edges();

	if (!in_url && chunk.contains("https://vscode.dev/tunnel/")) {
		in_url = true;
		building_url = "";
		int start_pos = chunk.find("https://vscode.dev/tunnel/");
		if (start_pos != -1) {
			String after = chunk.substr(start_pos);
			building_url += after;
		}
		return;
	}

	if (in_url && chunk.is_empty()) {
		String clean_url = building_url.strip_edges();
		ProjectSettings::get_singleton()->set_setting("editor/ide/vscode_url", clean_url);
		ProjectSettings::get_singleton()->save();
		_update_url_from_settings();

		in_url = false;
		building_url = "";
		// Finished building URL - stop the timer
		if (output_timer) {
			output_timer->stop();
			output_timer->set_autostart(false);
			output_timer->call_deferred("queue_free");
			output_timer = nullptr;
		}
		return;
	}

	if (in_url) {
		building_url += chunk;
	}
}

void GodotIDEPlugin::_open_script_in_vscode(const String &script_path) {
	//print_line("Attempting to open script in VSCode: " + script_path);
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
	String json_message = Variant(message).stringify();
	// TODO call into vscode extension
	// print_line("Sent message to VSCode webview: " + json_message);
}

void GodotIDEPlugin::_update_url_from_settings() {
	if (!main_screen_web_view || !ProjectSettings::get_singleton()) {
		return;
	}

	String new_url = ProjectSettings::get_singleton()->get_setting("editor/ide/vscode_url", "https://vscode.dev");
	if (current_url == new_url) {
		return;
	}
	current_url = new_url;
	if (main_screen_web_view) {
		main_screen_web_view->call("set_url", current_url);
	}
	if (main_loaded) {
		refresh_webview();
	}
}

void GodotIDEPlugin::_start_code_tunnel() {
	if (tunnel_started) {
		return;
	}
	
	PackedStringArray args;
	args.push_back("tunnel");
	args.push_back("--accept-server-license-terms");
	
	tunnel_process = OS::get_singleton()->execute_with_pipe("code", args, false);
	
	if (tunnel_process.has("pid") && tunnel_process.has("stdio")) {
		tunnel_stdio = tunnel_process["stdio"];
		tunnel_started = true;
		
		// Set up a timer to periodically check for output
		output_timer = memnew(Timer);
		output_timer->set_wait_time(1);
		output_timer->set_autostart(true);
		output_timer->connect("timeout", callable_mp(this, &GodotIDEPlugin::_process_tunnel_output));
		if (main_screen_web_view) {
			main_screen_web_view->add_child(output_timer);
		}
	} else {
		ERR_PRINT("Failed to start VSCode tunnel process");
	}
}

Ref<Texture2D> GodotIDEPlugin::_get_plugin_icon() const {
	if (!EditorInterface::get_singleton()) {
		return Ref<Texture2D>();
	}

	Ref<Theme> theme = EditorInterface::get_singleton()->get_editor_theme();
	if (!theme.is_valid()) {
		return Ref<Texture2D>();
	}

	return theme->get_icon(StringName("Script"), StringName("EditorIcons"));
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
