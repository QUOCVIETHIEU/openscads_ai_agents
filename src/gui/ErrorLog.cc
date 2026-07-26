#include "gui/ErrorLog.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QColor>
#include <QFont>
#include <QFontInfo>
#include <QFrame>
#include <QHeaderView>
#include <QList>
#include <QResizeEvent>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QString>
#include <QWidget>
#include <filesystem>

#include "openscad_gui.h"
#include "utils/printutils.h"

ErrorLog::ErrorLog(QWidget *parent) : QWidget(parent)
{
  setupUi(this);
  initGUI();
  applyTheme();
}

void ErrorLog::initGUI()
{
  row = 0;
  QList<QString> labels = QList<QString>()
                          << QString("Group") << QString("File") << QString("Line") << QString("Info");

  const int numColumns = labels.count();
  this->errorLogModel = new QStandardItemModel(row, numColumns, logTable);

  errorLogModel->setHorizontalHeaderLabels(labels);
  logTable->verticalHeader()->hide();
  logTable->setModel(errorLogModel);
  logTable->setSelectionMode(QAbstractItemView::SelectionMode::SingleSelection);
  logTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  logTable->setShowGrid(false);
  logTable->setAlternatingRowColors(false);
  logTable->setFrameShape(QFrame::NoFrame);
  logTable->setColumnWidth(errorLog_column::group, 80);
  logTable->setColumnWidth(errorLog_column::file, 200);
  logTable->setColumnWidth(errorLog_column::lineNo, 80);
  logTable->addAction(actionRowSelected);
  // last column will stretch itself

  connect(logTable->horizontalHeader(), &QHeaderView::sectionResized, this, &ErrorLog::onSectionResized);
}

void ErrorLog::applyTheme()
{
  const bool dark = isDarkMode();
  const QString bg = dark ? QStringLiteral("#1e1e1e") : QStringLiteral("#f8f8f8");
  const QString header = dark ? QStringLiteral("#252526") : QStringLiteral("#f3f3f3");
  const QString border = dark ? QStringLiteral("#2b2b2b") : QStringLiteral("#e5e5e5");
  const QString text = dark ? QStringLiteral("#cccccc") : QStringLiteral("#333333");
  const QString muted = dark ? QStringLiteral("#969696") : QStringLiteral("#6e6e6e");
  const QString hover = dark ? QStringLiteral("#2a2d2e") : QStringLiteral("#e8e8e8");
  const QString selected = dark ? QStringLiteral("#094771") : QStringLiteral("#e8f1ff");

  // Same proportional UI face as Project Explorer / Console.
  QFont uiFont = QApplication::font();
  uiFont.setPointSize(12);
  uiFont.setWeight(QFont::Normal);
  uiFont.setFixedPitch(false);
  uiFont.setStyleHint(QFont::SansSerif);
  setFont(uiFont);
  if (logTable) logTable->setFont(uiFont);
  if (errorLogComboBox) errorLogComboBox->setFont(uiFont);
  if (errorLogShow) {
    QFont labelFont = uiFont;
    labelFont.setPointSize(11);
    labelFont.setWeight(QFont::Bold);
    errorLogShow->setFont(labelFont);
  }
  const QString family = QFontInfo{uiFont}.family();

  setStyleSheet(QStringLiteral(R"(
    QWidget#errorLogWidget {
      background: %1;
      border: none;
      font-family: "%8";
      font-size: 12px;
    }
    QLabel#errorLogShow {
      color: %5;
      font-family: "%8";
      font-size: 11px;
      font-weight: 700;
      letter-spacing: 0.4px;
      padding-left: 4px;
    }
    QComboBox#errorLogComboBox {
      background: %1;
      color: %4;
      border: 1px solid %3;
      border-radius: 4px;
      padding: 2px 8px;
      min-height: 22px;
      font-family: "%8";
      font-size: 12px;
    }
    QComboBox#errorLogComboBox:hover {
      background: %6;
    }
    QComboBox#errorLogComboBox::drop-down {
      border: none;
      width: 18px;
    }
    QTableView#logTable {
      background: %1;
      color: %4;
      border: none;
      outline: 0;
      gridline-color: transparent;
      font-family: "%8";
      font-size: 12px;
      font-weight: 400;
      selection-background-color: %7;
      selection-color: %4;
    }
    QTableView#logTable::item {
      padding: 2px 6px;
      min-height: 22px;
      border: none;
    }
    QTableView#logTable::item:hover {
      background: %6;
    }
    QTableView#logTable::item:selected {
      background: %7;
      color: %4;
    }
    QHeaderView::section {
      background: %2;
      color: %5;
      border: none;
      border-bottom: 1px solid %3;
      border-right: 1px solid %3;
      padding: 4px 8px;
      font-family: "%8";
      font-size: 11px;
      font-weight: 700;
      letter-spacing: 0.4px;
    }
  )")
                  .arg(bg, header, border, text, muted, hover, selected, family));
}

