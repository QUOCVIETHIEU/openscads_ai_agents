#include "gui/ai/AISettingsPanel.h"

#include "core/AIFreeAgents.h"
#include "core/AIService.h"
#include "gui/Preferences.h"
#include "gui/qtgettext.h"
#include "platform/PlatformUtils.h"

#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <cmath>

namespace {

constexpr int kFieldLabelW = 78;

QString aiSettingsPath()
{
  QString configPath = QString::fromStdString(PlatformUtils::userConfigPath());
  if (configPath.isEmpty()) {
    configPath = QDir::homePath() + "/.openscad";
  }
  QDir().mkpath(configPath);
  return configPath + "/ai_settings.json";
}

nlohmann::json readSettingsFile()
{
  QFile file(aiSettingsPath());
  if (!file.open(QIODevice::ReadOnly)) {
    return nlohmann::json::object();
  }
  const QByteArray data = file.readAll();
  file.close();
  auto j = nlohmann::json::parse(data.constData(), nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    return nlohmann::json::object();
  }
  return j;
}

bool writeSettingsFile(const nlohmann::json& j)
{
  QFile file(aiSettingsPath());
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  const std::string s = j.dump(4);
  file.write(s.c_str(), static_cast<qint64>(s.size()));
  file.close();
  return true;
}

// Old builds seeded restrictive defaults. Strip those exact values so the agent is
// unrestricted unless the user explicitly configures limits.
bool stripLegacyDefaultLimits(nlohmann::json& params)
{
  if (!params.is_object()) return false;
  bool changed = false;
  if (params.contains("temperature") && params["temperature"].is_number() &&
      std::fabs(params["temperature"].get<double>() - 0.7) < 1e-9) {
    params.erase("temperature");
    changed = true;
  }
  if (params.contains("max_tokens")) {
    const auto& v = params["max_tokens"];
    const bool match = (v.is_number_integer() && v.get<int>() == 2048) ||
                       (v.is_number() && std::fabs(v.get<double>() - 2048.0) < 1e-9);
    if (match) {
      params.erase("max_tokens");
      changed = true;
    }
  }
  if (params.contains("context_limit")) {
    const auto& v = params["context_limit"];
    const bool match = (v.is_number_integer() && v.get<int>() == 10) ||
                       (v.is_number() && std::fabs(v.get<double>() - 10.0) < 1e-9);
    if (match) {
      params.erase("context_limit");
      changed = true;
    }
  }
  if (params.contains("default_prompt") && params["default_prompt"].is_string() &&
      params["default_prompt"].get<std::string>() ==
        "Create a sphere with radius 10 and detail level $fn=50.") {
    params.erase("default_prompt");
    changed = true;
  }
  return changed;
}

bool migrateLegacyDefaultLimits(nlohmann::json& settings)
{
  if (!settings.contains("profiles") || !settings["profiles"].is_object()) return false;
  bool changed = false;
  for (auto& profile : settings["profiles"].items()) {
    if (!profile.value().is_object() || !profile.value().contains("params")) continue;
    if (stripLegacyDefaultLimits(profile.value()["params"])) changed = true;
  }
  return changed;
}

QLabel *makeSectionTitle(const QString& text, QWidget *parent)
{
  auto *label = new QLabel(text, parent);
  label->setObjectName(QStringLiteral("sectionTitle"));
  return label;
}

QWidget *makeFieldRow(const QString& label, QWidget *field, QWidget *parent)
{
  auto *row = new QWidget(parent);
  auto *layout = new QHBoxLayout(row);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);
  auto *lbl = new QLabel(label, row);
  lbl->setObjectName(QStringLiteral("fieldLabel"));
  lbl->setFixedWidth(kFieldLabelW);
  lbl->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
  layout->addWidget(lbl, 0, Qt::AlignVCenter);
  layout->addWidget(field, 1);
  return row;
}

QFrame *makeDivider(QWidget *parent)
{
  auto *line = new QFrame(parent);
  line->setObjectName(QStringLiteral("sectionDivider"));
  line->setFrameShape(QFrame::HLine);
  line->setFrameShadow(QFrame::Plain);
  line->setFixedHeight(1);
  return line;
}

}  // namespace

AISettingsPanel::AISettingsPanel(QWidget *parent) : QWidget(parent)
{
  setObjectName(QStringLiteral("aiSettingsPanel"));
  buildUi();
  applyChrome();
  loadSettings();
}

void AISettingsPanel::setAutoSave(bool enabled)
{
  autoSave = enabled;
  if (autoSave && !autoSaveTimer) {
    autoSaveTimer = new QTimer(this);
    autoSaveTimer->setSingleShot(true);
    autoSaveTimer->setInterval(450);
    connect(autoSaveTimer, &QTimer::timeout, this, [this]() {
      if (saveAll()) {
        emit settingsSaved();
      }
    });
    connectAutoSaveHooks();
  }
}

void AISettingsPanel::reloadFromDisk()
{
  loadSettings();
}

