#include "gui/MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPdfBookmarkModel>
#include <QPdfDocument>
#include <QPdfPageNavigator>
#include <QPdfView>
#include <QSaveFile>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTime>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <functional>

#include "core/parser/preprocess.hpp"
#include "core/pipeline.hpp"
#include "core/style/style_loader.hpp"
#include "gui/Editor.h"
#include "gui/Highlighter.h"
#include "gui/Trace.h"

namespace placi::gui {

namespace {

QString qs(const std::string& s) { return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())); }
std::string ss(const QString& s) { return s.toStdString(); }

const char* kSample = R"(---
title: Judul Dokumen
author: Nama Penulis
style: skripsi-umum
bibliography: references.bib
---

# Kata Pengantar {pembuka}

Tulis kata pengantar di sini.

# DAFTAR ISI {pembuka daftar isi}

# Pendahuluan

## Latar Belakang

Mulailah menulis. Format halaman, judul, dan nomor diatur oleh style.
)";

// Normalised heading text, to match editor headings with PDF bookmarks.
QString key(const QString& s) { return s.simplified().toLower(); }

}  // namespace

MainWindow::MainWindow() {
    untitledPath_ = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(QStringLiteral("PlaciDocs/untitled.md"));
    QDir().mkpath(QFileInfo(untitledPath_).absolutePath());

    buildUi();
    buildMenus();

    analyzeTimer_.setSingleShot(true);
    analyzeTimer_.setInterval(350);
    connect(&analyzeTimer_, &QTimer::timeout, this, &MainWindow::analyze);
    followTimer_.setSingleShot(true);
    followTimer_.setInterval(250);
    connect(&followTimer_, &QTimer::timeout, this, &MainWindow::followCursor);
    autosaveTimer_.setInterval(60000);
    connect(&autosaveTimer_, &QTimer::timeout, this, &MainWindow::autosave);
    autosaveTimer_.start();

    connect(&build_, &BuildController::started, this, &MainWindow::onBuildStarted);
    connect(&build_, &BuildController::finished, this, &MainWindow::onBuildFinished);

    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray());
    if (!restoreState(settings.value(QStringLiteral("state")).toByteArray())) {
        resize(1400, 900);
        auto docks = findChildren<QDockWidget*>();
        for (auto* d : docks)
            resizeDocks({d}, {d->objectName() == QLatin1String("problems") ? 120 : 260},
                        d->objectName() == QLatin1String("problems") ? Qt::Vertical : Qt::Horizontal);
    }

    setText(QString::fromUtf8(kSample));
    updateTitle();
}

// ------------------------------------------------------------------ UI

void MainWindow::buildUi() {
    editor_ = new Editor;
    highlighter_ = new Highlighter(editor_->document());

    pdf_ = new QPdfDocument(this);
    view_ = new QPdfView;
    view_->setDocument(pdf_);
    view_->setPageMode(QPdfView::PageMode::MultiPage);
    view_->setZoomMode(QPdfView::ZoomMode::FitToWidth);
    bookmarks_ = new QPdfBookmarkModel(this);
    bookmarks_->setDocument(pdf_);

    auto* split = new QSplitter;
    split->addWidget(editor_);
    split->addWidget(view_);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);
    setCentralWidget(split);

    outline_ = new QTreeWidget;
    outline_->setHeaderHidden(true);
    auto* outlineDock = new QDockWidget(tr("Kerangka"), this);
    outlineDock->setObjectName(QStringLiteral("outline"));
    outlineDock->setWidget(outline_);
    addDockWidget(Qt::LeftDockWidgetArea, outlineDock);
    connect(outline_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
        editor_->goToLine(item->data(0, Qt::UserRole).toInt());
    });
    connect(outline_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
        editor_->goToLine(item->data(0, Qt::UserRole).toInt());
    });

    problems_ = new QListWidget;
    auto* problemsDock = new QDockWidget(tr("Masalah"), this);
    problemsDock->setObjectName(QStringLiteral("problems"));
    problemsDock->setWidget(problems_);
    addDockWidget(Qt::BottomDockWidgetArea, problemsDock);
    connect(problems_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        if (int line = item->data(Qt::UserRole).toInt(); line > 0) editor_->goToLine(line);
    });

    status_ = new QLabel;
    statusBar()->addPermanentWidget(status_);

    connect(editor_, &QPlainTextEdit::textChanged, this, [this] {
        if (loading_) return;
        analyzeTimer_.start();
        updateTitle();
    });
    connect(editor_, &QPlainTextEdit::cursorPositionChanged, this, &MainWindow::onCursorMoved);
}

