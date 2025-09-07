def can_build(env, platform):
    # Only build this module for editor builds on desktop platforms
    target = env.get("target", "")
    supported_platforms = ["macos", "linuxbsd", "windows"]

    print(f"GodotIDE can_build check: target={target}, platform={platform}")

    if target == "editor" and platform in supported_platforms:
        env.module_add_dependencies("godot_ide", ["godot_wry"], True)
        print(f"GodotIDE: Building for {platform} editor")
        return True

    print(f"GodotIDE: Skipping - target={target}, platform={platform}")
    return False


def configure(env):
    pass


def get_doc_classes():
    return [
        "GodotIDE",
        "GodotIDEPlugin",
    ]


def get_doc_path():
    return "doc_classes"
