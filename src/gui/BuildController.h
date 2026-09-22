#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QStringList>
#include <QTimer>

namespace placi::gui {

struct Problem {
    bool error = false;
    QString message;
    QString file;
    int line = 0;
};

struct BuildReport {
    bool ok = false;
    bool exported = false;   // true for "Ekspor PDF", false for the live preview
    QString pdf;
    QString tex;
    QList<Problem> problems;
    QStringList latexErrors;
    QString log;
    qint64 millis = 0;
};

// Builds the document in the background with libplaci. Preview builds are
// debounced and coalesced: while one runs, only the newest text is kept.
class BuildController : public QObject {
    Q_OBJECT
public:
    explicit BuildController(QObject* parent = nullptr);

    void setDocumentPath(const QString& path) { docPath_ = path; }
    void setStyleOverride(const QString& style) { style_ = style; }
    void setDelay(int ms) { timer_.setInterval(ms); }

    void requestPreview(const QString& text);        // debounced
    void exportPdf(const QString& text, const QString& target);
    bool busy() const { return watcher_.isRunning(); }

signals:
    void started(bool exporting);
    void finished(const placi::gui::BuildReport& report);

private:
    void launch();

    QString docPath_, style_;
    QTimer timer_;
    QFutureWatcher<BuildReport> watcher_;
    QString pendingText_, exportTarget_;
    bool pending_ = false;
    int flip_ = 0;
};

}  // namespace placi::gui
