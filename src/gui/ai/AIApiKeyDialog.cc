#include "gui/ai/AIApiKeyDialog.h"

#include "gui/ai/AISettingsPanel.h"
#include "gui/qtgettext.h"
#include "openscad_gui.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QMessageBox>
#include <QApplication>
#include <QPalette>

AIApiKeyDialog::AIApiKeyDialog(QWidget *parent) : QDialog(parent)
{
  setWindowTitle(_("AI Settings"));
  setModal(true);
  setWindowFlag(Qt::WindowContextHelpButtonHint, false);
  setMinimumWidth(930);
  setMaximumWidth(1080);

  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 12);
  root->setSpacing(0);

  panel = new AISettingsPanel(this);
  root->addWidget(panel, 1);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  buttons->button(QDialogButtonBox::Save)->setText(_("Save"));
  buttons->button(QDialogButtonBox::Cancel)->setText(_("Cancel"));
  buttons->button(QDialogButtonBox::Save)->setDefault(true);
  auto *buttonRow = new QHBoxLayout();
  buttonRow->setContentsMargins(22, 0, 22, 4);
  buttonRow->addWidget(buttons);
  root->addLayout(buttonRow);

  connect(buttons, &QDialogButtonBox::accepted, this, &AIApiKeyDialog::onSave);
  connect(buttons, &QDialogButtonBox::rejected, this, &AIApiKeyDialog::reject);

  const bool dark = isDarkMode();
  const QString bg = dark ? QStringLiteral("#1c1c1e") : QStringLiteral("#f2f2f4");
  const QString text = dark ? QStringLiteral("#f5f5f7") : QStringLiteral("#1d1d1f");
  const QString border = dark ? QStringLiteral("#3a3a3c") : QStringLiteral("#dcdce0");
  const QString btn = dark ? QStringLiteral("#3a3a3c") : QStringLiteral("#e8e8ec");
  const QString accent = dark ? QStringLiteral("#0a84ff") : QStringLiteral("#0071e3");
  setStyleSheet(QStringLiteral(
    "QDialog { background: %1; color: %2; }"
    "QDialogButtonBox QPushButton {"
    "  min-height: 28px; min-width: 84px; border-radius: 8px; padding: 5px 14px;"
    "  background: %3; color: %2; border: 1px solid %4; font-size: 12px;"
    "}"
    "QDialogButtonBox QPushButton:default, QDialogButtonBox QPushButton[default=\"true\"] {"
    "  background: %5; color: #ffffff; border: 1px solid %5; font-weight: 600;"
    "}")
    .arg(bg, text, btn, border, accent));

  // Match previous dialog sizing.
  adjustSize();
  if (height() < 780) resize(width(), 780);
  if (height() > 1080) resize(width(), 1080);
}

int AIApiKeyDialog::prompt(QWidget *parent)
{
  AIApiKeyDialog dialog(parent);
  return dialog.exec();
}

void AIApiKeyDialog::onSave()
{
  if (!panel || !panel->saveAll()) {
    QMessageBox::warning(this, _("AI Settings"), _("Could not save AI settings."));
    return;
  }
  accept();
}
