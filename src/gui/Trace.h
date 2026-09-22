#pragma once
// Optional tracing for troubleshooting: set PLACI_LOG=<file>.

#include <QFile>
#include <QString>
#include <QTime>

namespace placi::gui {

inline void trace(const QString& msg) {
    static const QString path = qEnvironmentVariable("PLACI_LOG");
    if (path.isEmpty()) return;
    QFile f(path);
    if (f.open(QIODevice::Append | QIODevice::Text))
        f.write((QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz ")) + msg + QLatin1Char('\n')).toUtf8());
}

}  // namespace placi::gui
