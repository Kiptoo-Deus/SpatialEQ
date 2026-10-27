// Native platform glue for the editor: file dialogs and embedding the web view in the host window.
#pragma once

#include <string>

namespace sq {

/// Runs a modal open or save panel. Returns the chosen path (UTF-8) or "" when cancelled.
std::string chooseFile(void* parentWindow, bool save, const std::string& title, const std::string& defaultName);

/// Adds the web view's native view as a child of the host's parent view (NSView* / HWND).
void embedChildView(void* parent, void* child, int width, int height);
void resizeChildView(void* child, int width, int height);
void removeChildView(void* child);

} // namespace sq