void MainWindow::buildMenus() {
    auto* style = QApplication::style();

    // --- File
    auto* file = menuBar()->addMenu(tr("&Berkas"));
    auto* actNew = file->addAction(style->standardIcon(QStyle::SP_FileIcon), tr("&Baru"), QKeySequence::New, this, &MainWindow::newFile);
    auto* actOpen = file->addAction(style->standardIcon(QStyle::SP_DialogOpenButton), tr("&Buka…"), QKeySequence::Open, this, &MainWindow::open);
    auto* actSave = file->addAction(style->standardIcon(QStyle::SP_DialogSaveButton), tr("&Simpan"), QKeySequence::Save, this, &MainWindow::save);
    file->addAction(tr("Simpan &sebagai…"), QKeySequence::SaveAs, this, &MainWindow::saveAs);
    file->addSeparator();
    auto* actExport = file->addAction(tr("&Ekspor PDF…"), QKeySequence(tr("Ctrl+E")), this, &MainWindow::exportPdf);
    file->addSeparator();
    recentMenu_ = file->addMenu(tr("Dokumen &terakhir"));
    autosave_ = file->addAction(tr("Simpan &otomatis"));
    autosave_->setCheckable(true);
    autosave_->setChecked(QSettings().value(QStringLiteral("autosave"), true).toBool());
    autosave_->setToolTip(tr("Menyimpan dokumen yang sudah pernah disimpan setiap menit."));
    connect(autosave_, &QAction::toggled, this, [](bool on) { QSettings().setValue(QStringLiteral("autosave"), on); });
    file->addSeparator();
    file->addAction(tr("&Keluar"), QKeySequence::Quit, this, &QWidget::close);
    rebuildRecentMenu();

    // --- Format
    auto* fmt = menuBar()->addMenu(tr("&Format"));
    auto* actBold = fmt->addAction(tr("&Tebal"), QKeySequence::Bold, this, [this] { editor_->wrapSelection("**", "**"); });
    auto* actItalic = fmt->addAction(tr("&Miring"), QKeySequence::Italic, this, [this] { editor_->wrapSelection("*", "*"); });
    auto* actCode = fmt->addAction(tr("&Kode"), QKeySequence(tr("Ctrl+`")), this, [this] { editor_->wrapSelection("`", "`"); });
    fmt->addSeparator();
    fmt->addAction(tr("Paragraf"), QKeySequence(tr("Ctrl+0")), this, [this] { setHeadingLevel(0); });
    for (int i = 1; i <= 5; ++i)
        fmt->addAction(tr("Judul %1").arg(i), QKeySequence(QStringLiteral("Ctrl+%1").arg(i)), this, [this, i] { setHeadingLevel(i); });
    fmt->addSeparator();
    auto* actBullet = fmt->addAction(tr("Daftar berpoin"), this, [this] { editor_->replaceCurrentLine("- " + editor_->currentLineText()); });
    auto* actNumber = fmt->addAction(tr("Daftar bernomor"), this, [this] { editor_->replaceCurrentLine("1. " + editor_->currentLineText()); });

    // --- Insert
    auto* ins = menuBar()->addMenu(tr("&Sisipkan"));
    auto* actImage = ins->addAction(tr("&Gambar…"), QKeySequence(tr("Ctrl+Shift+G")), this, &MainWindow::insertImage);
    auto* actTable = ins->addAction(tr("&Tabel…"), QKeySequence(tr("Ctrl+Shift+T")), this, &MainWindow::insertTable);
    auto* actMath = ins->addAction(tr("&Rumus…"), QKeySequence(tr("Ctrl+Shift+M")), this, &MainWindow::insertMath);
    auto* actCite = ins->addAction(tr("&Sitasi…"), QKeySequence(tr("Ctrl+Shift+C")), this, &MainWindow::insertCitation);
    auto* actBreak = ins->addAction(tr("&Halaman baru"), QKeySequence(tr("Ctrl+Return")), this, &MainWindow::insertPageBreak);

    tagMenu_ = new QMenu(tr("&Tag judul"), this);
    sectionMenu_ = new QMenu(tr("S&ection format"), this);
    listMenu_ = new QMenu(tr("&Daftar"), this);
    refMenu_ = new QMenu(tr("&Rujukan"), this);
    ins->addSeparator();
    ins->addMenu(tagMenu_);
    ins->addMenu(sectionMenu_);
    ins->addMenu(listMenu_);
    ins->addMenu(refMenu_);

    // --- View
    auto* view = menuBar()->addMenu(tr("&Tampilan"));
    follow_ = view->addAction(tr("Pratinjau mengikuti kursor"));
    follow_->setCheckable(true);
    follow_->setChecked(true);
    for (auto* dock : findChildren<QDockWidget*>()) view->addAction(dock->toggleViewAction());

    auto* help = menuBar()->addMenu(tr("&Bantuan"));
    help->addAction(tr("Panduan &sintaks"), QKeySequence::HelpContents, this, &MainWindow::showSyntaxHelp);
    help->addAction(tr("&Tentang PlaciDocs"), this, [this] {
        QMessageBox::about(this, tr("Tentang PlaciDocs"),
                           tr("<b>PlaciDocs</b> 0.2<br>Tulis Markdown, dapatkan PDF sesuai aturan penulisan instansi Anda.<br>"
                              "Mesin: libplaci + Tectonic (XeTeX)."));
    });

    // --- Toolbar
    auto* tb = addToolBar(tr("Utama"));
    tb->setObjectName(QStringLiteral("main"));
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    tb->addAction(actNew);
    tb->addAction(actOpen);
    tb->addAction(actSave);
    tb->addSeparator();
    format_ = new QComboBox;
    format_->addItems({tr("Paragraf"), tr("Judul 1"), tr("Judul 2"), tr("Judul 3"), tr("Judul 4"), tr("Judul 5")});
    connect(format_, &QComboBox::activated, this, [this](int i) { setHeadingLevel(i); });
    tb->addWidget(format_);
    tb->addAction(actBold);
    tb->addAction(actItalic);
    tb->addAction(actCode);
    tb->addAction(actBullet);
    tb->addAction(actNumber);
    tb->addSeparator();
    auto dropdown = [&](QMenu* menu, const QString& text) {
        auto* b = new QToolButton;
        b->setText(text);
        b->setMenu(menu);
        b->setPopupMode(QToolButton::InstantPopup);
        tb->addWidget(b);
    };
    dropdown(tagMenu_, tr("Tag"));
    dropdown(sectionMenu_, tr("Section"));
    tb->addSeparator();
    tb->addAction(actImage);
    tb->addAction(actTable);
    tb->addAction(actMath);
    tb->addAction(actCite);
    dropdown(refMenu_, tr("Rujukan"));
    tb->addAction(actBreak);
    tb->addSeparator();
    dropdown(listMenu_, tr("Daftar"));
    tb->addSeparator();
    tb->addWidget(new QLabel(tr(" Style: ")));
    styleBox_ = new QComboBox;
    styleBox_->addItem(tr("(dari dokumen)"), QString());
    for (auto& dir : placi::builtin_style_dirs())
        for (auto& e : std::filesystem::directory_iterator(dir))
            if (e.path().extension() == ".yaml") styleBox_->addItem(qs(placi::path_str(e.path().stem())), qs(placi::path_str(e.path().stem())));
    connect(styleBox_, &QComboBox::activated, this, &MainWindow::chooseStyle);
    tb->addWidget(styleBox_);
    tb->addSeparator();
    tb->addAction(actExport);
}

