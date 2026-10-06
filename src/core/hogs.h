#pragma once

#include <QString>

// Small pure helpers for the "Sugestões" list
namespace Hogs {
// Is this folder a rebuildable dependency/build folder? (node_modules,
// target next to Cargo.toml, build next to CMakeLists.txt, .venv...)
bool isBuildDir(const QString &path);
// Bytes in a size text from docker/ollama: "4.7 GB", "12.3GB", "1.2 GiB"; -1 if invalid
qint64 parseHumanSize(const QString &text);
}
