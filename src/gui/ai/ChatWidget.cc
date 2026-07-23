#include "gui/ai/ChatWidget.h"
#include "gui/qtgettext.h"
#include "json/json.hpp"
#include <future>
#include <QScrollBar>
#include <QFrame>
#include <QLabel>
#include <QTimer>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSizePolicy>
#include <QPushButton>
#include <QPlainTextEdit>
#include <cmath>
#include <functional>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QIcon>
#include <QSize>
#include <QPolygonF>
#include <QKeyEvent>
#include <QApplication>
#include <QPalette>
#include <QMenu>
#include <QAction>
#include <QSettings>
#include <QUuid>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDockWidget>
#include <QMainWindow>
#include "gui/MainWindow.h"
#include "gui/OpenSCADApp.h"
#include "gui/Preferences.h"
#include "gui/ai/CollapsibleBubble.h"

namespace {

QColor iconFg(bool dark) { return dark ? QColor("#c5c5c5") : QColor("#424242"); }

QIcon makeHiDpiIcon(int logicalSize, const std::function<void(QPainter&, int)>& paint)
{
  QIcon icon;
  for (qreal dpr : {1.0, 2.0, 3.0}) {
    const int px = qMax(1, qRound(logicalSize * dpr));
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    pm.setDevicePixelRatio(dpr);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    // Paint in logical coordinates 0..logicalSize
    paint(p, logicalSize);
    p.end();
    icon.addPixmap(pm);
  }
  return icon;
}

QIcon makeCircularSendIcon(bool stopMode, bool dark)
{
  // Compact black send disc with a thin arrow
  return makeHiDpiIcon(18, [stopMode, dark](QPainter& p, int s) {
    const QRectF circle(0.8, 0.8, s - 1.6, s - 1.6);
    p.setPen(Qt::NoPen);
    const QColor fill = dark ? QColor("#e6e6e6") : QColor("#1a1a1a");
    const QColor glyph = dark ? QColor("#1a1a1a") : QColor("#ffffff");
    if (stopMode) {
      p.setBrush(fill);
      p.drawEllipse(circle);
      p.setBrush(glyph);
      p.drawRoundedRect(QRectF(s * 0.34, s * 0.34, s * 0.32, s * 0.32), 1.2, 1.2);
    } else {
      p.setBrush(fill);
      p.drawEllipse(circle);
      // Thin upward arrow (stroke), not a chunky filled chevron
      QPen pen(glyph, 1.35, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
      p.setPen(pen);
      p.setBrush(Qt::NoBrush);
      const qreal cx = s * 0.5;
      p.drawLine(QPointF(cx, s * 0.30), QPointF(cx, s * 0.70));
      p.drawLine(QPointF(cx, s * 0.30), QPointF(s * 0.34, s * 0.48));
      p.drawLine(QPointF(cx, s * 0.30), QPointF(s * 0.66, s * 0.48));
    }
  });
}

QIcon makeHistoryIcon(bool dark)
{
  return makeHiDpiIcon(16, [dark](QPainter& p, int s) {
    QPen pen(iconFg(dark), 1.45, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(2.2, 2.2, s - 4.4, s - 4.4));
    p.drawLine(QPointF(s * 0.5, 4.6), QPointF(s * 0.5, s * 0.52));
    p.drawLine(QPointF(s * 0.5, s * 0.52), QPointF(s * 0.70, s * 0.66));
  });
}

QIcon makeEllipsisIcon(bool dark)
{
  return makeHiDpiIcon(16, [dark](QPainter& p, int s) {
    p.setPen(Qt::NoPen);
    p.setBrush(iconFg(dark));
    const qreal y = s * 0.5 - 1.35;
    const qreal d = 2.7;
    p.drawEllipse(QRectF(3.0, y, d, d));
    p.drawEllipse(QRectF(s * 0.5 - d * 0.5, y, d, d));
    p.drawEllipse(QRectF(s - 3.0 - d, y, d, d));
  });
}

QIcon makeSidebarIcon(bool dark)
{
  return makeHiDpiIcon(16, [dark](QPainter& p, int s) {
    QPen pen(iconFg(dark), 1.35, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(2.4, 2.8, s - 4.8, s - 5.6), 1.6, 1.6);
    p.drawLine(QPointF(s * 0.66, 2.8), QPointF(s * 0.66, s - 2.8));
  });
}

constexpr const char *kSavedChatsSettingsKey = "ai/savedChats";

QJsonArray loadSavedChatsArray()
{
  QSettings settings;
  const QByteArray raw = settings.value(kSavedChatsSettingsKey).toByteArray();
  if (raw.isEmpty()) return {};
  const QJsonDocument doc = QJsonDocument::fromJson(raw);
  return doc.isArray() ? doc.array() : QJsonArray{};
}

void storeSavedChatsArray(const QJsonArray& chats)
{
  QSettings settings;
  settings.setValue(kSavedChatsSettingsKey, QJsonDocument(chats).toJson(QJsonDocument::Compact));
}

QPushButton *makeHeaderIconButton(const QString& objectName, const QIcon& icon, const QString& tip,
                                  QWidget *parent)
{
  auto *btn = new QPushButton(parent);
  btn->setObjectName(objectName);
  btn->setIcon(icon);
  btn->setIconSize(QSize(16, 16));
  btn->setFixedSize(28, 28);
  btn->setFlat(true);
  btn->setCursor(Qt::PointingHandCursor);
  btn->setToolTip(tip);
  btn->setFocusPolicy(Qt::NoFocus);
  return btn;
}

}  // namespace

// MessageBubble implementation — VS Code Copilot-style message blocks
MessageBubble::MessageBubble(const QString& text, bool isUser, QWidget *parent) : QWidget(parent)
{
  QVBoxLayout *outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 6, 0, 6);
  outer->setSpacing(4);