// ------------------------------------------------------------------ files

QString MainWindow::docPath() const { return path_.isEmpty() ? untitledPath_ : path_; }
QString MainWindow::docDir() const { return QFileInfo(docPath()).absolutePath(); }

void MainWindow::setText(const QString& text) {
    loading_ = true;
    editor_->setPlainText(text);
    editor_->document()->setModified(false);
    loading_ = false;
    build_.setDocumentPath(docPath());
    analyze();
}

void MainWindow::updateTitle() {
    QString name = path_.isEmpty() ? tr("Tanpa judul") : QFileInfo(path_).fileName();
    setWindowTitle(name + (editor_->document()->isModified() ? QStringLiteral(" •") : QString()) + QStringLiteral(" — PlaciDocs"));
}

void MainWindow::newFile() {
    if (!maybeSave()) return;
    path_.clear();
    setText(QString::fromUtf8(kSample));
    updateTitle();
}

void MainWindow::open() {
    if (!maybeSave()) return;
    QString f = QFileDialog::getOpenFileName(this, tr("Buka dokumen"), QFileInfo(docPath()).absolutePath(),
                                             tr("Dokumen PlaciDocs (*.md *.markdown);;Semua berkas (*)"));
    if (!f.isEmpty()) openFile(f);
}

bool MainWindow::openFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Buka dokumen"), tr("Tidak bisa membuka %1: %2").arg(path, f.errorString()));
        return false;
    }
    path_ = QFileInfo(path).absoluteFilePath();
    setText(QString::fromUtf8(f.readAll()).replace(QStringLiteral("\r\n"), QStringLiteral("\n")));
    updateTitle();
    return true;
}

