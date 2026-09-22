#include "gui/Dialogs.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace placi::gui {

namespace {

QDialogButtonBox* okCancel(QDialog* d) {
    auto* b = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, d);
    QObject::connect(b, &QDialogButtonBox::accepted, d, &QDialog::accept);
    QObject::connect(b, &QDialogButtonBox::rejected, d, &QDialog::reject);
    return b;
}

void fillLists(QComboBox* box, const QList<ListChoice>& lists, const QStringList& kinds) {
    for (auto& l : lists)
        if (kinds.contains(l.kind)) box->addItem(l.prefix + QStringLiteral("  (") + l.key + ')', l.label);
}

// "{#fig:x width=70%}" with the active delimiters; empty when there is nothing to say.
QString attrBlock(const TagDelimiters& d, const QStringList& parts) {
    if (parts.isEmpty()) return {};
    return d.open + parts.join(' ') + d.close;
}

QString quoted(QString s) { return '"' + s.replace('"', '\'') + '"'; }

}  // namespace

// ---------------------------------------------------------------- image

InsertImageDialog::InsertImageDialog(const QString& docDir, const QList<ListChoice>& lists, const TagDelimiters& d,
                                     QWidget* parent)
    : QDialog(parent), docDir_(docDir), delims_(d), lists_(lists) {
    setWindowTitle(tr("Sisipkan Gambar"));
    auto* form = new QFormLayout;
    file_ = new QLineEdit;
    auto* browse = new QPushButton(tr("Pilih…"));
    auto* fileRow = new QHBoxLayout;
    fileRow->addWidget(file_);
    fileRow->addWidget(browse);
    form->addRow(tr("Berkas"), fileRow);
    caption_ = new QLineEdit;
    form->addRow(tr("Keterangan (caption)"), caption_);
    list_ = new QComboBox;
    fillLists(list_, lists, {QStringLiteral("figure"), QStringLiteral("float")});
    form->addRow(tr("Masuk daftar"), list_);
    label_ = new QLineEdit;
    label_->setPlaceholderText(tr("otomatis dari nama berkas"));
    form->addRow(tr("Label"), label_);
    width_ = new QSpinBox;
    width_->setRange(10, 100);
    width_->setValue(80);
    width_->setSuffix(QStringLiteral(" %"));
    form->addRow(tr("Lebar"), width_);
    auto* v = new QVBoxLayout(this);
    v->addLayout(form);
    v->addWidget(okCancel(this));
    connect(browse, &QPushButton::clicked, this, &InsertImageDialog::browse);
    resize(520, sizeHint().height());
}

void InsertImageDialog::browse() {
    QString f = QFileDialog::getOpenFileName(this, tr("Pilih gambar"), docDir_,
                                             tr("Gambar (*.png *.jpg *.jpeg *.pdf)"));
    if (f.isEmpty()) return;
    QDir dir(docDir_);
    QString rel = dir.relativeFilePath(f);
    if (rel.startsWith(QLatin1String(".."))) {
        // Keep the document self-contained: copy outside images into img/.
        dir.mkpath(QStringLiteral("img"));
        QString dest = dir.filePath(QStringLiteral("img/") + QFileInfo(f).fileName());
        if (!QFile::exists(dest) && !QFile::copy(f, dest)) {
            QMessageBox::warning(this, windowTitle(), tr("Tidak bisa menyalin gambar ke folder img/."));
            return;
        }
        rel = dir.relativeFilePath(dest);
    }
    file_->setText(rel);
    if (caption_->text().isEmpty()) caption_->setText(QFileInfo(f).completeBaseName());
}

QString InsertImageDialog::markdown() const {
    QString label = label_->text().trimmed();
    QString prefix = list_->currentData().toString();
    if (label.isEmpty()) label = slugify(QFileInfo(file_->text()).completeBaseName());
    QStringList parts;
    if (!label.isEmpty()) parts << '#' + (label.contains(':') ? label : prefix + ':' + label);
    if (width_->value() != 80) parts << QStringLiteral("width=%1%").arg(width_->value());
    return QStringLiteral("![%1](%2)").arg(caption_->text(), file_->text()) + attrBlock(delims_, parts);
}

