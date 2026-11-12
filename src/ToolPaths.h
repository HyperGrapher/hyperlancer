#pragma once

#include <QString>

// Where the Python tools are. Defaults are found next to the app; the user can override both
// paths in Settings.
namespace ToolPaths {

// Walks up from startDir looking for web-dashboard/scripts/pyproject.toml. Empty if not found.
[[nodiscard]] QString findScriptsDir(const QString &startDir);
// The uv virtual environment's interpreter inside scriptsDir (.venv/Scripts/python.exe on
// Windows, .venv/bin/python elsewhere).
[[nodiscard]] QString venvPython(const QString &scriptsDir);

} // namespace ToolPaths