bool MainWindow::save() {
    if (path_.isEmpty()) return saveAs();
    QSaveFile f(path_);
    if (!f.open(QIODevice::WriteOnly) || f.write(editor_->toPlainText().toUtf8()) < 0 || !f.commit()) {
        QMessageBox::warning(this, tr("Simpan"), tr("Tidak bisa menyimpan %1: %2").arg(path_, f.errorString()));
        return false;
    }
    editor_->document()->setModified(false);
    rememberRecent(path_);
    updateTitle();
    return true;
}

void MainWindow::rememberRecent(const QString& path) {
    QSettings settings;
    QStringList recent = settings.value(QStringLiteral("recentFiles")).toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > 8) recent.removeLast();
    settings.setValue(QStringLiteral("recentFiles"), recent);
    rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu() {
    if (!recentMenu_) return;
    recentMenu_->clear();
    const QStringList recent = QSettings().value(QStringLiteral("recentFiles")).toStringList();
    for (const QString& path : recent) {
        if (!QFileInfo::exists(path)) continue;
        recentMenu_->addAction(QFileInfo(path).fileName() + QStringLiteral("   —   ") + QFileInfo(path).absolutePath(),
                               this, [this, path] {
                                   if (maybeSave()) openFile(path);
                               });
    }
    if (recentMenu_->isEmpty()) recentMenu_->addAction(tr("(belum ada)"))->setEnabled(false);
}

// Saves a document that already has a file, the way Word does; untitled
// documents are left alone so nothing is written where the user did not ask.
void MainWindow::autosave() {
    if (!autosave_ || !autosave_->isChecked()) return;
    if (path_.isEmpty() || !editor_->document()->isModified()) return;
    if (save()) statusBar()->showMessage(tr("Disimpan otomatis %1").arg(QTime::currentTime().toString("HH:mm")), 4000);
}

bool MainWindow::saveAs() {
    QString f = QFileDialog::getSaveFileName(this, tr("Simpan dokumen"), path_.isEmpty() ? QDir::homePath() + "/dokumen.md" : path_,
                                             tr("Dokumen PlaciDocs (*.md)"));
    if (f.isEmpty()) return false;
    if (QFileInfo(f).suffix().isEmpty()) f += QStringLiteral(".md");
    path_ = f;
    build_.setDocumentPath(path_);
    bool ok = save();
    analyze();  // relative images and the .bib now resolve from the new folder
    return ok;
}

bool MainWindow::maybeSave() {
    if (!editor_->document()->isModified()) return true;
    auto r = QMessageBox::question(this, tr("Simpan perubahan?"), tr("Dokumen ini punya perubahan yang belum disimpan."),
                                   QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Save) return save();
    return r == QMessageBox::Discard;
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!maybeSave()) {
        event->ignore();
        return;
    }
    QSettings settings;
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
    settings.setValue(QStringLiteral("state"), saveState());
    event->accept();
}