void AISettingsPanel::scheduleAutoSave()
{
  if (!autoSave || loading) return;
  if (autoSaveTimer) autoSaveTimer->start();
}

void AISettingsPanel::connectAutoSaveHooks()
{
  connect(endpointEdit, &QLineEdit::textChanged, this, &AISettingsPanel::scheduleAutoSave);
  connect(modelEdit, &QLineEdit::textChanged, this, &AISettingsPanel::scheduleAutoSave);
  connect(apiKeyEdit, &QLineEdit::textChanged, this, &AISettingsPanel::scheduleAutoSave);
  connect(systemPromptEdit, &QPlainTextEdit::textChanged, this, &AISettingsPanel::scheduleAutoSave);
  connect(defaultPromptEdit, &QPlainTextEdit::textChanged, this, &AISettingsPanel::scheduleAutoSave);
}

QString AISettingsPanel::defaultSystemPrompt() const
{
  const std::string profile =
    currentProfile.isEmpty() ? std::string() : currentProfile.toStdString();
  return QString::fromStdString(AIService::systemPromptForProfile(profile));
}

void AISettingsPanel::buildUi()
{
  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(22, 18, 22, 16);
  root->setSpacing(12);

  // Header
  auto *header = new QHBoxLayout();
  header->setContentsMargins(0, 0, 0, 10);
  header->setSpacing(12);
  iconLabel = new QLabel(this);
  iconLabel->setFixedSize(36, 36);
  iconLabel->setAlignment(Qt::AlignCenter);
  iconLabel->setPixmap(QIcon::fromTheme(QStringLiteral("chokusen-bot-ai")).pixmap(28, 28));

  auto *titleCol = new QVBoxLayout();
  titleCol->setSpacing(1);
  titleLabel = new QLabel(_("AI Settings"), this);
  QFont titleFont = titleLabel->font();
  titleFont.setPointSize(titleFont.pointSize() + 2);
  titleFont.setBold(true);
  titleLabel->setFont(titleFont);
  hintLabel = new QLabel(_("Connection, prompts, and model parameters."), this);
  titleCol->addWidget(titleLabel);
  titleCol->addWidget(hintLabel);
  header->addWidget(iconLabel, 0, Qt::AlignTop);
  header->addLayout(titleCol, 1);
  root->addLayout(header);
  root->addSpacing(6);

  tabs = new QTabWidget(this);
  tabs->setObjectName(QStringLiteral("aiSettingsTabs"));
  tabs->setDocumentMode(true);
  tabs->setUsesScrollButtons(false);
  tabs->setElideMode(Qt::ElideNone);
  tabs->tabBar()->setObjectName(QStringLiteral("aiSettingsTabBar"));
  tabs->tabBar()->setExpanding(false);
  tabs->tabBar()->setDrawBase(false);
  tabs->tabBar()->setElideMode(Qt::ElideNone);
  tabs->tabBar()->setUsesScrollButtons(false);
  tabs->tabBar()->setMovable(false);

  // ===== General =====
  auto *generalScroll = new QScrollArea(tabs);
  generalScroll->setWidgetResizable(true);
  generalScroll->setFrameShape(QFrame::NoFrame);
  generalScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  generalScroll->setObjectName(QStringLiteral("generalScroll"));

  auto *generalPage = new QWidget();
  generalPage->setObjectName(QStringLiteral("generalPage"));
  auto *general = new QVBoxLayout(generalPage);
  general->setContentsMargins(16, 14, 16, 14);
  general->setSpacing(10);

  general->addWidget(makeSectionTitle(_("Connection"), generalPage));

  profileCombo = new QComboBox(generalPage);
  profileCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  newProfileButton = new QPushButton(_("New"), generalPage);
  deleteProfileButton = new QPushButton(_("Delete"), generalPage);
  newProfileButton->setObjectName(QStringLiteral("secondaryBtn"));
  deleteProfileButton->setObjectName(QStringLiteral("secondaryBtn"));
  newProfileButton->setFixedWidth(64);
  deleteProfileButton->setFixedWidth(64);

  auto *profileField = new QWidget(generalPage);
  auto *profileFieldLayout = new QHBoxLayout(profileField);
  profileFieldLayout->setContentsMargins(0, 0, 0, 0);
  profileFieldLayout->setSpacing(8);
  profileFieldLayout->addWidget(profileCombo, 1);
  profileFieldLayout->addWidget(newProfileButton);
  profileFieldLayout->addWidget(deleteProfileButton);
  general->addWidget(makeFieldRow(_("Profile"), profileField, generalPage));

  endpointEdit = new QLineEdit(generalPage);
  endpointEdit->setPlaceholderText(
    QStringLiteral("https://generativelanguage.googleapis.com/v1beta/openai"));
  modelEdit = new QLineEdit(generalPage);
  modelEdit->setPlaceholderText(QStringLiteral("e.g. gemini-2.0-flash"));
  apiKeyEdit = new QLineEdit(generalPage);
  apiKeyEdit->setEchoMode(QLineEdit::Password);
  apiKeyEdit->setPlaceholderText(_("Paste your API key"));
  apiKeyEdit->setClearButtonEnabled(true);

  general->addWidget(makeFieldRow(_("Endpoint"), endpointEdit, generalPage));
  general->addWidget(makeFieldRow(_("Model"), modelEdit, generalPage));
  general->addWidget(makeFieldRow(_("API Key"), apiKeyEdit, generalPage));

  general->addSpacing(4);
  general->addWidget(makeDivider(generalPage));
  general->addSpacing(2);

  auto *paramsTitleRow = new QWidget(generalPage);
  auto *paramsTitleLayout = new QHBoxLayout(paramsTitleRow);
  paramsTitleLayout->setContentsMargins(0, 0, 0, 0);
  paramsTitleLayout->setSpacing(8);
  paramsTitleLayout->addWidget(makeSectionTitle(_("Parameters"), paramsTitleRow), 1);
  addParamButton = new QPushButton(_("+ Add"), paramsTitleRow);
  addParamButton->setObjectName(QStringLiteral("linkBtn"));
  addParamButton->setFlat(true);
  addParamButton->setCursor(Qt::PointingHandCursor);
  addParamButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
  addParamButton->setToolTip(_("Add an optional API parameter (e.g. max_tokens, temperature)"));
  paramsTitleLayout->addWidget(addParamButton, 0, Qt::AlignVCenter);
  general->addWidget(paramsTitleRow);

  paramHeader = new QWidget(generalPage);
  auto *paramHeaderLayout = new QHBoxLayout(paramHeader);
  paramHeaderLayout->setContentsMargins(0, 0, 28, 0);
  paramHeaderLayout->setSpacing(8);
  auto *keyHead = new QLabel(_("Key"), paramHeader);
  auto *valHead = new QLabel(_("Value"), paramHeader);
  keyHead->setObjectName(QStringLiteral("columnHead"));
  valHead->setObjectName(QStringLiteral("columnHead"));
  paramHeaderLayout->addWidget(keyHead, 1);
  paramHeaderLayout->addWidget(valHead, 1);
  general->addWidget(paramHeader);

  paramsList = new QWidget(generalPage);
  paramsList->setObjectName(QStringLiteral("paramsList"));
  paramsList->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
  paramsListLayout = new QVBoxLayout(paramsList);
  paramsListLayout->setContentsMargins(0, 0, 0, 0);
  paramsListLayout->setSpacing(6);
  general->addWidget(paramsList);

  general->addSpacing(4);
  general->addWidget(makeDivider(generalPage));
  general->addSpacing(2);
  general->addWidget(makeSectionTitle(_("Chat history"), generalPage));

  auto *chatHistoryHint = new QLabel(generalPage);
  chatHistoryHint->setObjectName(QStringLiteral("infoCard"));
  chatHistoryHint->setWordWrap(true);
  chatHistoryHint->setTextFormat(Qt::RichText);
  chatHistoryHint->setTextInteractionFlags(Qt::TextSelectableByMouse);
  chatHistoryHint->setText(QStringLiteral(
    "<ul style='margin:0 0 0 16px; padding:0;'>"
    "<li style='margin-bottom:6px;'>%1</li>"
    "<li style='margin-bottom:6px;'>%2</li>"
    "<li style='margin-bottom:0;'>%3</li>"
    "</ul>")
    .arg(_("Use <b>History</b> to reopen a saved chat (up to about 30 sessions)."),
         _("Use <b>Clear chat</b> to start a new session. Previous messages in that chat are no "
           "longer sent as context."),
         _("Saved sessions keep only user and assistant text messages. Tool calls are not restored "
           "after reopening the app.")));
  general->addWidget(chatHistoryHint);
  general->addStretch(1);

  generalScroll->setWidget(generalPage);

  // ===== System Prompt =====
  auto *systemPage = new QWidget(tabs);
  systemPage->setObjectName(QStringLiteral("tabPage"));
  auto *systemLayout = new QVBoxLayout(systemPage);
  systemLayout->setContentsMargins(16, 14, 16, 14);
  systemLayout->setSpacing(8);
  auto *systemHeader = new QWidget(systemPage);
  auto *systemHeaderLayout = new QHBoxLayout(systemHeader);
  systemHeaderLayout->setContentsMargins(0, 0, 0, 0);
  systemHeaderLayout->setSpacing(8);
  auto *systemHint = new QLabel(_("Sent with every chat request."), systemHeader);
  systemHint->setObjectName(QStringLiteral("mutedHint"));
  systemHint->setWordWrap(true);
  resetSystemPromptButton = new QPushButton(_("Reset to default"), systemHeader);
  resetSystemPromptButton->setObjectName(QStringLiteral("resetPromptBtn"));
  resetSystemPromptButton->setCursor(Qt::PointingHandCursor);
  resetSystemPromptButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
  resetSystemPromptButton->setAutoDefault(false);
  resetSystemPromptButton->setDefault(false);
  systemHeaderLayout->addWidget(systemHint, 1);
  systemHeaderLayout->addWidget(resetSystemPromptButton, 0, Qt::AlignTop);
  systemPromptEdit = new QPlainTextEdit(systemPage);
  systemPromptEdit->setPlaceholderText(_("Instructions for the assistant…"));
  systemPromptEdit->setMinimumHeight(420);
  systemLayout->addWidget(systemHeader);
  systemLayout->addWidget(systemPromptEdit, 1);

  // ===== Default User Prompt =====
  auto *defaultPage = new QWidget(tabs);
  defaultPage->setObjectName(QStringLiteral("tabPage"));
  auto *defaultLayout = new QVBoxLayout(defaultPage);
  defaultLayout->setContentsMargins(16, 14, 16, 14);
  defaultLayout->setSpacing(8);
  auto *defaultHint = new QLabel(_("Optional starter for new conversations. Leave empty for none."),
                                 defaultPage);
  defaultHint->setObjectName(QStringLiteral("mutedHint"));
  defaultHint->setWordWrap(true);
  defaultPromptEdit = new QPlainTextEdit(defaultPage);
  defaultPromptEdit->setPlaceholderText(_("Optional starter prompt…"));
  defaultPromptEdit->setMinimumHeight(420);
  defaultLayout->addWidget(defaultHint);
  defaultLayout->addWidget(defaultPromptEdit, 1);

  tabs->addTab(generalScroll, _("General"));
  tabs->addTab(systemPage, _("System Prompt"));
  tabs->addTab(defaultPage, _("Default User Prompt"));
  tabs->setMinimumHeight(520);
  tabs->setElideMode(Qt::ElideNone);
  root->addWidget(tabs, 1);

  connect(profileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &AISettingsPanel::onProfileChanged);
  connect(newProfileButton, &QPushButton::clicked, this, &AISettingsPanel::onNewProfile);
  connect(deleteProfileButton, &QPushButton::clicked, this, &AISettingsPanel::onDeleteProfile);
  connect(addParamButton, &QPushButton::clicked, this, &AISettingsPanel::onAddParam);
  connect(resetSystemPromptButton, &QPushButton::clicked, this, &AISettingsPanel::onResetSystemPrompt);
}

