@tool
class_name VSCodeWebView
extends WebView

func _init() -> void:
	zoom_hotkeys = true
	full_window_size = false
	forward_input_events = true

	mouse_filter = Control.MOUSE_FILTER_STOP
	process_mode = Node.PROCESS_MODE_ALWAYS
	clip_contents = true

func update_url_from_project_settings():
	var new_url = ProjectSettings.get_setting("editor/ide/vscode_url", "https://vscode.dev")
	if url == new_url:
		return
	url = new_url
	reload()