  bool dark = isDarkTheme();

  QLabel *roleLabel = new QLabel(isUser ? _("You") : _("Copilot"), this);
  roleLabel->setStyleSheet(dark ? "QLabel { color: #cccccc; font-size: 12px; font-weight: 600; }"
                                : "QLabel { color: #616161; font-size: 12px; font-weight: 600; }");
  outer->addWidget(roleLabel);

  QFrame *bubbleFrame = new QFrame(this);
  bubbleFrame->setFrameShape(QFrame::NoFrame);
  bubbleFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

  QString frameStyle;
  QString labelStyle;
  if (isUser) {
    // User prompt card — subtle filled block like VS Code chat
    if (dark) {
      frameStyle =
        "QFrame { background-color: #2a2d2e; border: 1px solid #3c3c3c; border-radius: 2px; }";
      labelStyle = "QLabel { color: #cccccc; font-size: 13px; background: transparent; }";
    } else {
      frameStyle =
        "QFrame { background-color: #f3f3f3; border: 1px solid #e5e5e5; border-radius: 2px; }";
      labelStyle = "QLabel { color: #1e1e1e; font-size: 13px; background: transparent; }";
    }
  } else {
    // Assistant — flat, full-width, no heavy bubble
    if (dark) {
      frameStyle = "QFrame { background-color: transparent; border: none; border-radius: 0px; }";
      labelStyle = "QLabel { color: #cccccc; font-size: 13px; background: transparent; }";
    } else {
      frameStyle = "QFrame { background-color: transparent; border: none; border-radius: 0px; }";
      labelStyle = "QLabel { color: #1e1e1e; font-size: 13px; background: transparent; }";
    }
  }

  bubbleFrame->setStyleSheet(frameStyle);

  QVBoxLayout *frameLayout = new QVBoxLayout(bubbleFrame);
  frameLayout->setContentsMargins(isUser ? 10 : 0, isUser ? 8 : 0, isUser ? 10 : 0, isUser ? 8 : 0);

  this->label = new QLabel(text, bubbleFrame);
  this->label->setWordWrap(true);
  this->label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  this->label->setStyleSheet(labelStyle);
  this->label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

  frameLayout->addWidget(this->label);
  outer->addWidget(bubbleFrame);

  if (!isUser && text == _("Thinking...")) {
    thinkingStep = 0;
    thinkingTimer = new QTimer(this);
    connect(thinkingTimer, &QTimer::timeout, this, [this]() {
      thinkingStep = (thinkingStep + 1) % 4;
      QString dots;
      for (int i = 0; i < thinkingStep; ++i) {
        dots += ".";
      }
      this->label->setText(_("Thinking") + dots);
    });
    thinkingTimer->start(400);
  }
}

void MessageBubble::updateText(const QString& text)
{
  if (thinkingTimer) {
    thinkingTimer->stop();
    thinkingTimer->deleteLater();
    thinkingTimer = nullptr;
  }
  this->label->setText(text);
}