// ---------------------------------------------------------------- table

InsertTableDialog::InsertTableDialog(const QList<ListChoice>& lists, const TagDelimiters& d, QWidget* parent)
    : QDialog(parent), delims_(d), lists_(lists) {
    setWindowTitle(tr("Sisipkan Tabel"));
    auto* form = new QFormLayout;
    rows_ = new QSpinBox;
    rows_->setRange(1, 200);
    rows_->setValue(3);
    cols_ = new QSpinBox;
    cols_->setRange(1, 20);
    cols_->setValue(3);
    form->addRow(tr("Baris isi"), rows_);
    form->addRow(tr("Kolom"), cols_);
    caption_ = new QLineEdit;
    form->addRow(tr("Keterangan (caption)"), caption_);
    list_ = new QComboBox;
    fillLists(list_, lists, {QStringLiteral("table"), QStringLiteral("float")});
    form->addRow(tr("Masuk daftar"), list_);
    label_ = new QLineEdit;
    label_->setPlaceholderText(tr("otomatis dari keterangan"));
    form->addRow(tr("Label"), label_);
    auto* v = new QVBoxLayout(this);
    v->addLayout(form);
    v->addWidget(okCancel(this));
}

QString InsertTableDialog::markdown() const {
    const int cols = cols_->value();
    QStringList lines;
    QString caption = caption_->text().trimmed();
    if (!caption.isEmpty()) {
        QString label = label_->text().trimmed();
        if (label.isEmpty()) label = slugify(caption);
        QString prefix = list_->currentData().toString();
        QStringList parts{'#' + (label.contains(':') ? label : prefix + ':' + label)};
        lines << QStringLiteral("Tabel: ") + caption + ' ' + attrBlock(delims_, parts) << QString();
    }
    QString head = QStringLiteral("|"), sep = QStringLiteral("|"), row = QStringLiteral("|");
    for (int c = 0; c < cols; ++c) {
        head += QStringLiteral(" Kolom %1 |").arg(c + 1);
        sep += QStringLiteral("---|");
        row += QStringLiteral("   |");
    }
    lines << head << sep;
    for (int r = 0; r < rows_->value(); ++r) lines << row;
    return lines.join('\n');
}

// ---------------------------------------------------------------- math

InsertMathDialog::InsertMathDialog(const TagDelimiters& d, QWidget* parent) : QDialog(parent), delims_(d) {
    setWindowTitle(tr("Sisipkan Rumus (LaTeX)"));
    latex_ = new QPlainTextEdit;
    latex_->setPlaceholderText(QStringLiteral("E = mc^2\n\n% Beberapa baris: pisahkan dengan \\\\ dan ratakan dengan &"));
    QFont mono(QStringLiteral("Consolas"));
    latex_->setFont(mono);
    inline_ = new QCheckBox(tr("Di dalam kalimat ($...$)"));
    numbered_ = new QCheckBox(tr("Bernomor"));
    numbered_->setChecked(true);
    label_ = new QLineEdit;
    label_->setPlaceholderText(QStringLiteral("eq:nama"));
    caption_ = new QLineEdit;
    caption_->setPlaceholderText(tr("isi agar muncul di Daftar Rumus"));
    auto* form = new QFormLayout;
    form->addRow(inline_);
    form->addRow(numbered_);
    form->addRow(tr("Label"), label_);
    form->addRow(tr("Keterangan"), caption_);
    auto* v = new QVBoxLayout(this);
    v->addWidget(new QLabel(tr("Tulis rumus dalam notasi LaTeX:")));
    v->addWidget(latex_);
    v->addLayout(form);
    v->addWidget(okCancel(this));
    connect(inline_, &QCheckBox::toggled, this, [this](bool on) {
        numbered_->setEnabled(!on);
        label_->setEnabled(!on);
        caption_->setEnabled(!on);
    });
    resize(560, 360);
}

