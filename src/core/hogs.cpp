#include "hogs.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace Hogs {

bool isBuildDir(const QString &path)
{
    const QFileInfo info(path);
    const QString name = info.fileName();
    const QDir parent = info.dir();
    auto has = [&parent](const char *file) { return parent.exists(QLatin1String(file)); };
    if (name == QLatin1String("node_modules") || name == QLatin1String(".next") || name == QLatin1String(".nuxt")
        || name == QLatin1String(".gradle") || name == QLatin1String(".dart_tool"))
        return true;
    if (name == QLatin1String("target"))
        return has("Cargo.toml") || has("pom.xml");
    if (name == QLatin1String("build") || name.startsWith(QLatin1String("build-")) || name == QLatin1String("cmake-build-debug")
        || name == QLatin1String("cmake-build-release"))
        return has("CMakeLists.txt") || has("meson.build") || has("build.gradle") || has("build.gradle.kts")
            || has("package.json") || has("setup.py") || has("pyproject.toml") || has("pubspec.yaml");
    if (name == QLatin1String(".venv") || name == QLatin1String("venv"))
        return QFileInfo::exists(path + QStringLiteral("/pyvenv.cfg"));
    return false;
}

qint64 parseHumanSize(const QString &text)
{
    // "4.7 GB", "12.3GB", "850MB", "1.2 GiB", "0B"
    static const QRegularExpression re(QStringLiteral("^\\s*([0-9]+(?:[.,][0-9]+)?)\\s*([kKMGTP]?)(i?)B\\s*$"));
    const auto m = re.match(text);
    if (!m.hasMatch())
        return -1;
    const double value = m.captured(1).replace(QLatin1Char(','), QLatin1Char('.')).toDouble();
    const double base = m.captured(3).isEmpty() ? 1000.0 : 1024.0;
    const QString unit = m.captured(2).toUpper();
    int power = 0;
    if (unit == QLatin1String("K")) power = 1;
    else if (unit == QLatin1String("M")) power = 2;
    else if (unit == QLatin1String("G")) power = 3;
    else if (unit == QLatin1String("T")) power = 4;
    else if (unit == QLatin1String("P")) power = 5;
    double result = value;
    for (int i = 0; i < power; ++i)
        result *= base;
    return qint64(result);
}

} // namespace Hogs