bool MessageBubble::isDarkTheme() const
{
  QPalette pal = QApplication::palette();
  return pal.color(QPalette::Window).lightness() < 128;
}

// ChatWidget implementation
ChatWidget::ChatWidget(QWidget *parent) : QWidget(parent)
{
  setupUi(this);

  currentSessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);

  // Set titles/translations that might not be configured dynamically in UI files
  setupCursorHeader();
  setupCursorComposer();
  applyVSCodeChrome();

  // Connections
  connect(sendButton, &QPushButton::clicked, this, &ChatWidget::onSendPressed);
  connect(inputField, &ChatInputEdit::sendPressed, this, &ChatWidget::onSendPressed);
  if (historyButton) {
    connect(historyButton, &QPushButton::clicked, this, &ChatWidget::onHistoryPressed);
  }
  if (moreButton) {
    connect(moreButton, &QPushButton::clicked, this, &ChatWidget::onMorePressed);
  }
  if (layoutButton) {
    connect(layoutButton, &QPushButton::clicked, this, &ChatWidget::onTogglePanelPressed);
  }

  // Initialize backend and state
  aiService = std::make_shared<AIService>();
  aliveState = std::make_shared<bool>(true);

  // Register tool executor callback
  aiService->registerToolExecutor([this](const std::string& name, const std::string& arguments_json) {
    auto promise = std::make_shared<std::promise<std::string>>();
    auto future = promise->get_future();

    QMetaObject::invokeMethod(
      qApp,
      [this, promise, name, arguments_json]() {
        try {
          std::string result_val = this->executeTool(name, arguments_json);
          promise->set_value(result_val);
        } catch (const std::exception& e) {
          promise->set_value(std::string("Error parsing/executing tool: ") + e.what());
        } catch (...) {
          promise->set_value("Error: Unknown exception occurred during tool execution.");
        }
      },
      Qt::QueuedConnection);

    return future.get();
  });

  // Initial welcome greeting
  addMessage(_("Hello! Describe the 3D model you want — I will write OpenSCAD code and render "
               "a preview automatically. Try \"draw a sphere\" or \"create a box with a hole\"."),
             false);

  std::string defPrompt = aiService->getDefaultPrompt();
  if (!defPrompt.empty()) {
    inputField->setPlainText(QString::fromStdString(defPrompt));
  }
  updateComposerActionButton();
}

ChatWidget::~ChatWidget()
{
  *aliveState = false;
  aiService->cancelPendingRequests();
}

void ChatWidget::onSendPressed()
{
  if (isRequestRunning) {
    aiService->cancelPendingRequests();
    if (activeAIBubble) {
      std::string stop_msg = activeResponseText ? *activeResponseText : "";
      if (stop_msg.empty()) {
        scrollLayout->removeWidget(activeAIBubble);
        delete activeAIBubble;
      } else {
        stop_msg += "\n\n*[Request Stopped by User]*";
        activeAIBubble->updateText(QString::fromStdString(stop_msg));
        if (activeResponseText && !activeResponseText->empty()) {
          this->history.push_back({"assistant", *activeResponseText});
        }
      }
    }
    isRequestRunning = false;
    activeAIBubble = nullptr;
    activeResponseText = nullptr;
    enableInput(true);
    return;
  }

  QString prompt = inputField->toPlainText().trimmed();
  if (prompt.isEmpty()) {
    return;
  }

  inputField->clear();
  addMessage(prompt, true);

  // Save to history
  history.push_back({"user", prompt.toStdString()});

  // Set active request states
  isRequestRunning = true;
  startNewResponseTurn();

  // Disable input during streaming
  enableInput(false);

  auto alive = this->aliveState;

  aiService->chatCompletionStream(
    history,
    [this, alive](const std::string& chunk) {
      QMetaObject::invokeMethod(qApp, [this, alive, chunk]() {
        if (!*alive || !isRequestRunning) return;
        *activeResponseText += chunk;
        activeAIBubble->updateText(QString::fromStdString(*activeResponseText));
        // Auto-scroll to bottom
        this->scrollArea->verticalScrollBar()->setValue(
          this->scrollArea->verticalScrollBar()->maximum());
      });
    },
    [this, alive](const std::string& error_msg) {
      QMetaObject::invokeMethod(qApp, [this, alive, error_msg]() {
        if (!*alive || !isRequestRunning) return;
        std::string display_err = "Error: " + error_msg;
        if (activeResponseText && activeResponseText->empty()) {
          activeAIBubble->updateText(QString::fromStdString(display_err));
        } else {
          this->addMessage(QString::fromStdString(display_err), false);
        }
        isRequestRunning = false;
        activeAIBubble = nullptr;
        activeResponseText = nullptr;
        this->enableInput(true);
      });
    },
    [this, alive]() {
      QMetaObject::invokeMethod(qApp, [this, alive]() {
        if (!*alive || !isRequestRunning) return;
        if (activeResponseText && activeResponseText->empty()) {
          if (activeAIBubble) {
            scrollLayout->removeWidget(activeAIBubble);
            delete activeAIBubble;
            activeAIBubble = nullptr;
          }
        } else if (activeResponseText) {
          this->history.push_back({"assistant", *activeResponseText});
          this->saveCurrentSession();
        }
        isRequestRunning = false;
        activeAIBubble = nullptr;
        activeResponseText = nullptr;
        this->enableInput(true);
      });
    });
}

