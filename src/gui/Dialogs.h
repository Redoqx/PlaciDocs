#pragma once

#include <QDialog>

#include "gui/Bibliography.h"
#include "gui/DocumentText.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QSpinBox;

namespace placi::gui {

// A list the user can put an item in (label prefix + display name).
struct ListChoice {
    QString key;
    QString label;   // label prefix, e.g. "fig"
    QString prefix;  // "Gambar"
    QString kind;
};

class InsertImageDialog : public QDialog {
    Q_OBJECT
public:
    InsertImageDialog(const QString& docDir, const QList<ListChoice>& lists, const TagDelimiters& d, QWidget* parent = nullptr);
    QString markdown() const;

private:
    void browse();
    QString docDir_;
    TagDelimiters delims_;
    QLineEdit *file_, *caption_, *label_;
    QComboBox* list_;
    QSpinBox* width_;
    QList<ListChoice> lists_;
};

class InsertTableDialog : public QDialog {
    Q_OBJECT
public:
    InsertTableDialog(const QList<ListChoice>& lists, const TagDelimiters& d, QWidget* parent = nullptr);
    QString markdown() const;

private:
    TagDelimiters delims_;
    QSpinBox *rows_, *cols_;
    QLineEdit *caption_, *label_;
    QComboBox* list_;
    QList<ListChoice> lists_;
};

class InsertMathDialog : public QDialog {
    Q_OBJECT
public:
    InsertMathDialog(const TagDelimiters& d, QWidget* parent = nullptr);
    QString markdown() const;

private:
    TagDelimiters delims_;
    QPlainTextEdit* latex_;
    QCheckBox* inline_;
    QCheckBox* numbered_;
    QLineEdit *label_, *caption_;
};

class CitationDialog : public QDialog {
    Q_OBJECT
public:
    CitationDialog(const QString& bibPath, QWidget* parent = nullptr);
    QString markdown() const;

private:
    void filter(const QString& text);
    void addEntry();
    QString bibPath_;
    QList<BibEntry> entries_;
    QLineEdit *search_, *locator_;
    QListWidget* list_;
    QComboBox* mode_;
};

class AddListDialog : public QDialog {
    Q_OBJECT
public:
    explicit AddListDialog(QWidget* parent = nullptr);
    NewList list() const;
    bool insertHeading() const;

private:
    QLineEdit *name_, *title_, *prefix_, *label_, *numbering_;
    QComboBox* kind_;
    QCheckBox* insert_;
};

}  // namespace placi::gui