void AISettingsPanel::applyChrome()
{
  const bool dark = QApplication::palette().color(QPalette::Window).lightness() < 128;
  const QString bg = dark ? QStringLiteral("#1c1c1e") : QStringLiteral("#f2f2f4");
  const QString border = dark ? QStringLiteral("#3a3a3c") : QStringLiteral("#dcdce0");
  const QString text = dark ? QStringLiteral("#f5f5f7") : QStringLiteral("#1d1d1f");
  const QString muted = dark ? QStringLiteral("#98989d") : QStringLiteral("#6e6e73");
  const QString input = dark ? QStringLiteral("#2c2c2e") : QStringLiteral("#ffffff");
  const QString hover = dark ? QStringLiteral("#3a3a3c") : QStringLiteral("#e8e8ec");
  const QString btn = dark ? QStringLiteral("#3a3a3c") : QStringLiteral("#e8e8ec");
  const QString accent = dark ? QStringLiteral("#0a84ff") : QStringLiteral("#0071e3");
  const QString tabIdle = dark ? QStringLiteral("#2c2c2e") : QStringLiteral("#e5e5ea");
  const QString tabActive = dark ? QStringLiteral("#3a3a3c") : QStringLiteral("#ffffff");

  QString css = QStringLiteral(R"(
    QWidget#aiSettingsPanel {
      background: __BG__;
      color: __TEXT__;
    }
    QScrollArea#generalScroll, QWidget#generalPage, QWidget#tabPage {
      background: __BG__;
      border: none;
    }
    QLabel {
      color: __TEXT__;
      font-size: 12px;
    }
    QLabel#fieldLabel {
      color: __MUTED__;
      font-size: 12px;
    }
    QLabel#sectionTitle {
      color: __TEXT__;
      font-size: 11px;
      font-weight: 700;
      letter-spacing: 0.4px;
      padding: 2px 0 4px 0;
    }
    QLabel#columnHead {
      color: __MUTED__;
      font-size: 11px;
      font-weight: 600;
      padding: 0 2px 2px 2px;
    }
    QLabel#mutedHint {
      color: __MUTED__;
      font-size: 12px;
    }
    QLabel#infoCard {
      color: __TEXT__;
      font-size: 12px;
      line-height: 1.45;
      background: __INPUT__;
      border: 1px solid __BORDER__;
      border-radius: 10px;
      padding: 12px 14px;
    }
    QFrame#sectionDivider {
      background: __BORDER__;
      border: none;
      max-height: 1px;
    }
    QTabWidget#aiSettingsTabs {
      background: __BG__;
    }
    QTabWidget#aiSettingsTabs::pane {
      border: none;
      background: __BG__;
      top: 0px;
      padding-top: 8px;
    }
    QTabBar#aiSettingsTabBar {
      background: __BG__;
      alignment: left;
    }
    QTabBar#aiSettingsTabBar::tab {
      background: __TAB_IDLE__;
      color: __MUTED__;
      border: 1px solid transparent;
      border-radius: 9px;
      padding: 8px 16px;
      margin-right: 8px;
      margin-bottom: 8px;
      font-size: 12px;
      font-weight: 500;
      min-height: 20px;
    }
    QTabBar#aiSettingsTabBar::tab:selected {
      background: __TAB_ACTIVE__;
      color: __TEXT__;
      border: 1px solid __BORDER__;
      font-weight: 600;
    }
    QTabBar#aiSettingsTabBar::tab:hover:!selected {
      background: __HOVER__;
      color: __TEXT__;
    }
    QLineEdit, QComboBox, QPlainTextEdit {
      background: __INPUT__;
      color: __TEXT__;
      border: 1px solid __BORDER__;
      border-radius: 7px;
      padding: 6px 10px;
      min-height: 26px;
      selection-background-color: __HOVER__;
    }
    QPlainTextEdit {
      padding: 10px;
    }
    QComboBox::drop-down {
      border: none;
      width: 18px;
    }
    QPushButton {
      background: __BTN__;
      color: __TEXT__;
      border: 1px solid __BORDER__;
      border-radius: 7px;
      padding: 5px 12px;
      min-height: 26px;
      font-size: 12px;
    }
    QPushButton:hover {
      background: __HOVER__;
    }
    QPushButton:default {
      background: __ACCENT__;
      color: #ffffff;
      border: 1px solid __ACCENT__;
      font-weight: 600;
    }
    QPushButton#secondaryBtn {
      min-height: 26px;
      background: __BTN__;
      color: __TEXT__;
      border: 1px solid __BORDER__;
    }
    QPushButton#resetPromptBtn {
      background: __BTN__;
      color: __TEXT__;
      border: 1px solid __BORDER__;
      border-radius: 7px;
      padding: 5px 12px;
      min-height: 26px;
      font-size: 12px;
      font-weight: 500;
    }
    QPushButton#resetPromptBtn:hover {
      background: __HOVER__;
      color: __TEXT__;
    }
    QPushButton#linkBtn {
      background: transparent;
      border: none;
      color: __ACCENT__;
      padding: 4px 2px;
      min-height: 20px;
      font-weight: 600;
      text-align: left;
    }
    QPushButton#linkBtn:hover {
      background: transparent;
      color: __TEXT__;
    }
    QPushButton#rowRemoveBtn {
      background: transparent;
      border: none;
      color: __MUTED__;
      padding: 0px;
      min-width: 24px;
      max-width: 24px;
      min-height: 24px;
      font-size: 14px;
    }
    QPushButton#rowRemoveBtn:hover {
      color: __TEXT__;
      background: __HOVER__;
      border-radius: 6px;
    }
    QWidget#paramRow QLineEdit {
      background: __INPUT__;
    }
  )");
  css.replace(QStringLiteral("__BG__"), bg);
  css.replace(QStringLiteral("__BORDER__"), border);
  css.replace(QStringLiteral("__TEXT__"), text);
  css.replace(QStringLiteral("__MUTED__"), muted);
  css.replace(QStringLiteral("__INPUT__"), input);
  css.replace(QStringLiteral("__HOVER__"), hover);
  css.replace(QStringLiteral("__BTN__"), btn);
  css.replace(QStringLiteral("__ACCENT__"), accent);
  css.replace(QStringLiteral("__TAB_IDLE__"), tabIdle);
  css.replace(QStringLiteral("__TAB_ACTIVE__"), tabActive);
  setStyleSheet(css);

  if (hintLabel) {
    QPalette pal = hintLabel->palette();
    pal.setColor(QPalette::WindowText, QColor(muted));
    hintLabel->setPalette(pal);
  }
  if (iconLabel) {
    iconLabel->setPixmap(QIcon::fromTheme(QStringLiteral("chokusen-bot-ai")).pixmap(28, 28));
  }
  if (tabs) {
    tabs->setElideMode(Qt::ElideNone);
    if (tabs->tabBar()) {
      tabs->tabBar()->setElideMode(Qt::ElideNone);
      tabs->tabBar()->setExpanding(false);
      tabs->tabBar()->setDrawBase(false);
      tabs->tabBar()->setUsesScrollButtons(false);
    }
  }
}

