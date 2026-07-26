#pragma once

#include <QDialog>

class AISettingsPanel;

class AIApiKeyDialog : public QDialog
{
  Q_OBJECT

public:
  explicit AIApiKeyDialog(QWidget *parent = nullptr);

  static int prompt(QWidget *parent = nullptr);
  /*! Open settings focused on a tab (0=General … 3=MCP Server). */
  static int prompt(QWidget *parent, int tabIndex);

private slots:
  void onSave();

private:
  AISettingsPanel *panel = nullptr;
};