void MainWindow::exportPdf() {
    QString suggestion = QFileInfo(docPath()).absolutePath() + '/' + QFileInfo(docPath()).completeBaseName() + QStringLiteral(".pdf");
    QString f = QFileDialog::getSaveFileName(this, tr("Ekspor PDF"), suggestion, tr("PDF (*.pdf)"));
    if (f.isEmpty()) return;
    build_.exportPdf(editor_->toPlainText(), f);
}

// ------------------------------------------------------------------ analysis & builds

void MainWindow::analyze() {
    const QString text = editor_->toPlainText();
    const std::string src = ss(text);
    placi::Diagnostics quiet;
    const auto dir = placi::u8path(ss(docDir()));

    size_t fmLine = 0;
    auto fm = placi::extract_front_matter(src, &fmLine);
    std::map<std::string, std::string> meta;
    try {
        if (!fm.empty()) meta = placi::parse_front_matter(fm, ss(docPath()), fmLine, quiet);
    } catch (const std::exception&) {
    }
    try {
        style_ = placi::resolve_style(meta, dir, std::nullopt, quiet);
    } catch (const std::exception&) {
        style_ = placi::Style{};
    }
    placi::apply_document_lists(style_, meta, quiet);
    delims_ = {qs(style_.syntax.tag_open), qs(style_.syntax.tag_close)};
    loading_ = true;  // re-highlighting emits textChanged without changing the text
    highlighter_->setDelimiters(delims_);
    loading_ = false;

    auto it = meta.find("bibliography");
    bibPath_ = it == meta.end() ? QString() : QDir(docDir()).filePath(qs(it->second));

    placi::Document doc;
    try {
        doc = placi::parse_document(src, quiet, ss(docPath()), placi::ParseOptions::from_style(style_));
    } catch (const std::exception&) {
    }

    // Outline and labels.
    outlineItems_.clear();
    ids_.clear();
    outline_->clear();
    QVector<QTreeWidgetItem*> parents(7, nullptr);
    std::function<void(const placi::Node&)> walk = [&](const placi::Node& n) {
        if (auto id = n.attr("id"); !id.empty() && n.type != placi::NodeType::CrossRef) ids_ << qs(id);
        if (n.type == placi::NodeType::Heading) {
            QString title = qs(placi::plain_text(n));
            outlineItems_.append({static_cast<int>(n.line), n.level, title});
            QString shown = title;
            for (auto& t : n.tags) shown += QStringLiteral("  {") + qs(t) + '}';
            QTreeWidgetItem* parent = nullptr;
            for (int l = n.level - 1; l >= 1 && !parent; --l) parent = parents[l];
            auto* item = parent ? new QTreeWidgetItem(parent, {shown}) : new QTreeWidgetItem(outline_, {shown});
            item->setData(0, Qt::UserRole, static_cast<int>(n.line));
            parents[n.level] = item;
            for (int l = n.level + 1; l <= 6; ++l) parents[l] = nullptr;
        }
        for (auto& c : n.children) walk(c);
    };
    walk(doc.root);
    outline_->expandAll();

    // Menus that depend on the style.
    tagMenu_->clear();
    auto addTag = [&](const QString& label, const QString& token) {
        tagMenu_->addAction(label, this, [this, token] { toggleTag(token); });
    };
    addTag(tr("Tanpa nomor  (-)"), QStringLiteral("-"));
    tagMenu_->addSeparator();
    for (auto& [k, t] : style_.tags) {
        QString label = qs(k);
        if (!t.aliases.empty()) label += QStringLiteral("  (") + qs(t.aliases.front()) + ')';
        addTag(label, qs(k));
    }
    tagMenu_->addSeparator();
    addTag(tr("Daftar pustaka  (pustaka)"), QStringLiteral("pustaka"));
    addTag(tr("Sampul  (sampul)"), QStringLiteral("sampul"));

    sectionMenu_->clear();
    for (auto& [k, s] : style_.sections) {
        QString name = qs(k);
        sectionMenu_->addAction(name, this, [this, name] { wrapInSection(name); });
    }
    if (style_.sections.empty()) sectionMenu_->addAction(tr("(style ini tidak punya section)"))->setEnabled(false);

    lists_.clear();
    listMenu_->clear();
    listMenu_->addAction(tr("Daftar Isi"), this, [this] { insertListHeading(QStringLiteral("isi"), QStringLiteral("DAFTAR ISI")); });
    for (auto& [k, l] : style_.lists) {
        lists_.append({qs(k), qs(l.label), qs(l.prefix), qs(l.kind)});
        QString key = qs(k), title = qs(l.title);
        listMenu_->addAction(title, this, [this, key, title] { insertListHeading(key, title); });
    }
    listMenu_->addSeparator();
    listMenu_->addAction(tr("Tambah Daftar…"), this, &MainWindow::addList);

    refMenu_->clear();
    for (auto& id : ids_) refMenu_->addAction(id, this, [this, id] { editor_->insertPlainText('@' + id); });
    if (ids_.isEmpty()) refMenu_->addAction(tr("(belum ada label, mis. {#fig:nama})"))->setEnabled(false);

    build_.setDocumentPath(docPath());
    build_.requestPreview(text);
}

