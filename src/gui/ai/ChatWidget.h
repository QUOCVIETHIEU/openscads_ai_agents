#pragma once

#include <QString>
#include <QWidget>
#include <memory>
#include <vector>
#include "core/AIService.h"
#include "gui/qtgettext.h"  // IWYU pragma: keep
#include "ui_ChatWidget.h"

class QLabel;
class QTimer;
class QPushButton;
class QDockWidget;

class MessageBubble : public QWidget
{
  Q_OBJECT
public:
  MessageBubble(const QString& text, bool isUser, QWidget *parent = nullptr);
  void updateText(const QString& text);

private:
  bool isDarkTheme() const;
  QLabel *label;
  QTimer *thinkingTimer = nullptr;
  int thinkingStep = 0;
};

class CollapsibleBubble;

class ChatWidget : public QWidget, public Ui::ChatWidget
{
  Q_OBJECT

public:
  ChatWidget(QWidget *parent = nullptr);
  virtual ~ChatWidget();

  void applyCodeChange(const std::string& code);
  void logToolExecution(const std::string& name, const std::string& result);
  void startNewResponseTurn();
  void setCollapsed(bool collapsed);
  bool isCollapsed() const { return panelCollapsed; }

signals:
  void collapsedChanged(bool collapsed);

private slots:
  void onSendPressed();
  void onClearPressed();
  void onHistoryPressed();
  void onMorePressed();
  void onTogglePanelPressed();

private:
  MessageBubble *addMessage(const QString& text, bool isUser);
  bool isDarkTheme() const;
  void applyVSCodeChrome();
  void setupCursorComposer();
  void setupCursorHeader();
  void updateComposerActionButton();
  void enableInput(bool enabled);
  std::string executeTool(const std::string& name, const std::string& arguments_json);

  void clearMessageWidgets();
  void rebuildMessageWidgets();
  void saveCurrentSession();
  void loadSession(const QString& sessionId);
  QString sessionTitleFromHistory() const;
  QDockWidget *parentDock() const;
  void restoreExpandedChrome();

  std::shared_ptr<AIService> aiService;
  std::vector<ChatMessage> history;
  std::shared_ptr<bool> aliveState;
  QString currentSessionId;

  MessageBubble *activeAIBubble = nullptr;
  std::shared_ptr<std::string> activeResponseText;
  bool isRequestRunning = false;
  bool panelCollapsed = false;
  int expandedDockWidth = 320;

  CollapsibleBubble *activeToolBubble = nullptr;
  QPushButton *attachButton = nullptr;
  QPushButton *historyButton = nullptr;
  QPushButton *moreButton = nullptr;
  QPushButton *layoutButton = nullptr;
};
