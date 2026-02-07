@tool
class_name GodotVSCodePlugin
extends EditorPlugin

const VSCODE_WEBVIEW_SCENE := preload("res://addons/godot_vscode_ide/vscode_webview.tscn")
const VSCODE_ICON := preload("res://addons/godot_vscode_ide/vscode-alt.svg")

var webview: VSCodeWebView
var output_timer: Timer = null
var main_loaded := false
var distraction_free_enabled_by_us := false
# Tunnel/process capture
var tunnel_process := {}
var tunnel_stdio: FileAccess = null
var tunnel_stderr: FileAccess = null
var _tunnel_in_url := false
var _tunnel_building_url := ""

func _enter_tree():
	webview = VSCODE_WEBVIEW_SCENE.instantiate()
	if not webview:
		push_error("[VSCode] Failed to instantiate vscode_webview.tscn")
		return
	webview.visible = false
	webview.focus_mode = Control.FOCUS_ALL
	webview.update_url_from_project_settings()

	webview.ipc_message.connect(_on_ipc_message_main)
	webview.gui_input.connect(_on_webview_gui_input)

	var main_screen = get_editor_interface().get_editor_main_screen()
	main_screen.add_child(webview)

	add_tool_menu_item("Open developer tools", _open_dev_tools)
	add_tool_menu_item("Refresh VSCode view", _refresh_webview)
	add_tool_menu_item("Start tunnel", _start_code_tunnel)
	add_tool_menu_item("Stop tunnel", _cleanup_tunnel)
	add_tool_menu_item("Kill tunnels", _kill_all_tunnels)

	if ProjectSettings.get_setting("editor/ide/auto_start_tunnel", true):
		_start_code_tunnel()

func _exit_tree():
	_cleanup_tunnel()
	remove_tool_menu_item("Open developer tools")
	remove_tool_menu_item("Refresh VSCode view")
	remove_tool_menu_item("Start tunnel")
	remove_tool_menu_item("Stop tunnel")
	remove_tool_menu_item("Kill tunnels")
	if webview:
		webview.queue_free()
		webview = null

func _get_plugin_name() -> String:
	return "Code"

func _get_plugin_icon() -> Texture2D:
	return VSCODE_ICON

func _has_main_screen() -> bool:
	return true

func _make_visible(p_visible: bool) -> void:
	if not webview:
		return
	webview.update_url_from_project_settings()
	if not main_loaded:
		main_loaded = true
		webview.create_webview()

	webview.visible = p_visible
	webview.grab_click_focus()
	webview.grab_focus()

	var distraction_free_setting = ProjectSettings.get_setting("editor/ide/distraction_free_mode", false)
	var editor_interface = get_editor_interface()
	if distraction_free_setting:
		if p_visible:
			if not editor_interface.is_distraction_free_mode_enabled():
				editor_interface.set_distraction_free_mode(true)
				distraction_free_enabled_by_us = true
		else:
			if distraction_free_enabled_by_us:
				editor_interface.set_distraction_free_mode(false)
				distraction_free_enabled_by_us = false

func _refresh_webview() -> void:
	if webview and main_loaded:
		webview.reload()

func _on_ipc_message_main(message: String) -> void:
	if webview and main_loaded:
		webview.grab_click_focus()
		webview.grab_focus()

func _on_resource_selected(p_res: Resource, p_property: String) -> void:
	var selected_script := p_res
	if typeof(selected_script) == TYPE_OBJECT and selected_script is Script:
		var script_path = selected_script.resource_path
		if script_path != "":
			_open_script_in_vscode(script_path)

func _on_script_open_request(p_script: Script) -> void:
	if p_script:
		var script_path = p_script.resource_path
		if script_path != "":
			_open_script_in_vscode(script_path)

func _process(delta: float) -> void:
	if !tunnel_process.is_empty():
		var stdio_text = tunnel_stdio.get_as_text()
		if stdio_text != "":
			_extract_vscode_url(stdio_text)

		var stderr_text = tunnel_stderr.get_as_text()
		if stderr_text != "":
			print("[VSCode] Error from tunnel: ", stderr_text)