void ChatWidget::startNewResponseTurn()
{
  activeResponseText = std::make_shared<std::string>();
  activeAIBubble = addMessage(_("Thinking..."), false);
  activeToolBubble = nullptr;
}

MessageBubble *ChatWidget::addMessage(const QString& text, bool isUser)
{
  MessageBubble *bubble = new MessageBubble(text, isUser, scrollAreaWidgetContents);
  scrollLayout->insertWidget(scrollLayout->count() - 1, bubble);

  // Auto-scroll to bottom after layout calculation
  QTimer::singleShot(50, this, [this]() {
    this->scrollArea->verticalScrollBar()->setValue(this->scrollArea->verticalScrollBar()->maximum());
  });

  return bubble;
}

void ChatWidget::onClearPressed()
{
  if (!history.empty()) {
    saveCurrentSession();
  }

  clearMessageWidgets();
  history.clear();
  activeToolBubble = nullptr;
  currentSessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);

  addMessage(_("Hello! Describe the 3D model you want — I will write OpenSCAD code and render "
               "a preview automatically. Try \"draw a sphere\" or \"create a box with a hole\"."),
             false);

  std::string defPrompt = aiService->getDefaultPrompt();
  if (!defPrompt.empty()) {
    inputField->setPlainText(QString::fromStdString(defPrompt));
  } else {
    inputField->clear();
  }
}

void ChatWidget::clearMessageWidgets()
{
  QLayoutItem *child;
  while (scrollLayout->count() > 1) {
    child = scrollLayout->takeAt(0);
    if (child->widget()) {
      delete child->widget();
    }
    delete child;
  }
}

void ChatWidget::rebuildMessageWidgets()
{
  clearMessageWidgets();
  activeToolBubble = nullptr;
  activeAIBubble = nullptr;
  activeResponseText = nullptr;

  bool hasVisible = false;
  for (const auto& msg : history) {
    if (msg.role == "user") {
      addMessage(QString::fromStdString(msg.content), true);
      hasVisible = true;
    } else if (msg.role == "assistant" && !msg.content.empty()) {
      addMessage(QString::fromStdString(msg.content), false);
      hasVisible = true;
    }
  }

  if (!hasVisible) {
    addMessage(_("Hello! Describe the 3D model you want — I will write OpenSCAD code and render "
                 "a preview automatically. Try \"draw a sphere\" or \"create a box with a hole\"."),
               false);
  }
}

QString ChatWidget::sessionTitleFromHistory() const
{
  for (const auto& msg : history) {
    if (msg.role == "user" && !msg.content.empty()) {
      QString title = QString::fromStdString(msg.content).simplified();
      if (title.size() > 48) {
        title = title.left(45) + QStringLiteral("...");
      }
      return title;
    }
  }
  return _("Untitled chat");
}

