#include "gui/ai/FreeApiKeysDialog.h"

#include "core/AIFreeAgents.h"
#include "gui/qtgettext.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QAbstractItemView>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QApplication>
#include <QPalette>
#include <QFrame>
#include <QWidget>
#include <QSize>

namespace {

QLineEdit *makeField(QWidget *parent, const QString& text, const QString& placeholder,
                     bool password = false)
{
  auto *edit = new QLineEdit(parent);
  edit->setText(text);
  edit->setPlaceholderText(placeholder);
  edit->setClearButtonEnabled(true);
  edit->setMinimumHeight(34);
  if (password) {
    edit->setEchoMode(QLineEdit::Password);
  }
  return edit;
}

QIcon brandIcon(const QString& profileName)
{
  const std::string path = AIFreeAgents::brandIconResource(profileName.toStdString());
  if (path.empty()) return {};
  return QIcon(QString::fromStdString(path));
}

}  // namespace

FreeApiKeysDialog::FreeApiKeysDialog(QWidget *parent) : QDialog(parent)
{
  setWindowTitle(_("Free API Key Config"));
  setModal(true);
  setWindowFlag(Qt::WindowContextHelpButtonHint, false);
  setMinimumSize(680, 440);
  resize(760, 480);

  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(16, 14, 16, 14);
  root->setSpacing(12);

  auto *intro = new QLabel(
    _("Set Endpoint, Model, and API Key for each free agent. "
      "Use Set as default to choose the chat starting agent."),
    this);
  intro->setWordWrap(true);
  intro->setObjectName(QStringLiteral("freeKeysIntro"));
  root->addWidget(intro);

  auto *body = new QFrame(this);
  body->setObjectName(QStringLiteral("freeKeysBody"));
  auto *bodyLay = new QHBoxLayout(body);
  bodyLay->setContentsMargins(0, 0, 0, 0);
  bodyLay->setSpacing(0);

  providerList = new QListWidget(body);
  providerList->setObjectName(QStringLiteral("freeKeysProviderList"));
  providerList->setFixedWidth(196);
  providerList->setIconSize(QSize(18, 18));
  providerList->setSpacing(2);
  providerList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  providerList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  providerList->setFocusPolicy(Qt::NoFocus);

  pages = new QStackedWidget(body);
  pages->setObjectName(QStringLiteral("freeKeysPages"));

  for (const auto& name : AIFreeAgents::freePresetNames()) {
    const QString qname = QString::fromStdString(name);
    providerOrder.append(qname);

    AIFreeAgents::ProfileConnection conn;
    AIFreeAgents::readProfileConnection(name, conn);

    auto *item = new QListWidgetItem(brandIcon(qname), qname, providerList);
    item->setSizeHint(QSize(item->sizeHint().width(), 38));

    auto *page = new QWidget(pages);
    page->setObjectName(QStringLiteral("freeKeysPage"));
    auto *pageLay = new QVBoxLayout(page);
    pageLay->setContentsMargins(20, 16, 20, 16);
    pageLay->setSpacing(14);

    auto *header = new QWidget(page);
    auto *headerLay = new QHBoxLayout(header);
    headerLay->setContentsMargins(0, 0, 0, 0);
    headerLay->setSpacing(10);

    auto *logo = new QLabel(header);
    logo->setObjectName(QStringLiteral("freeKeysLogo"));
    logo->setFixedSize(28, 28);
    logo->setPixmap(brandIcon(qname).pixmap(28, 28));
    logo->setScaledContents(true);

    auto *titleCol = new QVBoxLayout();
    titleCol->setContentsMargins(0, 0, 0, 0);
    titleCol->setSpacing(2);
    auto *title = new QLabel(qname, header);
    title->setObjectName(QStringLiteral("freeKeysPageTitle"));
    auto *badge = new QLabel(header);
    badge->setObjectName(QStringLiteral("freeKeysDefaultBadge"));
    titleCol->addWidget(title);
    titleCol->addWidget(badge);

    auto *defaultBtn = new QPushButton(_("Set as default"), header);
    defaultBtn->setObjectName(QStringLiteral("freeKeysDefaultButton"));
    defaultBtn->setCursor(Qt::PointingHandCursor);
    defaultBtn->setFlat(true);
    connect(defaultBtn, &QPushButton::clicked, this, &FreeApiKeysDialog::onSetDefault);

    headerLay->addWidget(logo, 0, Qt::AlignVCenter);
    headerLay->addLayout(titleCol, 1);
    headerLay->addWidget(defaultBtn, 0, Qt::AlignVCenter);
    pageLay->addWidget(header);

    auto *form = new QFormLayout();
    form->setContentsMargins(0, 4, 0, 0);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(14);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignTop | Qt::AlignLeft);

    auto *endpointEdit =
      makeField(page, QString::fromStdString(conn.endpoint), _("https://…/v1"));
    auto *modelEdit =
      makeField(page, QString::fromStdString(conn.model), _("model-id"));
    const QString hint = QString::fromStdString(AIFreeAgents::apiKeySignupHint(name));
    auto *apiKeyEdit =
      makeField(page, QString::fromStdString(conn.apiKey), hint, true);
    apiKeyEdit->setToolTip(hint);

    auto *endpointLabel = new QLabel(_("Endpoint"), page);
    endpointLabel->setObjectName(QStringLiteral("freeKeysFieldLabel"));
    auto *modelLabel = new QLabel(_("Model"), page);
    modelLabel->setObjectName(QStringLiteral("freeKeysFieldLabel"));
    auto *keyLabel = new QLabel(_("API Key"), page);
    keyLabel->setObjectName(QStringLiteral("freeKeysFieldLabel"));

    form->addRow(endpointLabel, endpointEdit);
    form->addRow(modelLabel, modelEdit);
    form->addRow(keyLabel, apiKeyEdit);
    pageLay->addLayout(form);
    pageLay->addStretch(1);

    fieldsByName.insert(qname, {endpointEdit, modelEdit, apiKeyEdit, badge, defaultBtn});
    pages->addWidget(page);
  }

  bodyLay->addWidget(providerList);
  auto *divider = new QFrame(body);
  divider->setObjectName(QStringLiteral("freeKeysDivider"));
  divider->setFrameShape(QFrame::NoFrame);
  divider->setFixedWidth(1);
  bodyLay->addWidget(divider);
  bodyLay->addWidget(pages, 1);
  root->addWidget(body, 1);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  buttons->button(QDialogButtonBox::Save)->setText(_("Save"));
  buttons->button(QDialogButtonBox::Cancel)->setText(_("Cancel"));
  buttons->button(QDialogButtonBox::Save)->setDefault(true);
  connect(buttons, &QDialogButtonBox::accepted, this, &FreeApiKeysDialog::onSave);
  connect(buttons, &QDialogButtonBox::rejected, this, &FreeApiKeysDialog::reject);
  root->addWidget(buttons);

  connect(providerList, &QListWidget::currentRowChanged, this, &FreeApiKeysDialog::onProviderChanged);
  if (providerList->count() > 0) {
    providerList->setCurrentRow(0);
  }
  refreshDefaultUi();

  const bool dark = QApplication::palette().color(QPalette::Window).lightness() < 128;
  const QString bg = dark ? QStringLiteral("#1e1e1e") : QStringLiteral("#f3f3f3");
  const QString panel = dark ? QStringLiteral("#252526") : QStringLiteral("#ececec");
  const QString text = dark ? QStringLiteral("#e0e0e0") : QStringLiteral("#1f1f1f");
  const QString muted = dark ? QStringLiteral("#8c8c8c") : QStringLiteral("#6e6e73");
  const QString field = dark ? QStringLiteral("#2a2d2e") : QStringLiteral("#ffffff");
  const QString border = dark ? QStringLiteral("#3c3c3c") : QStringLiteral("#d0d0d0");
  const QString hover = dark ? QStringLiteral("#2a2d2e") : QStringLiteral("#e4e4e4");
  const QString selected = dark ? QStringLiteral("#094771") : QStringLiteral("#d6e8ff");
  const QString accent = dark ? QStringLiteral("#0a84ff") : QStringLiteral("#0071e3");

  setStyleSheet(QStringLiteral(R"(
    QDialog { background: %1; color: %3; }
    QLabel#freeKeysIntro { color: %4; font-size: 12px; background: transparent; }
    QFrame#freeKeysBody {
      background: %2; border: 1px solid %6; border-radius: 12px;
    }
    QFrame#freeKeysDivider { background: %6; border: none; max-width: 1px; }
    QListWidget#freeKeysProviderList {
      background: %2; border: none;
      border-top-left-radius: 12px; border-bottom-left-radius: 12px;
      outline: none; padding: 8px 6px; font-size: 12.5px; color: %3;
    }
    QListWidget#freeKeysProviderList::item {
      background: transparent; border: none; border-radius: 8px;
      padding: 8px 10px; margin: 1px 2px; color: %3;
    }
    QListWidget#freeKeysProviderList::item:hover { background: %7; }
    QListWidget#freeKeysProviderList::item:selected { background: %8; color: %3; }
    QStackedWidget#freeKeysPages, QWidget#freeKeysPage {
      background: %2; border: none;
      border-top-right-radius: 12px; border-bottom-right-radius: 12px;
    }
    QLabel#freeKeysLogo { background: transparent; }
    QLabel#freeKeysPageTitle {
      color: %3; font-size: 15px; font-weight: 600; background: transparent;
    }
    QLabel#freeKeysDefaultBadge {
      color: %9; font-size: 11px; background: transparent;
    }
    QPushButton#freeKeysDefaultButton {
      background: %5; color: %3; border: 1px solid %6; border-radius: 8px;
      padding: 6px 12px; font-size: 12px;
    }
    QPushButton#freeKeysDefaultButton:hover { border-color: %9; }
    QPushButton#freeKeysDefaultButton:disabled {
      color: %9; border-color: %9; background: transparent;
    }
    QLabel#freeKeysFieldLabel {
      color: %4; font-size: 12px; background: transparent; min-width: 72px;
    }
    QLineEdit {
      background: %5; color: %3; border: 1px solid %6; border-radius: 8px;
      padding: 7px 10px; font-size: 12.5px; selection-background-color: %9;
    }
    QLineEdit:focus { border: 1px solid %9; }
    QDialogButtonBox QPushButton {
      min-height: 30px; min-width: 88px; border-radius: 8px; padding: 5px 14px;
      background: %5; color: %3; border: 1px solid %6; font-size: 12px;
    }
    QDialogButtonBox QPushButton:default {
      background: %9; color: #ffffff; border: 1px solid %9;
    }
  )")
                  .arg(bg, panel, text, muted, field, border, hover, selected, accent));
}

