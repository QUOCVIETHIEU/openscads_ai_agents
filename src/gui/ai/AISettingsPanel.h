#pragma once

#include <QString>
#include <QVector>
#include <QWidget>
#include "json/json.hpp"

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QTimer;
class QVBoxLayout;

// Shared AI settings UI used by Preferences → AI and the chat AI Settings dialog.
class AISettingsPanel : public QWidget
{
  Q_OBJECT

public:
  explicit AISettingsPanel(QWidget *parent = nullptr);

  void setAutoSave(bool enabled);
  void reloadFromDisk();
  bool saveAll();
  QString defaultSystemPrompt() const;

signals:
  void settingsSaved();

private slots:
  void onProfileChanged(int index);
  void onNewProfile();
  void onDeleteProfile();
  void onAddParam();
  void onResetSystemPrompt();
  void scheduleAutoSave();

private:
  struct ParamRow {
    QWidget *row = nullptr;
    QLineEdit *keyEdit = nullptr;
    QLineEdit *valueEdit = nullptr;
  };

  void buildUi();
  void applyChrome();
  void loadSettings();
  void loadProfile(const QString& profileName);
  bool collectProfileIntoJson(nlohmann::json& profileObj) const;
  void clearParamRows();
  void addParamRow(const QString& key, const QString& value);
  void removeParamRow(QWidget *row);
  void ensureDefaultParamRow();
  void connectAutoSaveHooks();

  nlohmann::json settings;
  QString currentProfile;
  bool loading = false;
  bool autoSave = false;
  QTimer *autoSaveTimer = nullptr;

  QLabel *iconLabel = nullptr;
  QLabel *titleLabel = nullptr;
  QLabel *hintLabel = nullptr;
  QTabWidget *tabs = nullptr;

  QComboBox *profileCombo = nullptr;
  QPushButton *newProfileButton = nullptr;
  QPushButton *deleteProfileButton = nullptr;
  QLineEdit *endpointEdit = nullptr;
  QLineEdit *modelEdit = nullptr;
  QLineEdit *apiKeyEdit = nullptr;
  QPlainTextEdit *systemPromptEdit = nullptr;
  QPlainTextEdit *defaultPromptEdit = nullptr;
  QPushButton *resetSystemPromptButton = nullptr;

  QWidget *paramsList = nullptr;
  QVBoxLayout *paramsListLayout = nullptr;
  QWidget *paramHeader = nullptr;
  QPushButton *addParamButton = nullptr;
  QVector<ParamRow> paramRows;
};