void MainWindow::onBuildStarted(bool exporting) {
    status_->setText(exporting ? tr("Mengekspor PDF…") : tr("Memperbarui pratinjau…"));
}

void MainWindow::onBuildFinished(const BuildReport& r) {
    problems_->clear();
    int errors = 0, warnings = 0;
    const QString doc = QFileInfo(docPath()).absoluteFilePath();
    for (auto& p : r.problems) {
        bool here = p.file.isEmpty() || QFileInfo(p.file).absoluteFilePath() == doc;
        QString where = p.line > 0 ? (here ? tr("baris %1: ").arg(p.line) : QFileInfo(p.file).fileName() + QStringLiteral(":%1: ").arg(p.line)) : QString();
        auto* item = new QListWidgetItem(style()->standardIcon(p.error ? QStyle::SP_MessageBoxCritical : QStyle::SP_MessageBoxWarning),
                                         where + p.message, problems_);
        if (here) item->setData(Qt::UserRole, p.line);
        (p.error ? errors : warnings)++;
    }
    // LaTeX errors already arrive as problems, mapped to Markdown lines; show the
    // raw engine messages only when nothing was mapped.
    if (!errors)
        for (auto& e : r.latexErrors) {
            new QListWidgetItem(style()->standardIcon(QStyle::SP_MessageBoxCritical), tr("LaTeX: ") + e, problems_);
            ++errors;
        }

    QString summary = r.ok ? tr("Selesai dalam %1 dtk").arg(r.millis / 1000.0, 0, 'f', 1) : tr("Gagal");
    if (errors) summary += tr(" · %1 error").arg(errors);
    if (warnings) summary += tr(" · %1 peringatan").arg(warnings);
    status_->setText(summary);

    if (!r.ok) return;
    if (r.exported) {
        statusBar()->showMessage(tr("PDF disimpan: %1").arg(r.pdf), 8000);
        return;
    }
    // Keep the reader where they were.
    int page = view_->pageNavigator()->currentPage();
    QPointF where = view_->pageNavigator()->currentLocation();
    pdf_->load(r.pdf);
    shownPdf_ = r.pdf;
    lastOutlineIndex_ = -1;
    if (follow_->isChecked()) {
        followCursor();  // show the section being written, which may have moved
    } else if (page > 0 && page < pdf_->pageCount()) {
        view_->pageNavigator()->jump(page, where);
    }
}

void MainWindow::onCursorMoved() {
    // Reflect the current line in the format box.
    auto h = parseHeadingLine(editor_->currentLineText(), delims_);
    format_->setCurrentIndex(qBound(0, h.level, 5));
    if (follow_ && follow_->isChecked()) followTimer_.start();
}