QString InsertMathDialog::markdown() const {
    QString tex = latex_->toPlainText().trimmed();
    if (inline_->isChecked()) return '$' + QString(tex).replace('\n', ' ') + '$';
    QStringList parts;
    if (numbered_->isChecked()) {
        QString label = label_->text().trimmed();
        if (label.isEmpty()) label = QStringLiteral("eq:") + (caption_->text().isEmpty() ? QStringLiteral("rumus") : slugify(caption_->text()));
        if (!label.contains(':')) label = QStringLiteral("eq:") + label;
        parts << '#' + label;
        if (!caption_->text().trimmed().isEmpty()) parts << QStringLiteral("caption=") + quoted(caption_->text().trimmed());
    } else {
        parts << QStringLiteral(".unnumbered");
    }
    return QStringLiteral("```math ") + attrBlock(delims_, parts) + '\n' + tex + QStringLiteral("\n```");
}

// ---------------------------------------------------------------- citation

CitationDialog::CitationDialog(const QString& bibPath, QWidget* parent) : QDialog(parent), bibPath_(bibPath) {
    setWindowTitle(tr("Sisipkan Sitasi"));
    entries_ = readBibFile(bibPath);
    search_ = new QLineEdit;
    search_->setPlaceholderText(tr("Cari penulis, judul, tahun, atau kunci…"));
    list_ = new QListWidget;
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    locator_ = new QLineEdit;
    locator_->setPlaceholderText(tr("mis. hlm. 12"));
    mode_ = new QComboBox;
    mode_->addItem(tr("Dalam kurung — (Doe, 2020)"), QStringLiteral("paren"));
    mode_->addItem(tr("Naratif — Doe (2020)"), QStringLiteral("text"));
    mode_->addItem(tr("Tahun saja — (2020)"), QStringLiteral("year"));
    auto* add = new QPushButton(tr("Referensi baru…"));
    auto* form = new QFormLayout;
    form->addRow(tr("Bentuk"), mode_);
    form->addRow(tr("Halaman/bagian"), locator_);
    auto* v = new QVBoxLayout(this);
    v->addWidget(new QLabel(bibPath.isEmpty() ? tr("Dokumen belum punya berkas .bib (atur 'bibliography' di front matter).")
                                              : QFileInfo(bibPath).fileName()));
    v->addWidget(search_);
    v->addWidget(list_);
    v->addLayout(form);
    auto* buttons = okCancel(this);
    buttons->addButton(add, QDialogButtonBox::ActionRole);
    v->addWidget(buttons);
    connect(search_, &QLineEdit::textChanged, this, &CitationDialog::filter);
    connect(list_, &QListWidget::itemDoubleClicked, this, &QDialog::accept);
    connect(add, &QPushButton::clicked, this, &CitationDialog::addEntry);
    add->setEnabled(!bibPath.isEmpty());
    filter({});
    resize(620, 460);
}

void CitationDialog::filter(const QString& text) {
    list_->clear();
    for (auto& e : entries_) {
        QString shown = QStringLiteral("%1 (%2) — %3   [%4]").arg(e.author, e.year, e.title, e.key);
        if (!text.isEmpty() && !shown.contains(text, Qt::CaseInsensitive)) continue;
        auto* item = new QListWidgetItem(shown, list_);
        item->setData(Qt::UserRole, e.key);
    }
    if (list_->count()) list_->setCurrentRow(0);
}

void CitationDialog::addEntry() {
    QDialog d(this);
    d.setWindowTitle(tr("Referensi baru"));
    auto* form = new QFormLayout;
    auto* key = new QLineEdit;
    auto* type = new QComboBox;
    type->addItems({QStringLiteral("book"), QStringLiteral("article"), QStringLiteral("inproceedings"), QStringLiteral("misc")});
    auto* author = new QLineEdit;
    author->setPlaceholderText(tr("Belakang, Depan and Belakang2, Depan2"));
    auto* title = new QLineEdit;
    auto* year = new QLineEdit;
    form->addRow(tr("Kunci"), key);
    form->addRow(tr("Jenis"), type);
    form->addRow(tr("Penulis"), author);
    form->addRow(tr("Judul"), title);
    form->addRow(tr("Tahun"), year);
    auto* v = new QVBoxLayout(&d);
    v->addLayout(form);
    v->addWidget(okCancel(&d));
    connect(author, &QLineEdit::textChanged, &d, [=] {
        QString last = author->text().section(',', 0, 0).section(' ', -1).toLower();
        key->setPlaceholderText(last + year->text());
    });
    if (d.exec() != QDialog::Accepted) return;
    BibEntry e{key->text().trimmed().isEmpty() ? key->placeholderText() : key->text().trimmed(), type->currentText(),
               author->text(), title->text(), year->text()};
    QString err;
    if (e.key.isEmpty() || !appendBibEntry(bibPath_, e, &err)) {
        QMessageBox::warning(this, windowTitle(), tr("Referensi tidak tersimpan. %1").arg(err));
        return;
    }
    entries_ = readBibFile(bibPath_);
    search_->setText(e.key);
}

