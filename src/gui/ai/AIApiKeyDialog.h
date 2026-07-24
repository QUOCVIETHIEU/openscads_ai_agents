#pragma once

#include <QDialog>

class AISettingsPanel;

class AIApiKeyDialog : public QDialog
{
  Q_OBJECT

public:
  explicit AIApiKeyDialog(QWidget *parent = nullptr);

  static int prompt(QWidget *parent = nullptr);

private slots:
  void onSave();

private:
  AISettingsPanel *panel = nullptr;
};