// Moves the preview to the page of the section the cursor is in, using the PDF bookmarks.
void MainWindow::followCursor() {
    if (pdf_->status() != QPdfDocument::Status::Ready || outlineItems_.isEmpty()) return;
    const int line = editor_->currentLineNumber();
    int index = -1;
    for (int i = 0; i < outlineItems_.size(); ++i)
        if (outlineItems_[i].line > 0 && outlineItems_[i].line <= line) index = i;
    if (index < 0 || index == lastOutlineIndex_) return;
    lastOutlineIndex_ = index;
    // The n-th heading with this title is the n-th bookmark with it.
    const QString wanted = key(outlineItems_[index].title);
    int occurrence = 0;
    for (int i = 0; i < index; ++i) occurrence += key(outlineItems_[i].title) == wanted;
    int page = -1;
    std::function<void(const QModelIndex&)> find = [&](const QModelIndex& parent) {
        for (int r = 0; r < bookmarks_->rowCount(parent) && page < 0; ++r) {
            QModelIndex idx = bookmarks_->index(r, 0, parent);
            if (key(idx.data(int(QPdfBookmarkModel::Role::Title)).toString()) == wanted && occurrence-- == 0)
                page = idx.data(int(QPdfBookmarkModel::Role::Page)).toInt();
            find(idx);
        }
    };
    find({});
    trace(QStringLiteral("follow line=%1 heading='%2' bookmarks=%3 page=%4").arg(line).arg(wanted).arg(bookmarks_->rowCount()).arg(page));
    if (page >= 0) view_->pageNavigator()->jump(page, QPointF(), view_->pageNavigator()->currentZoom());
}

// ------------------------------------------------------------------ editing commands

void MainWindow::setHeadingLevel(int level) {
    editor_->replaceCurrentLine(placi::gui::setHeadingLevel(editor_->currentLineText(), level, delims_));
    editor_->setFocus();
}

void MainWindow::toggleTag(const QString& token) {
    QString line = editor_->currentLineText();
    if (!parseHeadingLine(line, delims_).level) {
        statusBar()->showMessage(tr("Tag dipasang pada judul: letakkan kursor di baris judul (#)."), 5000);
        return;
    }
    editor_->replaceCurrentLine(toggleHeadingToken(line, token, delims_));
    editor_->setFocus();
}

void MainWindow::wrapInSection(const QString& name) {
    const QString fence = QStringLiteral("::: ") + name;
    if (!editor_->textCursor().hasSelection()) {
        editor_->insertBlock(fence + QStringLiteral("\n\n") + fence);
        editor_->moveCursor(QTextCursor::Up);
        editor_->moveCursor(QTextCursor::Up);
        return;
    }
    editor_->wrapSelectedLines(fence, fence);
}

void MainWindow::insertListHeading(const QString& listKey, const QString& title) {
    QString tags = style_.find_tag("pembuka") ? QStringLiteral("pembuka daftar ") : QStringLiteral("daftar ");
    editor_->insertBlock(QStringLiteral("# ") + title + ' ' + delims_.open + tags + listKey + delims_.close);
}

void MainWindow::addList() {
    AddListDialog d(this);
    if (d.exec() != QDialog::Accepted) return;
    NewList l = d.list();
    if (l.key.isEmpty()) return;
    QString heading = QStringLiteral("# ") + l.title + ' ' + delims_.open +
                      (style_.find_tag("pembuka") ? QStringLiteral("pembuka daftar ") : QStringLiteral("daftar ")) + l.key + delims_.close;
    // Write the list into the front matter; keep the cursor on the same line.
    const int line = editor_->currentLineNumber();
    const int before = editor_->document()->blockCount();
    QTextCursor all(editor_->document());
    all.select(QTextCursor::Document);
    all.insertText(addFrontMatterList(editor_->toPlainText(), l));
    editor_->goToLine(line + (editor_->document()->blockCount() - before));
    if (d.insertHeading()) editor_->insertBlock(heading);
    statusBar()->showMessage(tr("Daftar '%1' ditambahkan. Beri label {#%2:nama} pada item agar masuk ke daftar ini.").arg(l.title, l.label), 8000);
}

void MainWindow::insertImage() {
    if (path_.isEmpty() &&
        QMessageBox::question(this, tr("Sisipkan Gambar"), tr("Simpan dokumen dulu agar gambar disimpan di sebelahnya?")) == QMessageBox::Yes &&
        !saveAs())
        return;
    InsertImageDialog d(docDir(), lists_, delims_, this);
    if (d.exec() == QDialog::Accepted) editor_->insertBlock(d.markdown());
}