void AISettingsPanel::clearParamRows()
{
  for (const auto& item : paramRows) {
    if (item.row) {
      paramsListLayout->removeWidget(item.row);
      item.row->deleteLater();
    }
  }
  paramRows.clear();
}

void AISettingsPanel::ensureDefaultParamRow()
{
  if (paramRows.isEmpty()) {
    addParamRow(QString(), QString());
  }
}

void AISettingsPanel::addParamRow(const QString& key, const QString& value)
{
  auto *row = new QWidget(paramsList);
  row->setObjectName(QStringLiteral("paramRow"));
  auto *layout = new QHBoxLayout(row);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto *keyEdit = new QLineEdit(row);
  keyEdit->setPlaceholderText(_("parameter_key"));
  keyEdit->setText(key);
  auto *valueEdit = new QLineEdit(row);
  valueEdit->setPlaceholderText(_("value"));
  valueEdit->setText(value);

  auto *removeBtn = new QPushButton(QStringLiteral("×"), row);
  removeBtn->setObjectName(QStringLiteral("rowRemoveBtn"));
  removeBtn->setToolTip(_("Remove"));
  removeBtn->setCursor(Qt::PointingHandCursor);
  removeBtn->setFocusPolicy(Qt::NoFocus);

  layout->addWidget(keyEdit, 1);
  layout->addWidget(valueEdit, 1);
  layout->addWidget(removeBtn, 0, Qt::AlignVCenter);

  ParamRow entry;
  entry.row = row;
  entry.keyEdit = keyEdit;
  entry.valueEdit = valueEdit;
  paramRows.push_back(entry);
  paramsListLayout->addWidget(row);

  connect(removeBtn, &QPushButton::clicked, this, [this, row]() { removeParamRow(row); });
  connect(keyEdit, &QLineEdit::textChanged, this, &AISettingsPanel::scheduleAutoSave);
  connect(valueEdit, &QLineEdit::textChanged, this, &AISettingsPanel::scheduleAutoSave);
}