QString CitationDialog::markdown() const {
    QStringList keys;
    for (auto* item : list_->selectedItems()) keys << item->data(Qt::UserRole).toString();
    if (keys.isEmpty()) return {};
    const QString mode = mode_->currentData().toString();
    const QString loc = locator_->text().trimmed();
    if (mode == QLatin1String("text") && keys.size() == 1) return '@' + keys[0];
    QStringList items;
    for (auto& k : keys) items << (mode == QLatin1String("year") ? QStringLiteral("-@") : QStringLiteral("@")) + k;
    QString body = items.join(QStringLiteral("; "));
    if (!loc.isEmpty()) body += QStringLiteral(", ") + loc;
    return '[' + body + ']';
}

// ---------------------------------------------------------------- add list

AddListDialog::AddListDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Tambah Daftar"));
    auto* form = new QFormLayout;
    name_ = new QLineEdit;
    name_->setPlaceholderText(tr("mis. grafik"));
    title_ = new QLineEdit;
    prefix_ = new QLineEdit;
    label_ = new QLineEdit;
    numbering_ = new QLineEdit(QStringLiteral("{h1}.{n}"));
    kind_ = new QComboBox;
    kind_->addItem(tr("Objek baru (gambar atau tabel)"), QStringLiteral("float"));
    kind_->addItem(tr("Gambar"), QStringLiteral("figure"));
    kind_->addItem(tr("Tabel"), QStringLiteral("table"));
    kind_->addItem(tr("Rumus"), QStringLiteral("equation"));
    insert_ = new QCheckBox(tr("Sisipkan judul daftar ini di posisi kursor"));
    insert_->setChecked(true);
    form->addRow(tr("Nama daftar"), name_);
    form->addRow(tr("Judul daftar"), title_);
    form->addRow(tr("Awalan keterangan"), prefix_);
    form->addRow(tr("Awalan label"), label_);
    form->addRow(tr("Jenis isi"), kind_);
    form->addRow(tr("Penomoran"), numbering_);
    auto* hint = new QLabel(tr("Contoh: nama <b>grafik</b>, awalan <b>Grafik</b>, label <b>gfk</b> → "
                               "tulis <code>{#gfk:penjualan}</code> pada gambar agar masuk ke Daftar Grafik."));
    hint->setWordWrap(true);
    auto* v = new QVBoxLayout(this);
    v->addLayout(form);
    v->addWidget(hint);
    v->addWidget(insert_);
    v->addWidget(okCancel(this));
    // Sensible defaults derived from the name.
    connect(name_, &QLineEdit::textChanged, this, [this](const QString& n) {
        QString cap = n.isEmpty() ? n : n.left(1).toUpper() + n.mid(1);
        title_->setPlaceholderText(QStringLiteral("DAFTAR ") + n.toUpper());
        prefix_->setPlaceholderText(cap);
        label_->setPlaceholderText(slugify(n).left(3));
    });
    resize(520, sizeHint().height());
}

NewList AddListDialog::list() const {
    auto pick = [](QLineEdit* e) { return e->text().trimmed().isEmpty() ? e->placeholderText() : e->text().trimmed(); };
    NewList l;
    l.key = slugify(name_->text()).replace('-', '_');
    l.title = pick(title_);
    l.prefix = pick(prefix_);
    l.label = pick(label_);
    l.kind = kind_->currentData().toString();
    l.numbering = numbering_->text().trimmed();
    return l;
}

bool AddListDialog::insertHeading() const { return insert_->isChecked(); }

}  // namespace placi::gui
