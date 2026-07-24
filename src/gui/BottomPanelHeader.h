#pragma once

#include <QWidget>

class QLabel;
class QToolButton;
class QHBoxLayout;

/*! VS Code-style bottom panel chrome: left tabs + right action icons. */
class BottomPanelHeader : public QWidget
{
  Q_OBJECT

public:
  enum Tab { ConsoleTab = 0, ErrorLogTab = 1 };

  explicit BottomPanelHeader(QWidget *parent = nullptr);

  void setActiveTab(int tab);
  [[nodiscard]] int activeTab() const { return activeTabIndex; }
  void setErrorCount(int count);
  void applyTheme();
  void setMaximized(bool maximized);

signals:
  void tabChanged(int tab);
  void clearClicked();
  void maximizeClicked();
  void closeClicked();
  void moreSaveConsole();
  void moreClearConsole();

private:
  void rebuildIcons();
  QToolButton *makeTabButton(const QString& text);
  QToolButton *makeIconButton(const QString& tooltip);

  QHBoxLayout *layout{nullptr};
  QToolButton *consoleTab{nullptr};
  QToolButton *errorLogTab{nullptr};
  QLabel *errorBadge{nullptr};
  QWidget *errorTabWrap{nullptr};
  QToolButton *clearBtn{nullptr};
  QToolButton *maximizeBtn{nullptr};
  QToolButton *closeBtn{nullptr};
  QToolButton *moreBtn{nullptr};
  int activeTabIndex{ConsoleTab};
  bool isMaximized{false};
  bool dark{false};
};