void AISettingsPanel::removeParamRow(QWidget *row)
{
  for (int i = 0; i < paramRows.size(); ++i) {
    if (paramRows[i].row == row) {
      paramsListLayout->removeWidget(row);
      row->deleteLater();
      paramRows.removeAt(i);
      break;
    }
  }
  ensureDefaultParamRow();
  scheduleAutoSave();
}

void AISettingsPanel::loadSettings()
{
  loading = true;
  settings = readSettingsFile();
  if (!settings.contains("profiles") || !settings["profiles"].is_object()) {
    settings["profiles"] = nlohmann::json::object();
  }
  if (migrateLegacyDefaultLimits(settings)) {
    writeSettingsFile(settings);
  }
  if (AIFreeAgents::ensurePresets(settings)) {
    writeSettingsFile(settings);
  }

  profileCombo->clear();
  // Fixed order matching built-in presets (not alphabetical legacy clutter).
  static const char *kOrdered[] = {"Gemini", "OpenAI", "Claude", "Ollama"};
  QStringList names;
  for (const char *n : kOrdered) {
    if (settings["profiles"].contains(n)) {
      names.append(QString::fromUtf8(n));
    }
  }
  // Any unexpected extras (user-created) appear after, sorted.
  QStringList extras;
  for (auto it = settings["profiles"].begin(); it != settings["profiles"].end(); ++it) {
    const QString name = QString::fromStdString(it.key());
    if (!names.contains(name)) extras.append(name);
  }
  extras.sort(Qt::CaseInsensitive);
  names.append(extras);

  if (names.isEmpty()) {
    // ensurePresets should have seeded; keep a hard fallback
    AIFreeAgents::ensurePresets(settings);
    writeSettingsFile(settings);
    for (const char *n : kOrdered) {
      if (settings["profiles"].contains(n)) {
        names.append(QString::fromUtf8(n));
      }
    }
  }

  for (const auto& name : names) {
    profileCombo->addItem(name);
  }

  QString active = QString::fromStdString(settings.value("activeProfile", ""));
  if (active.isEmpty() || profileCombo->findText(active) < 0) {
    active = profileCombo->itemText(0);
  }
  const int idx = profileCombo->findText(active);
  profileCombo->setCurrentIndex(idx >= 0 ? idx : 0);
  loading = false;
  loadProfile(profileCombo->currentText());
}