void ErrorLog::toErrorLog(const Message& logMsg)
{
  lastMessages.push_back(logMsg);
  QString currGroup = errorLogComboBox->currentText();

  // handle combobox
  if (errorLogComboBox->currentIndex() == 0)
    ;
  else if (currGroup.toStdString() != getGroupName(logMsg.group)) return;

  showtheErrorInGUI(logMsg);
}

void ErrorLog::showtheErrorInGUI(const Message& logMsg)
{
  auto *groupName = new QStandardItem(QString::fromStdString(getGroupName(logMsg.group)));
  groupName->setEditable(false);

  if (logMsg.group == message_group::Error)
    groupName->setForeground(QColor(isDarkMode() ? QStringLiteral("#f14c4c")
                                                 : QStringLiteral("#e51400")));
  else if (logMsg.group == message_group::Warning)
    groupName->setForeground(QColor(isDarkMode() ? QStringLiteral("#cca700")
                                                 : QStringLiteral("#bf8803")));

  errorLogModel->setItem(row, errorLog_column::group, groupName);

  QStandardItem *fileName;
  QStandardItem *lineNo;
  if (!logMsg.loc.isNone()) {
    const auto& filePath = logMsg.loc.filePath();
    if (is_regular_file(filePath)) {
      const auto path = QString::fromStdString(filePath.generic_string());
      fileName = new QStandardItem(QString::fromStdString(filePath.filename().generic_string()));
      fileName->setToolTip(path);
      fileName->setData(path, Qt::UserRole);
    } else {
      fileName = new QStandardItem(QString());
    }
    lineNo = new QStandardItem(QString::number(logMsg.loc.firstLine()));
  } else {
    fileName = new QStandardItem(QString());
    lineNo = new QStandardItem(QString());
  }
  fileName->setEditable(false);
  lineNo->setEditable(false);
  lineNo->setTextAlignment(Qt::AlignVCenter | Qt::AlignRight);
  errorLogModel->setItem(row, errorLog_column::file, fileName);
  errorLogModel->setItem(row, errorLog_column::lineNo, lineNo);

  auto *msg = new QStandardItem(QString::fromStdString(logMsg.msg));
  msg->setEditable(false);
  errorLogModel->setItem(row, errorLog_column::message, msg);
  errorLogModel->setRowCount(++row);

  this->resize();

  if (!logTable->selectionModel()->hasSelection()) {
    logTable->selectRow(0);
  }
}

void ErrorLog::resize()
{
  logTable->resizeRowsToContents();
}

void ErrorLog::onSectionResized(int /*logicalIndex*/, int /*oldSize*/, int /*newSize*/)
{
  this->resize();
}

void ErrorLog::resizeEvent(QResizeEvent *event)
{
  QWidget::resizeEvent(event);
  this->resize();
}

void ErrorLog::clearModel()
{
  errorLogModel->clear();
  initGUI();
  lastMessages.clear();
}

int ErrorLog::getLine(int row, int col)
{
  return logTable->model()->index(row, col).data().toInt();
}

void ErrorLog::on_errorLogComboBox_currentTextChanged(const QString& group)
{
  errorLogModel->clear();
  initGUI();
  for (auto& lastMessage : lastMessages) {
    if (group == QString::fromStdString("All") ||
        group == QString::fromStdString(getGroupName(lastMessage.group))) {
      showtheErrorInGUI(lastMessage);
    }
  }
}

void ErrorLog::on_logTable_doubleClicked(const QModelIndex& index)
{
  onIndexSelected(index);
}

void ErrorLog::on_actionRowSelected_triggered(bool)
{
  const auto indexes = logTable->selectionModel()->selectedRows(0);
  if (indexes.size() == 1) {
    onIndexSelected(indexes.first());
  }
}

void ErrorLog::onIndexSelected(const QModelIndex& index)
{
  if (index.isValid()) {
    const int r = index.row();
    const int line = getLine(r, errorLog_column::lineNo);
    const auto path = logTable->model()->index(r, errorLog_column::file).data(Qt::UserRole).toString();
    emit openFile(path, line - 1);
  }
}
