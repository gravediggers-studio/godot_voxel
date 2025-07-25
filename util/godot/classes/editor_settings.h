#ifndef ZN_GODOT_EDITOR_SETTINGS_H
#define ZN_GODOT_EDITOR_SETTINGS_H

#if defined(ZN_GODOT)

#include "../core/version.h"

#if GODOT_VERSION_MAJOR == 4 && GODOT_VERSION_MINOR <= 4
#include <editor/editor_settings.h>
#else
#include <editor/settings/editor_settings.h>
#endif

#elif defined(ZN_GODOT_EXTENSION)
#include <godot_cpp/classes/editor_settings.hpp>
using namespace godot;
#endif

#include "shortcut.h"

namespace zylann::godot {

Ref<Shortcut> get_or_create_editor_shortcut(const String &p_path, const String &p_name, Key p_keycode);

} // namespace zylann::godot

#endif // ZN_GODOT_EDITOR_SETTINGS_H