void FreeApiKeysDialog::onProviderChanged(int row)
{
  if (!pages || row < 0 || row >= pages->count()) return;
  pages->setCurrentIndex(row);
}

void FreeApiKeysDialog::refreshDefaultUi()
{
  const QString def = QString::fromStdString(AIFreeAgents::defaultFreeAgentName());
  for (int i = 0; i < providerOrder.size(); ++i) {
    const QString& name = providerOrder.at(i);
    const bool isDefault = (name == def);
    TabFields f = fieldsByName.value(name);
    if (f.defaultBadge) {
      f.defaultBadge->setText(isDefault ? tr("Default agent") : QString());
      f.defaultBadge->setVisible(isDefault);
    }
    if (f.defaultButton) {
      f.defaultButton->setEnabled(!isDefault);
      f.defaultButton->setText(isDefault ? tr("Default") : tr("Set as default"));
    }
    if (providerList && i < providerList->count()) {
      auto *item = providerList->item(i);
      item->setText(isDefault ? (name + QStringLiteral("  ★")) : name);
      item->setIcon(brandIcon(name));
    }
  }
}

void FreeApiKeysDialog::onSetDefault()
{
  const int row = pages ? pages->currentIndex() : -1;
  if (row < 0 || row >= providerOrder.size()) return;
  const QString name = providerOrder.at(row);
  std::string err;
  if (!AIFreeAgents::setDefaultFreeAgent(name.toStdString(), err)) {
    QMessageBox::warning(this, _("Free API Key Config"),
                         tr("Could not set default:\n%1").arg(QString::fromStdString(err)));
    return;
  }
  QSettings settings;
  settings.setValue(QStringLiteral("ai/composerAgent"), name);
  refreshDefaultUi();
}