void MainWindow::insertTable() {
    InsertTableDialog d(lists_, delims_, this);
    if (d.exec() == QDialog::Accepted) editor_->insertBlock(d.markdown());
}

void MainWindow::insertMath() {
    InsertMathDialog d(delims_, this);
    if (d.exec() != QDialog::Accepted) return;
    QString md = d.markdown();
    if (md.startsWith('$')) editor_->insertPlainText(md);
    else editor_->insertBlock(md);
}

void MainWindow::insertCitation() {
    QString bib = bibPath_;
    if (bib.isEmpty() || !QFileInfo::exists(bib)) {
        auto r = QMessageBox::question(this, tr("Sitasi"), tr("Dokumen belum punya berkas referensi (.bib). Pilih atau buat sekarang?"));
        if (r != QMessageBox::Yes) return;
        QString f = QFileDialog::getSaveFileName(this, tr("Berkas referensi"), QDir(docDir()).filePath(QStringLiteral("references.bib")),
                                                 tr("BibTeX (*.bib)"), nullptr, QFileDialog::DontConfirmOverwrite);
        if (f.isEmpty()) return;
        if (!QFileInfo::exists(f)) {
            QFile nf(f);
            nf.open(QIODevice::WriteOnly);
        }
        QString rel = QDir(docDir()).relativeFilePath(f);
        QTextCursor all(editor_->document());
        all.select(QTextCursor::Document);
        const int line = editor_->currentLineNumber(), before = editor_->document()->blockCount();
        all.insertText(setFrontMatterValue(editor_->toPlainText(), QStringLiteral("bibliography"), rel));
        editor_->goToLine(line + (editor_->document()->blockCount() - before));
        bib = f;
        bibPath_ = f;
    }
    CitationDialog d(bib, this);
    if (d.exec() == QDialog::Accepted) editor_->insertPlainText(d.markdown());
}

void MainWindow::insertPageBreak() { editor_->insertBlock(delims_.open + QStringLiteral("halaman-baru") + delims_.close); }

void MainWindow::chooseStyle(int index) {
    QString name = styleBox_->itemData(index).toString();
    if (name.isEmpty()) return;
    QTextCursor all(editor_->document());
    all.select(QTextCursor::Document);
    const int line = editor_->currentLineNumber(), before = editor_->document()->blockCount();
    all.insertText(setFrontMatterValue(editor_->toPlainText(), QStringLiteral("style"), name));
    editor_->goToLine(line + (editor_->document()->blockCount() - before));
    styleBox_->setCurrentIndex(0);
}

void MainWindow::showSyntaxHelp() {
    auto* b = new QTextBrowser;
    b->setHtml(tr(R"(<h2>Sintaks PlaciDocs</h2>
<h3>Tag judul (di akhir judul)</h3>
<pre># Kata Pengantar {pembuka}
# DAFTAR ISI {pembuka daftar isi}
# DAFTAR GAMBAR {pembuka daftar gambar}
# Pendahuluan {#sec:intro}
# Ucapan Terima Kasih {-}           (tanpa nomor)
# DAFTAR PUSTAKA {penutup pustaka}
# Kuesioner {lampiran}</pre>
<p>Tag berlaku untuk judul itu beserta isinya sampai judul berikutnya yang setingkat atau lebih tinggi.
Kurung kurawal biasa ditulis <code>\{</code> dan <code>\}</code>.</p>
<h3>Section format (seperti section break di Word)</h3>
<pre>::: Landscape
| tabel lebar ... |
::: Landscape</pre>
<h3>Isi</h3>
<pre>![Keterangan](img/a.png){#fig:a width=70%}
Tabel: Keterangan {#tbl:a}
$x^2$   $$ E = mc^2 $$ {#eq:e}
```math {#eq:s caption="Masuk Daftar Rumus"}
a &amp;= b \\
```
[@kunci]  [@a, hlm. 5; @b]  @kunci  [-@kunci]
@fig:a  @tbl:a  @eq:e
{halaman-baru}</pre>)"));
    auto* d = new QDialog(this);
    d->setWindowTitle(tr("Panduan sintaks"));
    auto* v = new QVBoxLayout(d);
    v->addWidget(b);
    d->resize(640, 620);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->show();
}

}  // namespace placi::gui