void ChatWidget::saveCurrentSession()
{
  if (history.empty()) return;

  QJsonArray messages;
  for (const auto& msg : history) {
    if (msg.role != "user" && msg.role != "assistant") continue;
    if (msg.content.empty()) continue;
    QJsonObject obj;
    obj.insert(QStringLiteral("role"), QString::fromStdString(msg.role));
    obj.insert(QStringLiteral("content"), QString::fromStdString(msg.content));
    messages.append(obj);
  }
  if (messages.isEmpty()) return;

  QJsonObject session;
  session.insert(QStringLiteral("id"), currentSessionId);
  session.insert(QStringLiteral("title"), sessionTitleFromHistory());
  session.insert(QStringLiteral("updatedAt"), QDateTime::currentMSecsSinceEpoch());
  session.insert(QStringLiteral("messages"), messages);

  QJsonArray chats = loadSavedChatsArray();
  QJsonArray next;
  next.append(session);
  for (const QJsonValue& value : chats) {
    const QJsonObject obj = value.toObject();
    if (obj.value(QStringLiteral("id")).toString() == currentSessionId) continue;
    next.append(obj);
  }
  // Keep a reasonable history size
  while (next.size() > 30) {
    next.removeLast();
  }
  storeSavedChatsArray(next);
}

void ChatWidget::loadSession(const QString& sessionId)
{
  if (isRequestRunning) return;

  if (!history.empty() && sessionId != currentSessionId) {
    saveCurrentSession();
  }

  const QJsonArray chats = loadSavedChatsArray();
  for (const QJsonValue& value : chats) {
    const QJsonObject obj = value.toObject();
    if (obj.value(QStringLiteral("id")).toString() != sessionId) continue;

    currentSessionId = sessionId;
    history.clear();
    const QJsonArray messages = obj.value(QStringLiteral("messages")).toArray();
    for (const QJsonValue& msgVal : messages) {
      const QJsonObject msgObj = msgVal.toObject();
      ChatMessage msg;
      msg.role = msgObj.value(QStringLiteral("role")).toString().toStdString();
      msg.content = msgObj.value(QStringLiteral("content")).toString().toStdString();
      history.push_back(msg);
    }
    rebuildMessageWidgets();
    inputField->clear();
    return;
  }
}

void ChatWidget::onHistoryPressed()
{
  QMenu menu(this);
  menu.setObjectName(QStringLiteral("chatHistoryMenu"));

  const QJsonArray chats = loadSavedChatsArray();
  bool hasEntries = false;
  for (const QJsonValue& value : chats) {
    const QJsonObject obj = value.toObject();
    const QString id = obj.value(QStringLiteral("id")).toString();
    const QString title = obj.value(QStringLiteral("title")).toString(_("Untitled chat"));
    if (id.isEmpty()) continue;
    hasEntries = true;
    QAction *action = menu.addAction(title);
    action->setCheckable(true);
    action->setChecked(id == currentSessionId);
    connect(action, &QAction::triggered, this, [this, id]() { loadSession(id); });
  }

  if (!hasEntries) {
    QAction *empty = menu.addAction(_("No chat history yet"));
    empty->setEnabled(false);
  }

  menu.exec(historyButton->mapToGlobal(QPoint(0, historyButton->height())));
}

void ChatWidget::onMorePressed()
{
  QMenu menu(this);
  QAction *apiKeyAction = menu.addAction(_("Configure API key…"));
  connect(apiKeyAction, &QAction::triggered, this, []() {
    GlobalPreferences::inst()->showAISettings();
  });
  menu.addSeparator();
  QAction *clearAction = menu.addAction(_("Clear chat"));
  clearAction->setEnabled(!isRequestRunning);
  connect(clearAction, &QAction::triggered, this, &ChatWidget::onClearPressed);
  menu.exec(moreButton->mapToGlobal(QPoint(0, moreButton->height())));
}

QDockWidget *ChatWidget::parentDock() const
{
  QWidget *w = parentWidget();
  while (w && !qobject_cast<QDockWidget *>(w)) {
    w = w->parentWidget();
  }
  return qobject_cast<QDockWidget *>(w);
}

void ChatWidget::restoreExpandedChrome()
{
  if (scrollArea) scrollArea->setVisible(true);
  if (inputWidget) inputWidget->setVisible(true);
  if (titleLabel) titleLabel->setVisible(true);
  if (historyButton) historyButton->setVisible(true);
  if (moreButton) moreButton->setVisible(true);
  if (layoutButton) {
    layoutButton->setVisible(true);
    layoutButton->setFixedSize(28, 28);
    layoutButton->setIconSize(QSize(16, 16));
    layoutButton->setToolTip(_("Hide AI chat"));
  }
  if (headerWidget) {
    headerWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    headerWidget->setMinimumSize(0, 0);
    headerWidget->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    headerWidget->setStyleSheet(QString());
  }
  if (headerLayout) {
    headerLayout->setContentsMargins(8, 4, 6, 0);
    headerLayout->setSpacing(2);
    for (int i = 0; i < headerLayout->count(); ++i) {
      if (QSpacerItem *spacer = headerLayout->itemAt(i)->spacerItem()) {
        spacer->changeSize(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);
      }
    }
  }
  if (mainLayout) {
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
  }
  setMinimumWidth(0);
  setMaximumWidth(QWIDGETSIZE_MAX);
  applyVSCodeChrome();
}

