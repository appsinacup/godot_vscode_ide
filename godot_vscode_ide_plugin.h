/**************************************************************************/
/*  godot_vscode_ide_plugin.h                                                    */
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

#pragma once

#include "editor/plugins/editor_plugin.h"
#include "scene/gui/control.h"
#include "core/input/shortcut.h"

class GodotIDEPlugin : public EditorPlugin {
	GDCLASS(GodotIDEPlugin, EditorPlugin);

private:
	Control *main_screen_holder;
	Control *main_screen_web_view;
	Control *bottom_panel_holder;
	Control *bottom_panel_web_view;
	Button *bottom_panel_button;
	bool main_loaded = false;
	bool bottom_loaded = false;
	bool bottom_panel_enabled = false;
	bool fully_initialized = false;
	String current_url;

	void _refresh_webview();
	void _refresh_all_webviews();
	void _update_url_from_settings();
	void _start_code_tunnel();
	void _start_code_tunnel_internal(bool auto_start_only);
	void _retry_terminal_access();
	void _toggle_bottom_panel();
	void _create_bottom_panel_webview();
	void _destroy_bottom_panel_webview();
	void _open_dev_tools();

protected:
	static void _bind_methods();

public:
	virtual String get_plugin_name() const override { return "VSCode"; }
	virtual bool has_main_screen() const override { return true; }
	virtual void make_visible(bool p_visible) override;
	virtual const Ref<Texture2D> get_plugin_icon() const override;

	void refresh_webview() { _refresh_webview(); }
	void refresh_all_webviews() { _refresh_all_webviews(); }
	void toggle_bottom_panel() { _toggle_bottom_panel(); }
	void open_dev_tools() { _open_dev_tools(); }

	GodotIDEPlugin();
	~GodotIDEPlugin();
};
