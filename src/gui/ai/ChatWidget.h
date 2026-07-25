#pragma once

#include <QString>
#include <QWidget>
#include <QImage>
#include <QList>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <memory>
#include <vector>
#include "core/AIService.h"
#include "gui/qtgettext.h"  // IWYU pragma: keep
#include "ui_ChatWidget.h"

class QLabel;
class QTimer;
class QPushButton;
class QDockWidget;
class QFrame;

class MessageBubble : public QWidget
{
  Q_OBJECT
public:
  MessageBubble(const QString& text, bool isUser, QWidget *parent = nullptr);
  void updateText(const QString& text);
  void addToolCall(const QString& summary, const QString& detail);
  void setImages(const QList<QImage>& images);
  void setHistoryIndex(int index) { historyIndex = index; }
  int getHistoryIndex() const { return historyIndex; }
  void setEditEnabled(bool enabled);

signals:
  void editRequested(int historyIndex);

private:
  bool isDarkTheme() const;
  void updateToolsPanel();
  void setBodyText(const QString& text);

  bool userMessage = false;
  int historyIndex = -1;
  QLabel *label = nullptr;
  QVBoxLayout *contentLayout = nullptr;
  QWidget *imageStrip = nullptr;
  QHBoxLayout *imageStripLayout = nullptr;
  QTimer *thinkingTimer = nullptr;
  int thinkingStep = 0;

  QPushButton *moreButton = nullptr;
  QPushButton *copyButton = nullptr;
  QPushButton *editButton = nullptr;
  QFrame *toolsPanel = nullptr;
  QLabel *toolsDetailLabel = nullptr;
  struct ToolCallLog {
    QString summary;
    QString detail;
  };
  std::vector<ToolCallLog> toolCalls;
};

class ChatWidget : public QWidget, public Ui::ChatWidget
{
  Q_OBJECT

public:
  ChatWidget(QWidget *parent = nullptr);
  virtual ~ChatWidget();

  void applyCodeChange(const std::string& code);
  void flushPendingPreview();
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
  void onSettingsPressed();
  void onTogglePanelPressed();
  void onAttachPressed();
  void onImagePasted(const QImage& image);
  void onEditMessage(int historyIndex);

private:
  MessageBubble *addMessage(const QString& text, bool isUser, const QList<QImage>& images = {},
                            int historyIndex = -1);
  bool isDarkTheme() const;
  void applyVSCodeChrome();
  void setupCursorComposer();
  void setupCursorHeader();
  void updateComposerActionButton();
  void updateAgentButton();
  bool ensureActiveProfileApiKey();
  void enableInput(bool enabled);
  void setUserEditButtonsEnabled(bool enabled);
  void stopActiveRequest(bool keepPartialAssistant);
  std::string executeTool(const std::string& name, const std::string& arguments_json);

  void clearMessageWidgets();
  void rebuildMessageWidgets();
  void saveCurrentSession();
  void loadSession(const QString& sessionId);
  QString sessionTitleFromHistory() const;
  QDockWidget *parentDock() const;
  void restoreExpandedChrome();

  bool addPendingImage(const QImage& image);
  void refreshAttachmentStrip();
  void clearPendingImages();
  static QString imageToDataUrl(const QImage& image);
  static QImage loadImageFromDataUrl(const QString& dataUrl);

  std::shared_ptr<AIService> aiService;
  std::vector<ChatMessage> history;
  std::shared_ptr<bool> aliveState;
  QString currentSessionId;

  MessageBubble *activeAIBubble = nullptr;
  std::shared_ptr<std::string> activeResponseText;
  bool isRequestRunning = false;
  bool pendingPreviewRender = false;
  bool appliedCodeThisTurn = false;
  bool panelCollapsed = false;
  int expandedDockWidth = 320;

  QPushButton *attachButton = nullptr;
  QPushButton *agentButton = nullptr;
  QPushButton *historyButton = nullptr;
  QPushButton *clearChatButton = nullptr;
  QPushButton *layoutButton = nullptr;

  QWidget *attachmentStrip = nullptr;
  QHBoxLayout *attachmentStripLayout = nullptr;
  QList<QImage> pendingImages;
  static constexpr int kMaxAttachments = 4;
};
