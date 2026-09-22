#include "gui/BuildController.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTime>
#include <QFileInfo>
#include <QtConcurrent/QtConcurrentRun>

#include "core/pipeline.hpp"
#include "gui/Trace.h"

namespace placi::gui {

namespace {

QString qs(const std::string& s) { return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())); }
std::string ss(const QString& s) { return s.toStdString(); }

BuildReport runBuild(QString docPath, QString text, QString style, QString job, QString target) {
    trace(QStringLiteral("build start %1 job=%2 chars=%3").arg(docPath, job).arg(text.size()));
    BuildReport rep;
    rep.exported = !target.isEmpty();
    QElapsedTimer t;
    t.start();
    placi::Diagnostics diags;
    try {
        placi::BuildOptions o;
        o.input = placi::u8path(ss(docPath));
        o.source = ss(text);
        if (!style.isEmpty()) o.style = ss(style);
        o.job_name = ss(job);
        const auto buildDir = QFileInfo(docPath).absoluteDir().filePath(QStringLiteral(".placi"));
        o.output = placi::u8path(ss(target.isEmpty() ? QDir(buildDir).filePath(job + QStringLiteral(".pdf")) : target));
        auto r = placi::build_pdf(o, diags);
        rep.ok = r.compile.ok;
        rep.pdf = qs(placi::path_str(r.pdf.empty() ? r.compile.pdf : r.pdf));
        rep.tex = qs(placi::path_str(r.tex));
        for (auto& e : r.compile.errors) rep.latexErrors << qs(e);
        rep.log = qs(r.compile.output);
    } catch (const placi::PlaciError& e) {
        rep.problems.append({true, qs(e.what()), qs(e.file), static_cast<int>(e.line)});
    } catch (const std::exception& e) {
        rep.problems.append({true, qs(e.what()), {}, 0});
    }
    for (auto& d : diags)
        rep.problems.append({d.level == placi::Diagnostic::Level::Error, qs(d.message), qs(d.file), static_cast<int>(d.line)});
    rep.millis = t.elapsed();
    trace(QStringLiteral("build end ok=%1 ms=%2 problems=%3 pdf=%4").arg(rep.ok).arg(rep.millis).arg(rep.problems.size()).arg(rep.pdf));
    for (auto& p : rep.problems) trace(QStringLiteral("  %1:%2 %3").arg(p.file).arg(p.line).arg(p.message));
    return rep;
}

}  // namespace

BuildController::BuildController(QObject* parent) : QObject(parent) {
    timer_.setSingleShot(true);
    timer_.setInterval(1200);
    connect(&timer_, &QTimer::timeout, this, &BuildController::launch);
    connect(&watcher_, &QFutureWatcher<BuildReport>::finished, this, [this] {
        emit finished(watcher_.result());
        if (pending_) launch();
    });
}

void BuildController::requestPreview(const QString& text) {
    trace(QStringLiteral("preview requested for %1").arg(docPath_));
    pendingText_ = text;
    pending_ = true;
    timer_.start();
}

void BuildController::exportPdf(const QString& text, const QString& target) {
    pendingText_ = text;
    exportTarget_ = target;
    pending_ = true;
    timer_.stop();
    launch();
}

void BuildController::launch() {
    trace(QStringLiteral("launch pending=%1 doc=%2 running=%3").arg(pending_).arg(docPath_).arg(watcher_.isRunning()));
    if (!pending_ || docPath_.isEmpty()) return;
    if (watcher_.isRunning()) return;  // relaunched from the finished handler
    pending_ = false;
    QString target = exportTarget_;
    exportTarget_.clear();
    // Two alternating job names, so the viewer never holds the file Tectonic writes.
    QString job = target.isEmpty() ? (flip_++ % 2 ? QStringLiteral("preview-b") : QStringLiteral("preview-a"))
                                   : QStringLiteral("export");
    emit started(!target.isEmpty());
    watcher_.setFuture(QtConcurrent::run(runBuild, docPath_, pendingText_, style_, job, target));
}

}  // namespace placi::gui