int FreeApiKeysDialog::prompt(QWidget *parent)
{
  FreeApiKeysDialog dialog(parent);
  return dialog.exec();
}

void FreeApiKeysDialog::onSave()
{
  for (const QString& name : providerOrder) {
    const TabFields f = fieldsByName.value(name);
    if (!f.endpointEdit || !f.modelEdit || !f.apiKeyEdit) continue;

    AIFreeAgents::ProfileConnection conn;
    conn.endpoint = f.endpointEdit->text().trimmed().toStdString();
    conn.model = f.modelEdit->text().trimmed().toStdString();
    conn.apiKey = f.apiKeyEdit->text().trimmed().toStdString();

    if (conn.endpoint.empty()) {
      QMessageBox::warning(this, _("Free API Key Config"),
                           tr("Endpoint is required for %1.").arg(name));
      if (providerList) providerList->setCurrentRow(providerOrder.indexOf(name));
      return;
    }
    if (conn.model.empty()) {
      QMessageBox::warning(this, _("Free API Key Config"),
                           tr("Model is required for %1.").arg(name));
      if (providerList) providerList->setCurrentRow(providerOrder.indexOf(name));
      return;
    }

    std::string err;
    if (!AIFreeAgents::writeProfileConnection(name.toStdString(), conn, err)) {
      QMessageBox::warning(this, _("Free API Key Config"),
                           tr("Could not save %1:\n%2").arg(name, QString::fromStdString(err)));
      return;
    }
  }
  accept();
}