void ChatWidget::setCollapsed(bool collapsed)
{
  if (panelCollapsed == collapsed) return;
  panelCollapsed = collapsed;

  QDockWidget *dock = parentDock();
  if (collapsed) {
    if (dock) {
      expandedDockWidth = qMax(200, dock->width());
      dock->hide();
    }
  } else {
    restoreExpandedChrome();
    if (dock) {
      dock->setMinimumWidth(220);
      dock->setMaximumWidth(QWIDGETSIZE_MAX);
      dock->show();
      dock->raise();
      if (auto *mw = qobject_cast<QMainWindow *>(dock->parentWidget())) {
        mw->resizeDocks({dock}, {expandedDockWidth}, Qt::Horizontal);
      }
    }
  }

  emit collapsedChanged(collapsed);
}

void ChatWidget::onTogglePanelPressed()
{
  setCollapsed(!panelCollapsed);
}

void ChatWidget::enableInput(bool enabled)
{
  inputField->setEnabled(true);  // keep editable; Stop uses the circular action button
  if (attachButton) attachButton->setEnabled(enabled || !isRequestRunning);
  if (historyButton) historyButton->setEnabled(enabled || !isRequestRunning);
  if (moreButton) moreButton->setEnabled(true);
  if (enabled) {
    inputField->setFocus();
    activeToolBubble = nullptr;
  }
  updateComposerActionButton();
}

void ChatWidget::updateComposerActionButton()
{
  const bool dark = isDarkTheme();
  sendButton->setText(QString());
  sendButton->setIcon(makeCircularSendIcon(isRequestRunning, dark));
  sendButton->setIconSize(QSize(18, 18));
  sendButton->setToolTip(isRequestRunning ? _("Stop") : _("Send"));
  sendButton->setEnabled(true);
}

void ChatWidget::setupCursorHeader()
{
  const bool dark = isDarkTheme();

  while (QLayoutItem *item = headerLayout->takeAt(0)) {
    if (item->widget()) {
      item->widget()->setParent(nullptr);
    }
    delete item;
  }

  headerLayout->setContentsMargins(8, 4, 6, 0);
  headerLayout->setSpacing(2);

  // Single persistent chat — no tab strip / close / new-chat controls
  titleLabel->setParent(headerWidget);
  titleLabel->setText(_("Chat"));
  titleLabel->setObjectName("titleLabel");
  headerLayout->addWidget(titleLabel, 0);
  headerLayout->addStretch(1);

  clearButton->setVisible(false);

  historyButton =
    makeHeaderIconButton("headerHistoryButton", makeHistoryIcon(dark), _("Chat history"), headerWidget);
  moreButton = makeHeaderIconButton("headerMoreButton", makeEllipsisIcon(dark), _("More"), headerWidget);
  layoutButton = makeHeaderIconButton("headerLayoutButton", makeSidebarIcon(dark), _("Hide AI chat"),
                                      headerWidget);

  headerLayout->addWidget(historyButton);
  headerLayout->addWidget(moreButton);
  headerLayout->addWidget(layoutButton);
}

