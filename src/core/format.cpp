#include "format.h"

#include <QLocale>
#include <QStringList>

namespace Format {

static const QLocale &locale()
{
    static const QLocale ptBR(QLocale::Portuguese, QLocale::Brazil);
    return ptBR;
}

static QString number(double value)
{
    // 3 significant digits at most: 4,52 -> "4,5", 17,93 -> "17,9", 695,2 -> "695"
    const int decimals = value < 100.0 ? 1 : 0;
    QString text = locale().toString(value, 'f', decimals);
    if (decimals > 0 && text.endsWith(QStringLiteral(",0")))
        text.chop(2);
    return text;
}

QString bytes(qint64 value)
{
    if (value < 0)
        value = 0;
    static const char *units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    double size = double(value);
    int unit = 0;
    while (size >= 1000.0 && unit < 5) { // 1000 KiB shows as "0,98 MB" -> "1 MB"
        size /= 1024.0;
        ++unit;
    }
    if (unit == 0)
        return QStringLiteral("%1 B").arg(value);
    return number(size) + QLatin1Char(' ') + QLatin1String(units[unit]);
}

QString percent(double fraction)
{
    const double value = fraction * 100.0;
    if (value > 0.0 && value < 10.0)
        return number(value) + QLatin1Char('%');
    return locale().toString(qRound(value)) + QLatin1Char('%');
}

QString frequency(double mhz)
{
    if (mhz <= 0.0)
        return QString();
    if (mhz >= 1000.0)
        return locale().toString(mhz / 1000.0, 'f', 1) + QStringLiteral(" GHz");
    return locale().toString(qRound(mhz)) + QStringLiteral(" MHz");
}

QString duration(qint64 seconds)
{
    const qint64 days = seconds / 86400;
    const qint64 hours = (seconds % 86400) / 3600;
    const qint64 minutes = (seconds % 3600) / 60;
    if (days > 0)
        return QStringLiteral("%1 d %2 h").arg(days).arg(hours);
    if (hours > 0)
        return QStringLiteral("%1 h %2 min").arg(hours).arg(minutes);
    return QStringLiteral("%1 min").arg(minutes);
}

} // namespace Format