void AISettingsPanel::loadProfile(const QString& profileName)
{
  loading = true;
  currentProfile = profileName;
  settings["activeProfile"] = profileName.toStdString();

  const auto profiles = settings.value("profiles", nlohmann::json::object());
  const auto profile = profiles.value(profileName.toStdString(), nlohmann::json::object());
  auto params = profile.value("params", nlohmann::json::object());

  QString endpoint = QString::fromStdString(profile.value("endpoint", ""));
  if (endpoint.isEmpty()) {
    if (profileName.contains(QStringLiteral("Gemini"), Qt::CaseInsensitive)) {
      endpoint = QStringLiteral("https://generativelanguage.googleapis.com/v1beta/openai");
    } else if (profileName.contains(QStringLiteral("Ollama"), Qt::CaseInsensitive)) {
      endpoint = QStringLiteral("http://localhost:11434/v1");
    } else if (profileName.contains(QStringLiteral("OpenAI"), Qt::CaseInsensitive)) {
      endpoint = QStringLiteral("https://api.openai.com/v1");
    } else if (profileName.contains(QStringLiteral("Claude"), Qt::CaseInsensitive) ||
               profileName.contains(QStringLiteral("Anthropic"), Qt::CaseInsensitive)) {
      endpoint = QStringLiteral("https://api.anthropic.com/v1");
    } else if (profileName.contains(QStringLiteral("Cursor"), Qt::CaseInsensitive)) {
      endpoint = QStringLiteral("https://api.cursor.com/v1");
    } else {
      endpoint = QStringLiteral("http://localhost:8080/v1");
    }
  }

  QString model = QString::fromStdString(params.value("model", ""));
  if (model.startsWith(QStringLiteral("models/"))) {
    model = model.mid(7);
  }
  if (model.isEmpty()) {
    if (endpoint.contains(QStringLiteral("generativelanguage.googleapis.com"))) {
      model = QStringLiteral("gemini-2.0-flash");
    } else if (profileName.contains(QStringLiteral("Ollama"), Qt::CaseInsensitive)) {
      model = QStringLiteral("qwen2.5-coder:14b");
    } else if (profileName.contains(QStringLiteral("OpenAI"), Qt::CaseInsensitive)) {
      model = QStringLiteral("gpt-4o");
    } else if (profileName.contains(QStringLiteral("Claude"), Qt::CaseInsensitive)) {
      model = QStringLiteral("claude-sonnet-4-5");
    } else if (profileName.contains(QStringLiteral("Cursor"), Qt::CaseInsensitive)) {
      model = QStringLiteral("auto");
    } else {
      model = QStringLiteral("custom");
    }
  }

  QString systemPrompt = QString::fromStdString(params.value("system_prompt", ""));
  if (systemPrompt.isEmpty()) systemPrompt = defaultSystemPrompt();
  const QString defaultPrompt = QString::fromStdString(params.value("default_prompt", ""));

  endpointEdit->setText(endpoint);
  modelEdit->setText(model);
  apiKeyEdit->setText(QString::fromStdString(profile.value("apiKey", "")));
  systemPromptEdit->setPlainText(systemPrompt);
  defaultPromptEdit->setPlainText(defaultPrompt);

  const QString tierNote = QString::fromStdString(params.value("tier_note", ""));
  const bool isOllama = profileName.contains(QStringLiteral("Ollama"), Qt::CaseInsensitive);
  if (hintLabel) {
    if (!tierNote.isEmpty()) {
      hintLabel->setText(tierNote);
    } else {
      hintLabel->setText(_("Connection, prompts, and model parameters."));
    }
  }
  if (isOllama) {
    apiKeyEdit->setPlaceholderText(_("No API key needed for Ollama"));
  } else {
    apiKeyEdit->setPlaceholderText(_("Paste your API key"));
  }

  clearParamRows();
  QStringList keys;
  for (auto it = params.begin(); it != params.end(); ++it) {
    const QString key = QString::fromStdString(it.key());
    if (key == QStringLiteral("system_prompt") || key == QStringLiteral("default_prompt") ||
        key == QStringLiteral("model") || key == QStringLiteral("tier") ||
        key == QStringLiteral("tier_note")) {
      continue;
    }
    keys.append(key);
  }
  keys.sort(Qt::CaseInsensitive);
  for (const auto& key : keys) {
    const auto val = params.value(key.toStdString(), nlohmann::json(""));
    QString text;
    if (val.is_number_integer()) text = QString::number(val.get<int>());
    else if (val.is_number_float()) text = QString::number(val.get<double>());
    else if (val.is_boolean()) text = val.get<bool>() ? QStringLiteral("true") : QStringLiteral("false");
    else if (val.is_string()) text = QString::fromStdString(val.get<std::string>());
    addParamRow(key, text);
  }
  ensureDefaultParamRow();

  loading = false;
}

