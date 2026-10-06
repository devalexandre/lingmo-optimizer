#pragma once

#include <QString>

namespace Format {
// "695 GB", "17,9 GB", "512 MB", "3,2 KB" (binary units, Brazilian decimal comma)
QString bytes(qint64 bytes);
// "79%", "4,5%" for fractions 0..1
QString percent(double fraction);
// "2,4 GHz" / "800 MHz" for a frequency in MHz
QString frequency(double mhz);
// "3 d 4 h", "5 h 12 min", "8 min"
QString duration(qint64 seconds);
}
