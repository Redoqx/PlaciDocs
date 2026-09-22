#pragma once

#include <QMainWindow>
#include <QTimer>

#include "core/style/style.hpp"
#include "gui/BuildController.h"
#include "gui/Dialogs.h"
#include "gui/DocumentText.h"

class QComboBox;
class QLabel;
class QListWidget;
class QMenu;
class QPdfBookmarkModel;
class QPdfDocument;
class QPdfView;
class QToolButton;
class QTreeWidget;
class QAction;

namespace placi::gui {

class Editor;
class Highlighter;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    bool openFile(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    struct OutlineItem {
        int line;
        int level;
        QString title;
    };

    void buildUi();
    void buildMenus();
    void newFile();
    void open();
    bool save();
    bool saveAs();
    void exportPdf();
    bool maybeSave();
    void updateTitle();

    void analyze();  // parse + resolve style: outline, menus, then a preview build
    void onBuildStarted(bool exporting);
    void onBuildFinished(const BuildReport& report);
    void onCursorMoved();
    void followCursor();

    void setHeadingLevel(int level);
    void toggleTag(const QString& token);
    void wrapInSection(const QString& name);
    void insertListHeading(const QString& key, const QString& title);
    void addList();
    void insertImage();
    void insertTable();
    void insertMath();
    void insertCitation();
    void insertPageBreak();
    void chooseStyle(int index);
    void showSyntaxHelp();

    QString docPath() const;  // path used for building (a temp file for untitled documents)
    QString docDir() const;
    void setText(const QString& text);

    Editor* editor_ = nullptr;
    Highlighter* highlighter_ = nullptr;
    QPdfDocument* pdf_ = nullptr;
    QPdfView* view_ = nullptr;
    QPdfBookmarkModel* bookmarks_ = nullptr;
    QTreeWidget* outline_ = nullptr;
    QListWidget* problems_ = nullptr;
    QComboBox* format_ = nullptr;
    QComboBox* styleBox_ = nullptr;
    QMenu *tagMenu_ = nullptr, *sectionMenu_ = nullptr, *listMenu_ = nullptr, *refMenu_ = nullptr;
    QLabel* status_ = nullptr;
    QAction* follow_ = nullptr;

    BuildController build_;
    QTimer analyzeTimer_, followTimer_;
    QString path_;          // empty = untitled
    QString untitledPath_;
    bool loading_ = false;

    // results of the last analysis
    placi::Style style_;
    TagDelimiters delims_;
    QVector<OutlineItem> outlineItems_;
    QStringList ids_;
    QList<ListChoice> lists_;
    QString bibPath_;
    QString shownPdf_;
    int lastOutlineIndex_ = -1;
};

}  // namespace placi::gui