bool AISettingsPanel::collectProfileIntoJson(nlohmann::json& profileObj) const
{
  profileObj = nlohmann::json::object();
  profileObj["endpoint"] = endpointEdit->text().trimmed().toStdString();
  profileObj["apiKey"] = apiKeyEdit->text().trimmed().toStdString();

  nlohmann::json params = nlohmann::json::object();
  // Preserve free-tier metadata when switching/saving profiles
  if (!currentProfile.isEmpty() && settings.contains("profiles") &&
      settings["profiles"].contains(currentProfile.toStdString())) {
    const auto& prev = settings["profiles"][currentProfile.toStdString()];
    if (prev.contains("params") && prev["params"].is_object()) {
      const auto& prevParams = prev["params"];
      if (prevParams.contains("tier")) params["tier"] = prevParams["tier"];
      if (prevParams.contains("tier_note")) params["tier_note"] = prevParams["tier_note"];
    }
  }

  params["system_prompt"] = systemPromptEdit->toPlainText().toStdString();
  params["default_prompt"] = defaultPromptEdit->toPlainText().toStdString();

  QString model = modelEdit->text().trimmed();
  if (model.startsWith(QStringLiteral("models/"))) {
    model = model.mid(7);
  }
  if (!model.isEmpty()) {
    params["model"] = model.toStdString();
  }

  for (const auto& row : paramRows) {
    if (!row.keyEdit || !row.valueEdit) continue;
    const QString key = row.keyEdit->text().trimmed();
    if (key.isEmpty() || key == QStringLiteral("system_prompt") ||
        key == QStringLiteral("default_prompt") || key == QStringLiteral("model") ||
        key == QStringLiteral("tier") || key == QStringLiteral("tier_note")) {
      continue;
    }
    const QString valText = row.valueEdit->text().trimmed();
    bool okInt = false;
    const int valInt = valText.toInt(&okInt);
    bool okDouble = false;
    const double valDouble = valText.toDouble(&okDouble);
    if (okInt) {
      params[key.toStdString()] = valInt;
    } else if (okDouble && valText.contains(QLatin1Char('.'))) {
      params[key.toStdString()] = valDouble;
    } else if (valText.toLower() == QStringLiteral("true")) {
      params[key.toStdString()] = true;
    } else if (valText.toLower() == QStringLiteral("false")) {
      params[key.toStdString()] = false;
    } else {
      params[key.toStdString()] = valText.toStdString();
    }
  }

  profileObj["params"] = params;
  return true;
}

