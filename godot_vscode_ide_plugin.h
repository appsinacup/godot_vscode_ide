/**************************************************************************/
/*  godot_vscode_ide_plugin.h                                             */
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

#pragma once

#include "core/io/file_access.h"

#include "editor/plugins/editor_plugin.h"
#include "scene/main/timer.h"
#include "scene/gui/control.h"
#include "core/input/shortcut.h"

class GodotIDEPlugin : public EditorPlugin {
	GDCLASS(GodotIDEPlugin, EditorPlugin);

private:
	Timer *output_timer;
	Control *main_screen_web_view;
	bool main_loaded = false;
	bool fully_initialized = false;
	bool distraction_free_enabled_by_us = false;
	String current_url;
	
	// Tunnel process management
	Dictionary tunnel_process;
	Ref<FileAccess> tunnel_stdio;
	bool tunnel_started = false;

	void _refresh_webview();
	void _update_url_from_settings();
	void _on_ipc_message_main(const String &message);
	void _on_resource_selected(const Ref<Resource> &p_res, const String &p_property);
	void _on_script_open_request(const Ref<Script> &p_script);
	void _process_tunnel_output();
	void _extract_vscode_url(const String &text);
	void _cleanup_tunnel();
	void _on_webview_gui_input(const Ref<InputEvent> &event);
	void _open_script_in_vscode(const String &script_path);
	void _start_code_tunnel();
	void _open_dev_tools();

protected:
	static void _bind_methods();

public:
	virtual String get_plugin_name() const override { return "VSCode"; }
	virtual bool has_main_screen() const override { return true; }
	virtual void make_visible(bool p_visible) override;
	virtual const Ref<Texture2D> get_plugin_icon() const override;

	void refresh_webview() { _refresh_webview(); }
	void open_dev_tools() { _open_dev_tools(); }

	GodotIDEPlugin();
	~GodotIDEPlugin();
};
