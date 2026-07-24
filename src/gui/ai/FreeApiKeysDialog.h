#pragma once

#include <QDialog>
#include <QHash>
#include <QString>
#include <QStringList>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;

/*! Free-agent connection settings: sidebar of providers + Endpoint / Model / API Key. */
class FreeApiKeysDialog : public QDialog
{
  Q_OBJECT

public:
  explicit FreeApiKeysDialog(QWidget *parent = nullptr);

  static int prompt(QWidget *parent = nullptr);

private slots:
  void onSave();
  void onProviderChanged(int row);
  void onSetDefault();

private:
  struct TabFields {
    QLineEdit *endpointEdit = nullptr;
    QLineEdit *modelEdit = nullptr;
    QLineEdit *apiKeyEdit = nullptr;
    QLabel *defaultBadge = nullptr;
    QPushButton *defaultButton = nullptr;
  };

  void refreshDefaultUi();

  QListWidget *providerList = nullptr;
  QStackedWidget *pages = nullptr;
  QHash<QString, TabFields> fieldsByName;
  QStringList providerOrder;
};