void ChatWidget::setupCursorComposer()
{
  // Rebuild input area into a Cursor-style prompt card
  while (QLayoutItem *item = inputLayout->takeAt(0)) {
    if (item->widget()) {
      item->widget()->setParent(nullptr);
    }
    delete item;
  }

  inputLayout->setContentsMargins(12, 8, 12, 8);
  inputLayout->setSpacing(0);

  QFrame *composer = new QFrame(inputWidget);
  composer->setObjectName("composerCard");
  auto *composerLayout = new QVBoxLayout(composer);
  composerLayout->setContentsMargins(10, 6, 10, 6);
  composerLayout->setSpacing(4);

  inputField->setParent(composer);
  inputField->setPlaceholderText(_("Add a follow-up"));
  inputField->setMaximumHeight(80);
  inputField->setMinimumHeight(28);
  composerLayout->addWidget(inputField);

  auto *toolbar = new QWidget(composer);
  auto *toolbarLayout = new QHBoxLayout(toolbar);
  toolbarLayout->setContentsMargins(0, 0, 0, 0);
  toolbarLayout->setSpacing(6);
  toolbarLayout->addStretch(1);

  attachButton = new QPushButton(toolbar);
  attachButton->setObjectName("attachButton");
  attachButton->setIcon(QIcon::fromTheme(QStringLiteral("chokusen-paper-clip")));
  attachButton->setIconSize(QSize(18, 18));
  attachButton->setFixedSize(24, 24);
  attachButton->setFlat(true);
  attachButton->setCursor(Qt::PointingHandCursor);
  attachButton->setToolTip(_("Attach file"));
  toolbarLayout->addWidget(attachButton);

  sendButton->setParent(toolbar);
  sendButton->setObjectName("sendButton");
  sendButton->setText(QString());
  sendButton->setFixedSize(22, 22);
  sendButton->setMinimumSize(22, 22);
  sendButton->setMaximumSize(22, 22);
  sendButton->setCursor(Qt::PointingHandCursor);
  sendButton->setFlat(true);
  toolbarLayout->addWidget(sendButton);

  composerLayout->addWidget(toolbar);
  inputLayout->addWidget(composer);

  connect(inputField, &QPlainTextEdit::textChanged, this, &ChatWidget::updateComposerActionButton);
  updateComposerActionButton();
}

bool ChatWidget::isDarkTheme() const
{
  QPalette pal = QApplication::palette();
  return pal.color(QPalette::Window).lightness() < 128;
}

void ChatWidget::applyVSCodeChrome()
{
  const bool dark = isDarkTheme();
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);
  scrollLayout->setContentsMargins(12, 8, 12, 8);
  scrollLayout->setSpacing(4);

  if (attachButton) {
    attachButton->setIcon(QIcon::fromTheme(QStringLiteral("chokusen-paper-clip")));
  }
  if (historyButton) historyButton->setIcon(makeHistoryIcon(dark));
  if (moreButton) moreButton->setIcon(makeEllipsisIcon(dark));
  if (layoutButton) layoutButton->setIcon(makeSidebarIcon(dark));
  updateComposerActionButton();

  if (dark) {
    setStyleSheet(QStringLiteral(R"(
      ChatWidget, QWidget#ChatWidget {
        background-color: #1e1e1e;
      }
      QWidget#headerWidget {
        background-color: #252526;
        border-bottom: 1px solid #2b2b2b;
        min-height: 36px;
      }
      QLabel#titleLabel {
        color: #cccccc;
        font-size: 13px;
        font-weight: 500;
        padding-left: 4px;
      }
      QPushButton#headerHistoryButton,
      QPushButton#headerMoreButton, QPushButton#headerLayoutButton {
        background: transparent;
        border: none;
        border-radius: 4px;
      }
      QPushButton#headerHistoryButton:hover,
      QPushButton#headerMoreButton:hover, QPushButton#headerLayoutButton:hover {
        background: #2a2d2e;
      }
      QScrollArea {
        background: #1e1e1e;
        border: none;
      }
      QWidget#scrollAreaWidgetContents {
        background: #1e1e1e;
      }
      QWidget#inputWidget {
        background-color: #1e1e1e;
        border-top: none;
      }
      QFrame#composerCard {
        background: #1e1e1e;
        border: 1px solid #3c3c3c;
        border-radius: 12px;
      }
      ChatInputEdit, QPlainTextEdit#inputField {
        background: transparent;
        color: #cccccc;
        border: none;
        padding: 2px 0px;
        font-size: 13px;
        selection-background-color: #264f78;
      }
      QPushButton#attachButton, QPushButton#sendButton {
        background: transparent;
        border: none;
        border-radius: 11px;
        padding: 0px;
      }
      QPushButton#attachButton:hover, QPushButton#sendButton:hover {
        background: #2a2d2e;
      }
    )"));
  } else {
    setStyleSheet(QStringLiteral(R"(
      ChatWidget, QWidget#ChatWidget {
        background-color: #f8f8f8;
      }
      QWidget#headerWidget {
        background-color: #f3f3f3;
        border-bottom: 1px solid #e5e5e5;
        min-height: 36px;
      }
      QLabel#titleLabel {
        color: #1e1e1e;
        font-size: 13px;
        font-weight: 500;
        padding-left: 4px;
      }
      QPushButton#headerHistoryButton,
      QPushButton#headerMoreButton, QPushButton#headerLayoutButton {
        background: transparent;
        border: none;
        border-radius: 4px;
      }
      QPushButton#headerHistoryButton:hover,
      QPushButton#headerMoreButton:hover, QPushButton#headerLayoutButton:hover {
        background: #e8e8e8;
      }
      QScrollArea {
        background: #f8f8f8;
        border: none;
      }
      QWidget#scrollAreaWidgetContents {
        background: #f8f8f8;
      }
      QWidget#inputWidget {
        background-color: #f8f8f8;
        border-top: none;
      }
      QFrame#composerCard {
        background: #f8f8f8;
        border: 1px solid #e0e0e0;
        border-radius: 12px;
      }
      ChatInputEdit, QPlainTextEdit#inputField {
        background: transparent;
        color: #1e1e1e;
        border: none;
        padding: 2px 0px;
        font-size: 13px;
        selection-background-color: #add6ff;
      }
      QPushButton#attachButton, QPushButton#sendButton {
        background: transparent;
        border: none;
        border-radius: 11px;
        padding: 0px;
      }
      QPushButton#attachButton:hover, QPushButton#sendButton:hover {
        background: #f0f0f0;
      }
    )"));
  }
}