void AISettingsPanel::onProfileChanged(int index)
{
  if (loading || index < 0) return;

  if (!currentProfile.isEmpty()) {
    nlohmann::json profileObj;
    collectProfileIntoJson(profileObj);
    settings["profiles"][currentProfile.toStdString()] = profileObj;
  }
  loadProfile(profileCombo->itemText(index));
  scheduleAutoSave();
}

void AISettingsPanel::onNewProfile()
{
  bool ok = false;
  const QString name =
    QInputDialog::getText(this, _("New AI Profile"), _("Profile name:"), QLineEdit::Normal, "", &ok)
      .trimmed();
  if (!ok || name.isEmpty()) return;
  if (profileCombo->findText(name) >= 0) {
    QMessageBox::warning(this, _("Duplicate Profile"), _("A profile with that name already exists."));
    return;
  }

  if (!currentProfile.isEmpty()) {
    nlohmann::json profileObj;
    collectProfileIntoJson(profileObj);
    settings["profiles"][currentProfile.toStdString()] = profileObj;
  }

  nlohmann::json profile = nlohmann::json::object();
  profile["endpoint"] = "http://localhost:8080/v1";
  profile["apiKey"] = "";
  nlohmann::json params = nlohmann::json::object();
  params["model"] = "custom";
  params["system_prompt"] = defaultSystemPrompt().toStdString();
  params["default_prompt"] = "";
  profile["params"] = params;
  settings["profiles"][name.toStdString()] = profile;

  loading = true;
  profileCombo->addItem(name);
  profileCombo->setCurrentIndex(profileCombo->count() - 1);
  loading = false;
  loadProfile(name);
  scheduleAutoSave();
}

void AISettingsPanel::onDeleteProfile()
{
  if (profileCombo->count() <= 1) {
    QMessageBox::warning(this, _("Delete AI Profile"), _("At least one profile is required."));
    return;
  }
  const QString name = profileCombo->currentText();
  const auto result = QMessageBox::question(this, _("Delete AI Profile"),
                                            QString(_("Delete profile \"%1\"?")).arg(name),
                                            QMessageBox::Yes | QMessageBox::No);
  if (result != QMessageBox::Yes) return;

  settings["profiles"].erase(name.toStdString());
  const int idx = profileCombo->currentIndex();
  loading = true;
  profileCombo->removeItem(idx);
  loading = false;
  loadProfile(profileCombo->currentText());
  scheduleAutoSave();
}

void AISettingsPanel::onAddParam()
{
  addParamRow(QString(), QString());
  if (!paramRows.isEmpty() && paramRows.last().keyEdit) {
    paramRows.last().keyEdit->setFocus();
  }
  scheduleAutoSave();
}

void AISettingsPanel::onResetSystemPrompt()
{
  systemPromptEdit->setPlainText(defaultSystemPrompt());
  systemPromptEdit->setFocus();
  scheduleAutoSave();
}

bool AISettingsPanel::saveAll()
{
  if (currentProfile.isEmpty() && profileCombo->count() > 0) {
    currentProfile = profileCombo->currentText();
  }
  if (currentProfile.isEmpty()) {
    return false;
  }

  nlohmann::json profileObj;
  collectProfileIntoJson(profileObj);
  settings["profiles"][currentProfile.toStdString()] = profileObj;
  settings["activeProfile"] = currentProfile.toStdString();

  if (!writeSettingsFile(settings)) {
    return false;
  }
  // Avoid recursive reload when Preferences embeds this panel with auto-save.
  if (!autoSave) {
    if (auto *prefs = GlobalPreferences::inst()) {
      prefs->reloadAISettingsFromDisk();
    }
  }
  return true;
}
