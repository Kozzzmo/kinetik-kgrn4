// Kinetik — files dropped on the plugin window (Windows: OLE drop target, macOS: NSView dragging destination,
// Linux: not supported, use the LOAD button). Keep identical in every Kinetik repository that uses it.
#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace kn {
// fn gets the UTF-8 path of the first dropped file and the drop position as a fraction of the view (0..1, top-left origin)
using FileDropFn = std::function<void(const std::string& path, double fx, double fy)>;
void* fileDropAttach(uintptr_t nativeView, FileDropFn fn);   // returns a token (nullptr if not supported)
void fileDropDetach(void* token);
}