void ChatWidget::applyCodeChange(const std::string& code)
{
  MainWindow *mw = nullptr;
  for (auto *win : scadApp->windowManager.getWindows()) {
    mw = win;
    break;
  }
  if (mw && mw->activeEditor) {
    mw->activeEditor->setText(QString::fromStdString(code));
    mw->actionRenderPreview();
  }
}

void ChatWidget::logToolExecution(const std::string& name, const std::string& result)
{
  QString summary;
  QString detail;

  if (name == "get_editor_code") {
    summary = tr("Inspected current code");
    detail = tr("Tool: get_editor_code\nResult: Read %1 lines.")
               .arg(QString::fromStdString(result).count('\n'));
  } else if (name == "set_editor_code") {
    summary = tr("Applied code changes");
    detail = tr("Tool: set_editor_code\nResult: Applied code to the active editor and triggered preview.");
  } else if (name == "trigger_preview") {
    summary = tr("Triggered render preview");
    detail = QString::fromStdString("Tool: trigger_preview\nResult: " + result);
  } else {
    summary = tr("Executed tool: %1").arg(QString::fromStdString(name));
    detail = QString::fromStdString("Tool: " + name + "\nResult: " + result);
  }

  // Find or create the active collapsible tool bubble
  if (!activeToolBubble || !isRequestRunning) {
    activeToolBubble = new CollapsibleBubble(summary, detail, this);
    int idx = scrollLayout->indexOf(activeAIBubble);
    if (idx != -1) {
      scrollLayout->insertWidget(idx, activeToolBubble);
    } else {
      scrollLayout->insertWidget(scrollLayout->count() - 1, activeToolBubble);
    }
  } else {
    activeToolBubble->addToolCall(summary, detail);
  }

  // Scroll to bottom
  scrollArea->verticalScrollBar()->setValue(scrollArea->verticalScrollBar()->maximum());
}

std::string ChatWidget::executeTool(const std::string& name, const std::string& arguments_json)
{
  MainWindow *mw = nullptr;
  for (auto *win : scadApp->windowManager.getWindows()) {
    mw = win;
    break;
  }

  std::string result_val;

  if (name == "get_editor_code") {
    if (mw && mw->activeEditor) {
      result_val = mw->activeEditor->toPlainText().toStdString();
    } else {
      result_val = "Error: No active editor found.";
    }
  } else if (name == "set_editor_code") {
    auto args = nlohmann::json::parse(arguments_json);
    if (!args.contains("code")) {
      return "Error: Missing required argument 'code'.";
    }
    std::string code = args["code"].get<std::string>();
    this->applyCodeChange(code);
    result_val = "Success: Code applied to the editor and preview triggered.";
  } else if (name == "trigger_preview") {
    if (mw) {
      mw->actionRenderPreview();
      result_val = "Success: Preview triggered.";
    } else {
      result_val = "Error: No active MainWindow found.";
    }
  } else {
    result_val = "Error: Unknown tool name '" + name + "'.";
  }

  this->logToolExecution(name, result_val);
  return result_val;
}