func _extract_vscode_url(text: String) -> void:
	for line in text.split("\n"):
		var chunk = line.strip_edges()
		print("[VSCode] ", chunk)

		if not _tunnel_in_url and chunk.find("https://vscode.dev/tunnel/") != -1:
			_tunnel_in_url = true
			_tunnel_building_url = ""
			var start_pos = chunk.find("https://vscode.dev/tunnel/")
			if start_pos != -1:
				var after = chunk.substr(start_pos, chunk.length() - start_pos)
				_tunnel_building_url += after
			continue
		if _tunnel_in_url and chunk.is_empty():
			var clean_url = _tunnel_building_url.strip_edges()
			print("[VSCode] Found tunnel at: ", _tunnel_building_url)
			ProjectSettings.set_setting("editor/ide/vscode_url", clean_url)
			ProjectSettings.save()
			webview.update_url_from_project_settings()
			_tunnel_in_url = false
			_tunnel_building_url = ""
			continue

		if _tunnel_in_url:
			_tunnel_building_url += chunk

func _cleanup_tunnel() -> void:
	if output_timer and output_timer.is_inside_tree():
		output_timer.stop()
		output_timer.queue_free()
		output_timer = null
	var pid = tunnel_process.get("pid", -1)
	if pid != -1:
		print("[VSCode] Killing tunnel with PID ", pid)
		OS.kill(pid)
	set_process(false)
	tunnel_process = {}
	tunnel_stdio = null
	tunnel_stderr = null

func _kill_all_tunnels() -> void:
	var os_name = OS.get_name()
	print("[VSCode] Killing all VSCode tunnel processes on ", os_name)
	var result := []
	if os_name == "Windows":
		# Use PowerShell to find processes whose command line contains "code" and "tunnel" and terminate them.
		var ps_cmd = "Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'code(\\.exe)?\\s+tunnel' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }"
		OS.execute("powershell", ["-NoProfile", "-Command", ps_cmd], result, true)
	else:
		# macOS/Linux: list and kill processes whose command line contains "code" and "tunnel".
		var output: Array = []
		var exit_code = OS.execute("ps", ["-axo", "pid=,command="], output, true)
		if exit_code != 0 or output.is_empty():
			print("[VSCode] Failed to list processes via ps.")
			return
		var output_text = str(output[0])
		var killed_any := false
		for line in output_text.split("\n"):
			var trimmed = line.strip_edges()
			if trimmed == "":
				continue
			var parts = trimmed.split(" ", false, 1)
			if parts.size() < 2:
				continue
			var pid_str = parts[0].strip_edges()
			var cmd = parts[1]
			var is_tunnel = (cmd.find("tunnel") != -1) and (cmd.find("code") != -1 or cmd.find("code-tunnel") != -1)
			if not is_tunnel:
				continue
			var pid = int(pid_str)
			print("[VSCode] Killing tunnel PID ", pid, ": ", cmd)
			OS.execute("kill", ["-9", str(pid)], [], true)
			killed_any = true
		if not killed_any:
			print("[VSCode] No tunnel processes found via ps.")
	for line in result:
		print("[VSCode] ", line)
	_cleanup_tunnel()

func _open_script_in_vscode(script_path: String) -> void:
	if not webview or script_path == "":
		return

	var project_path = ProjectSettings.globalize_path("res://")
	var full_script_path = ProjectSettings.globalize_path(script_path)
	var message = {"type": "open_file", "path": full_script_path, "project_path": project_path}

func _start_code_tunnel() -> void:
	if !tunnel_process.is_empty():
		return

	var args = ["tunnel", "--accept-server-license-terms"]
	# Use execute_with_pipe to capture stdio/stderr handles (non-blocking)
	var process = OS.execute_with_pipe("code", args, false)
	if process.has("pid") and process.has("stdio"):
		tunnel_process = process
		tunnel_stdio = process["stdio"]
		tunnel_stderr = process["stderr"]
		set_process(true)
		print("[VSCode] Tunnel started with pid %d; capturing output..." % process["pid"])
	else:
		push_error("[VSCode] Failed to start VSCode tunnel (execute_with_pipe did not provide stdio)")

func _open_dev_tools() -> void:
	if not webview:
		push_error("[VSCode] IDE: Main screen webview not available")
		return
	if not main_loaded:
		push_error("[VSCode] IDE: Main screen webview not loaded yet")
		return
	webview.open_devtools()

func _on_webview_gui_input(event: InputEvent) -> void:
	if event:
		# Prevent event propagation
		if get_viewport():
			get_viewport().set_input_as_handled()
