#include "gui/ai/ChatWidget.h"
#include "gui/qtgettext.h"
#include "json/json.hpp"
#include "core/AIClient.h"
#include "core/AIFreeAgents.h"
#include "openscad_gui.h"
#include "gui/ai/OpenSCADAiBridge.h"
#include <future>
#include <QEventLoop>
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
#include <cctype>
#include <sstream>
#include <functional>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QIcon>
#include <QSize>
#include <QPolygonF>
#include <QKeyEvent>
#include <QApplication>
#include <QClipboard>
#include <QThread>
#include <QPalette>
#include <QMenu>
#include <QMessageBox>
#include <QAction>
#include <QSettings>
#include <QUuid>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDockWidget>
#include <QMainWindow>
#include <QFileDialog>
#include <QBuffer>
#include <QByteArray>
#include <QImageReader>
#include <QTextCursor>
#include <QGraphicsDropShadowEffect>
#include <QMouseEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QInputDialog>
#include <QLineEdit>
#include "gui/MainWindow.h"
#include "gui/QGLView.h"
#include "gui/Console.h"
#include "platform/PlatformUtils.h"
#include <QBuffer>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <QImage>
#include "gui/OpenSCADApp.h"
#include "gui/Preferences.h"
#include "gui/ai/AIApiKeyDialog.h"
#include "gui/ai/ChatInputEdit.h"

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
  // VS Code / Cursor style: filled disc with up-arrow (send) or stop square.
  return makeHiDpiIcon(20, [stopMode, dark](QPainter& p, int s) {
    const QRectF circle(0.5, 0.5, s - 1.0, s - 1.0);
    p.setPen(Qt::NoPen);
    const QColor fill = dark ? QColor("#e8e8e8") : QColor("#1a1a1a");
    const QColor glyph = dark ? QColor("#1a1a1a") : QColor("#ffffff");
    p.setBrush(fill);
    p.drawEllipse(circle);

    if (stopMode) {
      // Clear stop square in the center
      const qreal side = s * 0.34;
      const qreal x = (s - side) * 0.5;
      p.setBrush(glyph);
      p.drawRoundedRect(QRectF(x, x, side, side), 1.5, 1.5);
    } else {
      // Upward send arrow
      QPen pen(glyph, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
      p.setPen(pen);
      p.setBrush(Qt::NoBrush);
      const qreal cx = s * 0.5;
      p.drawLine(QPointF(cx, s * 0.28), QPointF(cx, s * 0.72));
      p.drawLine(QPointF(cx, s * 0.28), QPointF(s * 0.32, s * 0.48));
      p.drawLine(QPointF(cx, s * 0.28), QPointF(s * 0.68, s * 0.48));
    }
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

// Crisp status dot with a subtle highlight, so it reads as a small glossy LED
// rather than a muddy blob. Green when the MCP bridge is live, gray when off.
QIcon makeStatusDotIcon(const QColor& color)
{
  return makeHiDpiIcon(8, [color](QPainter& p, int s) {
    const QPointF c(s * 0.5, s * 0.5);
    const qreal r = s * 0.34;
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(c, r, r);
    // Tiny specular highlight on the upper-left for a refined LED look.
    QColor hl(255, 255, 255, 150);
    p.setBrush(hl);
    p.drawEllipse(QPointF(c.x() - r * 0.32, c.y() - r * 0.32), r * 0.34, r * 0.34);
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
MessageBubble::MessageBubble(const QString& text, bool isUser, QWidget *parent) : QWidget(parent),
  userMessage(isUser)
{
  QVBoxLayout *outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 8, 0, 8);
  outer->setSpacing(6);

  bool dark = isDarkTheme();

  QWidget *header = new QWidget(this);
  QHBoxLayout *headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(0, 0, 0, 0);
  headerLayout->setSpacing(4);

  QLabel *roleLabel = new QLabel(isUser ? _("You") : _("Cad Agent"), header);
  roleLabel->setStyleSheet(dark ? "QLabel { color: #cccccc; font-size: 12px; font-weight: 600; }"
                                : "QLabel { color: #616161; font-size: 12px; font-weight: 600; }");
  headerLayout->addWidget(roleLabel, 1);

  if (isUser) {
    const QString actionBtnStyle =
      dark ? "QPushButton { border: none; border-radius: 4px; padding: 0; }"
             "QPushButton:hover { background-color: #3c3c3c; }"
             "QPushButton:disabled { opacity: 0.4; }"
           : "QPushButton { border: none; border-radius: 4px; padding: 0; }"
             "QPushButton:hover { background-color: #e8e8e8; }"
             "QPushButton:disabled { opacity: 0.4; }";

    copyButton = new QPushButton(header);
    copyButton->setFlat(true);
    copyButton->setCursor(Qt::PointingHandCursor);
    copyButton->setToolTip(_("Copy message text"));
    copyButton->setIcon(QIcon::fromTheme(QStringLiteral("chokusen-copy")));
    copyButton->setIconSize(QSize(14, 14));
    copyButton->setFixedSize(22, 22);
    copyButton->setStyleSheet(actionBtnStyle);
    headerLayout->addWidget(copyButton, 0, Qt::AlignRight | Qt::AlignVCenter);
    connect(copyButton, &QPushButton::clicked, this, [this]() {
      if (!label) return;
      const QString text = label->text();
      if (text.isEmpty()) return;
      if (QClipboard *clipboard = QApplication::clipboard()) {
        clipboard->setText(text);
      }
    });

    editButton = new QPushButton(header);
    editButton->setFlat(true);
    editButton->setCursor(Qt::PointingHandCursor);
    editButton->setToolTip(_("Edit this message and resend"));
    editButton->setIcon(QIcon::fromTheme(QStringLiteral("chokusen-edit")));
    editButton->setIconSize(QSize(14, 14));
    editButton->setFixedSize(22, 22);
    editButton->setStyleSheet(actionBtnStyle);
    headerLayout->addWidget(editButton, 0, Qt::AlignRight | Qt::AlignVCenter);
    connect(editButton, &QPushButton::clicked, this, [this]() {
      if (historyIndex >= 0) {
        emit editRequested(historyIndex);
      }
    });
  }

  if (!isUser) {
    moreButton = new QPushButton(QStringLiteral("···"), header);
    moreButton->setCheckable(true);
    moreButton->setChecked(false);
    moreButton->setFlat(true);
    moreButton->setCursor(Qt::PointingHandCursor);
    moreButton->setToolTip(_("Show used tools"));
    moreButton->setFixedSize(28, 22);
    moreButton->setVisible(false);
    moreButton->setStyleSheet(
      dark ? "QPushButton { color: #cccccc; border: none; border-radius: 4px; font-size: 14px; "
             "font-weight: 700; letter-spacing: 1px; padding: 0; }"
             "QPushButton:hover { background-color: #3c3c3c; color: #ffffff; }"
             "QPushButton:checked { background-color: #3c3c3c; color: #ffffff; }"
           : "QPushButton { color: #616161; border: none; border-radius: 4px; font-size: 14px; "
             "font-weight: 700; letter-spacing: 1px; padding: 0; }"
             "QPushButton:hover { background-color: #e8e8e8; color: #333333; }"
             "QPushButton:checked { background-color: #e8e8e8; color: #333333; }");
    headerLayout->addWidget(moreButton, 0, Qt::AlignRight | Qt::AlignVCenter);

    toolsPanel = new QFrame(this);
    toolsPanel->setObjectName(QStringLiteral("toolsPanel"));
    toolsPanel->setFrameShape(QFrame::NoFrame);
    toolsPanel->setStyleSheet(
      dark ? "#toolsPanel { background-color: #252526; border: 1px solid #3c3c3c; "
             "border-radius: 10px; }"
           : "#toolsPanel { background-color: #f3f3f3; border: 1px solid #e5e5e5; "
             "border-radius: 10px; }");
    QVBoxLayout *toolsLayout = new QVBoxLayout(toolsPanel);
    toolsLayout->setContentsMargins(14, 12, 14, 12);
    toolsLayout->setSpacing(8);

    QLabel *toolsTitle = new QLabel(_("Used tools"), toolsPanel);
    toolsTitle->setStyleSheet(
      dark ? "QLabel { color: #9cdcfe; font-size: 12px; font-weight: 600; "
             "background: transparent; border: none; }"
           : "QLabel { color: #0451a5; font-size: 12px; font-weight: 600; "
             "background: transparent; border: none; }");
    toolsDetailLabel = new QLabel(toolsPanel);
    toolsDetailLabel->setWordWrap(true);
    toolsDetailLabel->setTextFormat(Qt::RichText);
    toolsDetailLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    toolsDetailLabel->setStyleSheet(
      "QLabel { background: transparent; border: none; padding: 0; }");
    toolsLayout->addWidget(toolsTitle);
    toolsLayout->addWidget(toolsDetailLabel);
    toolsPanel->hide();

    connect(moreButton, &QPushButton::toggled, this, [this](bool checked) {
      if (toolsPanel) {
        toolsPanel->setVisible(checked);
      }
      moreButton->setToolTip(checked ? _("Hide used tools") : _("Show used tools"));
    });
  }

  outer->addWidget(header);
  if (toolsPanel) {
    outer->addWidget(toolsPanel);
  }

  QFrame *bubbleFrame = new QFrame(this);
  bubbleFrame->setFrameShape(QFrame::NoFrame);
  bubbleFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

  QString frameStyle;
  QString labelStyle;
  if (isUser) {
    // VS Code chat user request — soft fill, no hard “input box” border
    if (dark) {
      frameStyle = "QFrame { background-color: #2b2d2e; border: none; border-radius: 8px; }";
      labelStyle =
        "QLabel { color: #e3e3e3; font-size: 13px; background: transparent; padding: 0px; }";
    } else {
      frameStyle = "QFrame { background-color: #ebebeb; border: none; border-radius: 8px; }";
      labelStyle =
        "QLabel { color: #1f1f1f; font-size: 13px; background: transparent; padding: 0px; }";
    }
  } else {
    // Assistant — flat prose, no bubble chrome
    if (dark) {
      frameStyle = "QFrame { background-color: transparent; border: none; border-radius: 0px; }";
      labelStyle = "QLabel { color: #cccccc; font-size: 13px; background: transparent; }";
    } else {
      frameStyle = "QFrame { background-color: transparent; border: none; border-radius: 0px; }";
      labelStyle = "QLabel { color: #1f1f1f; font-size: 13px; background: transparent; }";
    }
  }

  bubbleFrame->setStyleSheet(frameStyle);

  QVBoxLayout *frameLayout = new QVBoxLayout(bubbleFrame);
  frameLayout->setContentsMargins(isUser ? 12 : 0, isUser ? 10 : 0, isUser ? 12 : 0, isUser ? 10 : 0);
  frameLayout->setSpacing(0);

  this->label = new QLabel(bubbleFrame);
  this->label->setWordWrap(true);
  this->label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
  this->label->setStyleSheet(labelStyle);
  this->label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
  setBodyText(text);

  imageStrip = new QWidget(bubbleFrame);
  imageStripLayout = new QHBoxLayout(imageStrip);
  imageStripLayout->setContentsMargins(0, 0, 0, 6);
  imageStripLayout->setSpacing(6);
  imageStrip->hide();

  contentLayout = frameLayout;
  frameLayout->addWidget(imageStrip);
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
      setBodyText(_("Thinking") + dots);
    });
    thinkingTimer->start(400);
  }
}

void MessageBubble::setBodyText(const QString& text)
{
  if (!label) return;
  const bool thinking =
    text.startsWith(_("Thinking")) || text == _("Thinking...");
  // Assistant replies are markdown (fenced code blocks, bold, lists). User + thinking stay plain.
  if (!userMessage && !thinking) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    label->setTextFormat(Qt::MarkdownText);
#else
    label->setTextFormat(Qt::PlainText);
#endif
  } else {
    label->setTextFormat(Qt::PlainText);
  }
  label->setText(text);
}

void MessageBubble::updateText(const QString& text)
{
  if (thinkingTimer) {
    thinkingTimer->stop();
    thinkingTimer->deleteLater();
    thinkingTimer = nullptr;
  }
  setBodyText(text);
  this->label->setVisible(!text.isEmpty());
}

void MessageBubble::setEditEnabled(bool enabled)
{
  if (editButton) {
    editButton->setEnabled(enabled);
  }
}

void MessageBubble::setImages(const QList<QImage>& images)
{
  if (!imageStrip || !imageStripLayout) return;

  while (QLayoutItem *child = imageStripLayout->takeAt(0)) {
    if (child->widget()) delete child->widget();
    delete child;
  }

  if (images.isEmpty()) {
    imageStrip->hide();
    return;
  }

  for (const QImage& image : images) {
    if (image.isNull()) continue;
    auto *thumb = new QLabel(imageStrip);
    thumb->setFixedSize(72, 72);
    thumb->setAlignment(Qt::AlignCenter);
    thumb->setStyleSheet(
      "QLabel { background: #00000010; border-radius: 8px; border: 1px solid #00000018; }");
    const QPixmap pix = QPixmap::fromImage(image).scaled(72, 72, Qt::KeepAspectRatio,
                                                         Qt::SmoothTransformation);
    thumb->setPixmap(pix);
    imageStripLayout->addWidget(thumb);
  }
  imageStripLayout->addStretch(1);
  imageStrip->show();
}

void MessageBubble::addToolCall(const QString& summary, const QString& detail)
{
  toolCalls.push_back({summary, detail});
  updateToolsPanel();
  if (moreButton) {
    moreButton->setVisible(true);
  }
}

void MessageBubble::updateToolsPanel()
{
  if (!toolsDetailLabel) return;

  const bool dark = isDarkTheme();
  const QString titleColor = dark ? QStringLiteral("#e3e3e3") : QStringLiteral("#1f1f1f");
  const QString metaColor = dark ? QStringLiteral("#9cdcfe") : QStringLiteral("#0451a5");
  const QString bodyColor = dark ? QStringLiteral("#d4d4d4") : QStringLiteral("#333333");
  const QString divider = dark ? QStringLiteral("#3c3c3c") : QStringLiteral("#e5e5e5");

  QString html;
  for (size_t i = 0; i < toolCalls.size(); ++i) {
    if (i > 0) {
      html += QStringLiteral(
        "<div style='height:1px; background:%1; margin:10px 0;'></div>").arg(divider);
    }

    const QString summary = toolCalls[i].summary.toHtmlEscaped();
    QString toolLine;
    QString resultLine;
    const QStringList detailLines = toolCalls[i].detail.split('\n');
    for (const QString& line : detailLines) {
      if (line.startsWith(QStringLiteral("Tool:"))) {
        toolLine = line.mid(5).trimmed().toHtmlEscaped();
      } else if (line.startsWith(QStringLiteral("Result:"))) {
        resultLine = line.mid(7).trimmed().toHtmlEscaped();
      }
    }

    html += QStringLiteral(
      "<div style='margin:0;'>"
      "<div style='font-size:13px; font-weight:600; color:%1; line-height:1.35;'>%2</div>")
              .arg(titleColor, summary);
    if (!toolLine.isEmpty()) {
      html += QStringLiteral(
                "<div style='font-size:12px; color:%1; margin-top:3px; line-height:1.35;'>%2</div>")
                .arg(metaColor, toolLine);
    }
    if (!resultLine.isEmpty()) {
      html += QStringLiteral(
                "<div style='font-size:12px; color:%1; margin-top:2px; line-height:1.4;'>%2</div>")
                .arg(bodyColor, resultLine);
    }
    html += QStringLiteral("</div>");
  }

  toolsDetailLabel->setText(html);

  if (moreButton) {
    moreButton->setToolTip(
      tr("Show used tools (%1)").arg(static_cast<int>(toolCalls.size())));
  }
}

bool MessageBubble::isDarkTheme() const
{
  return isDarkMode();
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
  if (clearChatButton) {
    connect(clearChatButton, &QPushButton::clicked, this, &ChatWidget::onClearPressed);
  }
  if (layoutButton) {
    connect(layoutButton, &QPushButton::clicked, this, &ChatWidget::onTogglePanelPressed);
  }

  // Initialize backend and state
  aiService = std::make_shared<AIService>();
  aliveState = std::make_shared<bool>(true);

  // Register tool executor callback (HTTP AI path — callbacks arrive off the GUI thread).
  auto toolExec = [this](const std::string& name, const std::string& arguments_json) {
    if (QThread::currentThread() == qApp->thread()) {
      return this->executeTool(name, arguments_json);
    }
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
  };

  aiService->registerToolExecutor(toolExec);

  // Localhost bridge for MCP tools (same executor; avoids deadlock on GUI thread).
  // Started only when enabled in AI Settings → MCP.
  OpenSCADAiBridge::instance().setToolExecutor(toolExec);
  if (AIFreeAgents::mcpEnabled()) {
    OpenSCADAiBridge::instance().setDesiredPort(0);
    OpenSCADAiBridge::instance().start();
  }

  // Start with an empty chat — no welcome greeting.

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
  OpenSCADAiBridge::instance().setToolExecutor({});
  OpenSCADAiBridge::instance().stop();
}

void ChatWidget::onSendPressed()
{
  if (isRequestRunning) {
    stopActiveRequest(true);
    return;
  }

  QString prompt = inputField->toPlainText().trimmed();
  if (prompt.isEmpty() && pendingImages.isEmpty()) {
    return;
  }

  if (!ensureActiveProfileApiKey()) {
    return;
  }

  // When images are attached (e.g. 2D drawing sheets), inject reconstruction rules so the
  // model doesn't treat silhouettes as solid extrusions even if the user sends little text.
  if (!pendingImages.isEmpty() && !prompt.contains(QStringLiteral("[DRAWING→3D]"))) {
    static const QString kDrawingHint = QStringLiteral(
      "[DRAWING→3D] Rebuild a parametric 3D OpenSCAD model from the attached drawing.\n"
      "CRITICAL:\n"
      "- Top/Front silhouettes are OUTER outlines only — do NOT extrude into a solid block.\n"
      "- SECTION A-A / B-B: hatched = solid material; empty regions in the section = "
      "pockets/cavities/holes.\n"
      "- Read wall thickness, floor thickness, and pocket depth from the SECTION views "
      "(and any wall~/floor~ hints).\n"
      "- Typical tray/shell: difference() of outer body minus inner cavity, then hole "
      "cylinders from Top. Keep pockets open at the top if sections show an open tray.\n"
      "- Apply the full script with set_editor_code. Chat = short dimension summary only.");
    if (prompt.isEmpty()) {
      prompt = kDrawingHint;
    } else {
      prompt = prompt + QStringLiteral("\n\n") + kDrawingHint;
    }
  }

  const QList<QImage> imagesToSend = pendingImages;
  clearPendingImages();
  inputField->clear();

  ChatMessage userMsg;
  userMsg.role = "user";
  userMsg.content = prompt.toStdString();
  for (const QImage& image : imagesToSend) {
    const QString dataUrl = imageToDataUrl(image);
    if (!dataUrl.isEmpty()) {
      userMsg.images.push_back(dataUrl.toStdString());
    }
  }
  const int historyIndex = static_cast<int>(history.size());
  history.push_back(userMsg);

  const QString displayText = prompt.isEmpty() ? tr("(Image attached)") : prompt;
  addMessage(displayText, true, imagesToSend, historyIndex);

  // Set active request states
  isRequestRunning = true;
  pendingPreviewRender = false;
  updateComposerActionButton();  // switch to Stop immediately
  startNewResponseTurn();

  // Disable input during streaming
  enableInput(false);

  auto alive = this->aliveState;

  OpenSCADAiBridge::instance().resetSessionStats();

  aiService->chatCompletionStream(
    history,
    [this, alive](const std::string& chunk) {
      QMetaObject::invokeMethod(qApp, [this, alive, chunk]() {
        if (!*alive || !isRequestRunning) return;
        // Buffer tokens only — show the full reply once when the turn completes.
        *activeResponseText += chunk;
      });
    },
    [this, alive](const std::string& error_msg) {
      QMetaObject::invokeMethod(qApp, [this, alive, error_msg]() {
        if (!*alive || !isRequestRunning) return;
        QString display_err = QString::fromStdString(error_msg).trimmed();
        if (display_err.isEmpty()) {
          display_err = tr("Unknown error from the AI provider (empty response). "
                           "Check that Ollama is running and the model is pulled.");
        } else if (display_err.contains(QStringLiteral("API limit reached"), Qt::CaseInsensitive) ||
            display_err.contains(QStringLiteral("rate limited"), Qt::CaseInsensitive) ||
            display_err.contains(QStringLiteral("temporarily limited"), Qt::CaseInsensitive)) {
          display_err = tr("⚠️ API limited\n\n") + display_err;
        } else if (display_err.contains(QStringLiteral("API key missing"), Qt::CaseInsensitive) ||
                   display_err.contains(QStringLiteral("invalid"), Qt::CaseInsensitive)) {
          display_err = tr("⚠️ API key required\n\n") + display_err;
        } else {
          display_err = tr("Error: ") + display_err;
        }
        if (activeResponseText && activeResponseText->empty()) {
          activeAIBubble->updateText(display_err);
        } else {
          this->addMessage(display_err, false);
        }
        pendingPreviewRender = false;
        isRequestRunning = false;
        activeAIBubble = nullptr;
        activeResponseText = nullptr;
        this->enableInput(true);
        this->updateComposerActionButton();  // restore Send arrow
      });
    },
    [this, alive]() {
      QMetaObject::invokeMethod(qApp, [this, alive]() {
        if (!*alive || !isRequestRunning) return;

        // Hold the final chat reply + unlock until F6 render finishes so code,
        // chat response, and rendered geometry (exportable) land together.
        const bool needRender = pendingPreviewRender;
        pendingPreviewRender = false;

        auto finishTurn = [this, alive]() {
          if (!*alive) return;
          if (activeResponseText) {
            auto trimCopy = [](std::string s) {
              while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(0, 1);
              while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
              return s;
            };
            std::string text = trimCopy(*activeResponseText);
            auto isTrivial = [](const std::string& s) {
              if (s.empty()) return true;
              std::string lower;
              lower.reserve(s.size());
              for (unsigned char c : s) lower.push_back(static_cast<char>(std::tolower(c)));
              return lower == "ok" || lower == "okay" || lower == "done" || lower == "sure" ||
                     lower == "yes" || lower == "yep" || lower == "got it" || lower == "đã xong" ||
                     lower == "xong" || lower == "ok." || lower == "done.";
            };

            if (appliedCodeThisTurn) {
              if (text.empty() || isTrivial(text) || contentLooksLikeEmbeddedToolCall(text)) {
                text = tr("Applied the OpenSCAD model to the editor. "
                          "Check the 3D view / editor — say what to change next.")
                         .toStdString();
              }
            } else if (contentLooksLikeEmbeddedToolCall(text) &&
                       tryParseEmbeddedToolCalls(text).empty()) {
              text = tr("Error: Model returned a tool call that could not be parsed. "
                        "Try again, or simplify the request.")
                       .toStdString();
            }

            *activeResponseText = text;
          }

          if (activeResponseText && activeResponseText->empty()) {
            if (activeAIBubble) {
              scrollLayout->removeWidget(activeAIBubble);
              delete activeAIBubble;
              activeAIBubble = nullptr;
            }
          } else if (activeResponseText) {
            if (activeAIBubble) {
              activeAIBubble->updateText(QString::fromStdString(*activeResponseText));
            }
            this->history.push_back({"assistant", *activeResponseText});
            this->saveCurrentSession();
          }
          isRequestRunning = false;
          appliedCodeThisTurn = false;
          activeAIBubble = nullptr;
          activeResponseText = nullptr;
          this->enableInput(true);
          this->updateComposerActionButton();
          this->scrollArea->verticalScrollBar()->setValue(
            this->scrollArea->verticalScrollBar()->maximum());
        };

        if (needRender) {
          MainWindow *mw = nullptr;
          for (auto *win : scadApp->windowManager.getWindows()) {
            mw = win;
            break;
          }
          if (mw) {
            mw->startAIFullRender([finishTurn](const AIRenderResult&) { finishTurn(); });
            return;
          }
        }
        finishTurn();
      });
    });
}

void ChatWidget::stopActiveRequest(bool keepPartialAssistant)
{
  if (!isRequestRunning) return;
  aiService->cancelPendingRequests();
  pendingPreviewRender = false;
  // Break a synchronous render wait in executeTool(), if one is active.
  if (activeRenderLoop) {
    activeRenderLoop->quit();
  }
  for (auto *win : scadApp->windowManager.getWindows()) {
    win->cancelAIFullRenderCallback();
    break;
  }
  if (activeAIBubble) {
    std::string stop_msg = activeResponseText ? *activeResponseText : "";
    if (stop_msg.empty() || !keepPartialAssistant) {
      scrollLayout->removeWidget(activeAIBubble);
      delete activeAIBubble;
    } else {
      stop_msg += "\n\n*[Request Stopped by User]*";
      activeAIBubble->updateText(QString::fromStdString(stop_msg));
      if (activeResponseText && !activeResponseText->empty()) {
        history.push_back({"assistant", *activeResponseText});
      }
    }
  }
  isRequestRunning = false;
  pendingPreviewRender = false;
  activeAIBubble = nullptr;
  activeResponseText = nullptr;
  enableInput(true);
  updateComposerActionButton();
}

void ChatWidget::onEditMessage(int historyIndex)
{
  if (historyIndex < 0 || historyIndex >= static_cast<int>(history.size())) return;
  if (history[static_cast<size_t>(historyIndex)].role != "user") return;

  if (isRequestRunning) {
    stopActiveRequest(false);
  }

  const ChatMessage edited = history[static_cast<size_t>(historyIndex)];
  history.resize(static_cast<size_t>(historyIndex));
  rebuildMessageWidgets();

  clearPendingImages();
  inputField->setPlainText(QString::fromStdString(edited.content));
  for (const auto& url : edited.images) {
    const QImage image = loadImageFromDataUrl(QString::fromStdString(url));
    if (!image.isNull()) {
      addPendingImage(image);
    }
  }

  inputField->setFocus();
  inputField->moveCursor(QTextCursor::End);
  updateComposerActionButton();
  saveCurrentSession();
}

void ChatWidget::setUserEditButtonsEnabled(bool enabled)
{
  for (int i = 0; i < scrollLayout->count(); ++i) {
    QLayoutItem *item = scrollLayout->itemAt(i);
    if (!item || !item->widget()) continue;
    if (auto *bubble = qobject_cast<MessageBubble *>(item->widget())) {
      bubble->setEditEnabled(enabled);
    }
  }
}

void ChatWidget::startNewResponseTurn()
{
  appliedCodeThisTurn = false;
  activeResponseText = std::make_shared<std::string>();
  activeAIBubble = addMessage(_("Thinking..."), false);
}

MessageBubble *ChatWidget::addMessage(const QString& text, bool isUser, const QList<QImage>& images,
                                      int historyIndex)
{
  MessageBubble *bubble = new MessageBubble(text, isUser, scrollAreaWidgetContents);
  if (!images.isEmpty()) {
    bubble->setImages(images);
  }
  if (isUser && historyIndex >= 0) {
    bubble->setHistoryIndex(historyIndex);
    connect(bubble, &MessageBubble::editRequested, this, &ChatWidget::onEditMessage);
  }
  bubble->setEditEnabled(!isRequestRunning);
  scrollLayout->insertWidget(scrollLayout->count() - 1, bubble);

  // Auto-scroll to bottom after layout calculation
  QTimer::singleShot(50, this, [this]() {
    this->scrollArea->verticalScrollBar()->setValue(this->scrollArea->verticalScrollBar()->maximum());
  });

  return bubble;
}

void ChatWidget::onClearPressed()
{
  const auto result = QMessageBox::question(
    this, _("Clear chat"), _("Clear the current chat? This cannot be undone."),
    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (result != QMessageBox::Yes) {
    return;
  }

  if (!history.empty()) {
    saveCurrentSession();
  }

  clearMessageWidgets();
  history.clear();
  clearPendingImages();
  currentSessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);

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
  activeAIBubble = nullptr;
  activeResponseText = nullptr;

  for (size_t i = 0; i < history.size(); ++i) {
    const auto& msg = history[i];
    if (msg.role == "user") {
      QList<QImage> images;
      for (const auto& url : msg.images) {
        const QImage image = loadImageFromDataUrl(QString::fromStdString(url));
        if (!image.isNull()) images.append(image);
      }
      const QString content = QString::fromStdString(msg.content);
      const QString display =
        content.isEmpty() && !images.isEmpty() ? tr("(Image attached)") : content;
      addMessage(display, true, images, static_cast<int>(i));
    } else if (msg.role == "assistant" && !msg.content.empty()) {
      addMessage(QString::fromStdString(msg.content), false);
    }
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
    if (msg.content.empty() && msg.images.empty()) continue;
    QJsonObject obj;
    obj.insert(QStringLiteral("role"), QString::fromStdString(msg.role));
    obj.insert(QStringLiteral("content"), QString::fromStdString(msg.content));
    if (!msg.images.empty()) {
      QJsonArray images;
      for (const auto& url : msg.images) {
        images.append(QString::fromStdString(url));
      }
      obj.insert(QStringLiteral("images"), images);
    }
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
      const QJsonArray images = msgObj.value(QStringLiteral("images")).toArray();
      for (const QJsonValue& imageVal : images) {
        const QString url = imageVal.toString();
        if (!url.isEmpty()) {
          msg.images.push_back(url.toStdString());
        }
      }
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

void ChatWidget::onSettingsPressed()
{
  AIApiKeyDialog::prompt(this);

  // The MCP enable checkbox may have changed inside the dialog — reconcile the
  // local bridge so the badge reflects the live state.
  if (AIFreeAgents::mcpEnabled()) {
    if (!OpenSCADAiBridge::instance().isRunning()) {
      OpenSCADAiBridge::instance().setDesiredPort(0);
      OpenSCADAiBridge::instance().start();
    }
  } else if (OpenSCADAiBridge::instance().isRunning()) {
    OpenSCADAiBridge::instance().stop();
  }

  updateAgentButton();
  updateMcpBadge();
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
  if (clearChatButton) clearChatButton->setVisible(true);
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
  QDockWidget *dock = parentDock();
  // If expand was requested but the dock is already marked expanded while still
  // hidden (e.g. closed via Window menu), fall through and force it visible.
  const bool alreadyExpandedButHidden =
    !collapsed && !panelCollapsed && dock && !dock->isVisible();
  if (panelCollapsed == collapsed && !alreadyExpandedButHidden) return;
  panelCollapsed = collapsed;

  if (collapsed) {
    if (dock) {
      expandedDockWidth = qMax(280, dock->width());
      dock->hide();
    }
  } else {
    restoreExpandedChrome();
    if (dock) {
      const int targetW = qMax(280, expandedDockWidth);
      dock->setMinimumWidth(220);
      dock->setMaximumWidth(QWIDGETSIZE_MAX);
      auto *mw = qobject_cast<QMainWindow *>(dock->window());
      if (!mw) mw = qobject_cast<QMainWindow *>(dock->parentWidget());
      if (mw) {
        // Closed docks can leave the right area empty; re-assert placement.
        mw->addDockWidget(Qt::RightDockWidgetArea, dock);
      }
      dock->setVisible(true);
      dock->show();
      dock->raise();
      if (mw) {
        mw->resizeDocks({dock}, {targetW}, Qt::Horizontal);
        // Qt often applies dock sizes only after the next layout pass.
        QTimer::singleShot(0, dock, [mw, dock, targetW]() {
          if (!mw || !dock || !dock->isVisible()) return;
          mw->resizeDocks({dock}, {targetW}, Qt::Horizontal);
        });
      }
    }
  }

  emit collapsedChanged(collapsed);
}

void ChatWidget::onTogglePanelPressed()
{
  QDockWidget *dock = parentDock();
  const bool currentlyHidden = panelCollapsed || (dock && !dock->isVisible());
  setCollapsed(!currentlyHidden);
}

void ChatWidget::enableInput(bool enabled)
{
  inputField->setEnabled(true);  // keep editable; Stop uses the circular action button
  if (attachButton) attachButton->setEnabled(enabled || !isRequestRunning);
  if (agentButton) agentButton->setEnabled(enabled || !isRequestRunning);
  if (historyButton) historyButton->setEnabled(enabled || !isRequestRunning);
  if (clearChatButton) clearChatButton->setEnabled(enabled && !isRequestRunning);
  setUserEditButtonsEnabled(enabled && !isRequestRunning);
  if (enabled) {
    inputField->setFocus();
  }
  updateComposerActionButton();
}

void ChatWidget::updateAgentButton()
{
  if (!agentButton) return;

  const QString profile = QString::fromStdString(AIFreeAgents::activeProfileName());
  QString label = QString::fromStdString(AIFreeAgents::activeModelName());
  if (label.isEmpty()) {
    label = profile.isEmpty() ? tr("Model") : profile;
  }
  if (label.size() > 22) {
    label = label.left(20) + QStringLiteral("…");
  }
  agentButton->setText(label);
  agentButton->setIcon(QIcon::fromTheme(QStringLiteral("chokusen-agent")));
  agentButton->setIconSize(QSize(15, 15));
  if (!profile.isEmpty() && profile != label) {
    agentButton->setToolTip(
      tr("Model: %1\nProfile: %2\nClick to open AI Settings").arg(label, profile));
  } else {
    agentButton->setToolTip(tr("Model: %1\nClick to open AI Settings").arg(label));
  }
}

bool ChatWidget::ensureActiveProfileApiKey()
{
  const std::string profileName = AIFreeAgents::activeProfileName();
  if (profileName.empty()) {
    onSettingsPressed();
    return !AIFreeAgents::activeProfileName().empty() &&
           !AIFreeAgents::readProfileApiKey(AIFreeAgents::activeProfileName()).empty();
  }
  if (!AIFreeAgents::requiresApiKey(profileName)) return true;

  if (!AIFreeAgents::readProfileApiKey(profileName).empty()) return true;

  onSettingsPressed();
  return !AIFreeAgents::readProfileApiKey(profileName).empty();
}

void ChatWidget::updateComposerActionButton()
{
  const bool dark = isDarkTheme();
  sendButton->setText(QString());
  // Force icon refresh so Qt picks up send ↔ stop swap immediately.
  sendButton->setIcon(QIcon());
  sendButton->setIcon(makeCircularSendIcon(isRequestRunning, dark));
  sendButton->setIconSize(QSize(20, 20));
  sendButton->setToolTip(isRequestRunning ? _("Stop") : _("Send"));
  // Keep Stop clickable while running; otherwise require text or images.
  const bool canSend =
    isRequestRunning || !inputField->toPlainText().trimmed().isEmpty() || !pendingImages.isEmpty();
  sendButton->setEnabled(canSend);
  if (attachButton) {
    attachButton->setEnabled(!isRequestRunning && pendingImages.size() < kMaxAttachments);
  }
  if (agentButton) {
    agentButton->setEnabled(!isRequestRunning);
  }
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

  clearChatButton =
    makeHeaderIconButton("headerClearButton", QIcon::fromTheme(QStringLiteral("chokusen-recycle")),
                         _("Clear chat"), headerWidget);
  historyButton =
    makeHeaderIconButton("headerHistoryButton", QIcon::fromTheme(QStringLiteral("chokusen-history")),
                         _("Chat history"), headerWidget);
  layoutButton = makeHeaderIconButton("headerLayoutButton", makeSidebarIcon(dark), _("Hide AI chat"),
                                      headerWidget);

  headerLayout->addWidget(clearChatButton);
  headerLayout->addWidget(historyButton);
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

  attachmentStrip = new QWidget(composer);
  attachmentStrip->setObjectName(QStringLiteral("attachmentStrip"));
  attachmentStripLayout = new QHBoxLayout(attachmentStrip);
  attachmentStripLayout->setContentsMargins(0, 2, 0, 2);
  attachmentStripLayout->setSpacing(6);
  attachmentStrip->hide();
  composerLayout->addWidget(attachmentStrip);

  inputField->setParent(composer);
  inputField->setPlaceholderText(_("Describe the model you want to build…"));
  inputField->setMaximumHeight(80);
  inputField->setMinimumHeight(28);
  composerLayout->addWidget(inputField);

  auto *toolbar = new QWidget(composer);
  auto *toolbarLayout = new QHBoxLayout(toolbar);
  toolbarLayout->setContentsMargins(0, 0, 0, 0);
  toolbarLayout->setSpacing(6);

  agentButton = new QPushButton(toolbar);
  agentButton->setObjectName(QStringLiteral("agentButton"));
  agentButton->setIcon(QIcon::fromTheme(QStringLiteral("chokusen-agent")));
  agentButton->setIconSize(QSize(15, 15));
  agentButton->setFlat(true);
  agentButton->setCursor(Qt::PointingHandCursor);
  agentButton->setFocusPolicy(Qt::NoFocus);
  agentButton->setToolTip(_("Open AI Settings for the active profile"));
  connect(agentButton, &QPushButton::clicked, this, &ChatWidget::onSettingsPressed);
  toolbarLayout->addWidget(agentButton);

  mcpBadge = new QPushButton(QStringLiteral("MCP Server"), toolbar);
  mcpBadge->setObjectName(QStringLiteral("mcpBadge"));
  mcpBadge->setFlat(true);
  mcpBadge->setCursor(Qt::PointingHandCursor);
  mcpBadge->setFocusPolicy(Qt::NoFocus);
  mcpBadge->setIconSize(QSize(9, 9));
  connect(mcpBadge, &QPushButton::clicked, this, &ChatWidget::onSettingsPressed);
  toolbarLayout->addWidget(mcpBadge);

  toolbarLayout->addStretch(1);

  attachButton = new QPushButton(toolbar);
  attachButton->setObjectName("attachButton");
  attachButton->setIcon(QIcon::fromTheme(QStringLiteral("chokusen-paper-clip")));
  attachButton->setIconSize(QSize(18, 18));
  attachButton->setFixedSize(24, 24);
  attachButton->setFlat(true);
  attachButton->setCursor(Qt::PointingHandCursor);
  attachButton->setToolTip(_("Attach image (or paste from clipboard)"));
  toolbarLayout->addWidget(attachButton);

  sendButton->setParent(toolbar);
  sendButton->setObjectName("sendButton");
  sendButton->setText(QString());
  sendButton->setFixedSize(24, 24);
  sendButton->setMinimumSize(24, 24);
  sendButton->setMaximumSize(24, 24);
  sendButton->setCursor(Qt::PointingHandCursor);
  sendButton->setFlat(true);
  toolbarLayout->addWidget(sendButton);

  composerLayout->addWidget(toolbar);
  inputLayout->addWidget(composer);

  connect(inputField, &QPlainTextEdit::textChanged, this, &ChatWidget::updateComposerActionButton);
  connect(inputField, &ChatInputEdit::imagePasted, this, &ChatWidget::onImagePasted);
  connect(attachButton, &QPushButton::clicked, this, &ChatWidget::onAttachPressed);
  updateAgentButton();
  updateMcpBadge();
  updateComposerActionButton();
}

void ChatWidget::updateMcpBadge()
{
  if (!mcpBadge) return;

  const bool dark = isDarkTheme();
  const bool enabled = AIFreeAgents::mcpEnabled();
  const bool running = OpenSCADAiBridge::instance().isRunning();
  const bool live = enabled && running;

  const QColor onDot = dark ? QColor("#3fb950") : QColor("#2ea043");
  const QColor offDot = dark ? QColor("#6e7681") : QColor("#c2c2c2");
  mcpBadge->setIcon(makeStatusDotIcon(live ? onDot : offDot));
  mcpBadge->setFixedHeight(20);

  if (live) {
    mcpBadge->setToolTip(
      tr("MCP bridge is ON — OpenSCAD tools are exposed to AI agents.\nClick to open AI Settings."));
  } else if (enabled) {
    mcpBadge->setToolTip(
      tr("MCP is enabled but the local bridge is not running yet.\nClick to open AI Settings."));
  } else {
    mcpBadge->setToolTip(
      tr("MCP bridge is OFF — agents fall back to plain chat.\nClick to open AI Settings."));
  }

  // Refined pill: fully rounded, soft state-tinted fill, no harsh border.
  QString bg, hover, text;
  if (live) {
    bg = dark ? QStringLiteral("rgba(63,185,80,0.16)") : QStringLiteral("rgba(46,160,67,0.12)");
    hover = dark ? QStringLiteral("rgba(63,185,80,0.26)") : QStringLiteral("rgba(46,160,67,0.20)");
    text = dark ? QStringLiteral("#57d364") : QStringLiteral("#1a7f37");
  } else {
    bg = dark ? QStringLiteral("rgba(255,255,255,0.06)") : QStringLiteral("rgba(0,0,0,0.05)");
    hover = dark ? QStringLiteral("rgba(255,255,255,0.11)") : QStringLiteral("rgba(0,0,0,0.09)");
    text = dark ? QStringLiteral("#8b949e") : QStringLiteral("#8a8a8a");
  }

  mcpBadge->setStyleSheet(QStringLiteral(R"(
    QPushButton#mcpBadge {
      border: none;
      border-radius: 10px;
      padding: 0px 9px 0px 7px;
      color: %1;
      font-size: 10px;
      font-weight: 700;
      letter-spacing: 0.6px;
      background: %2;
      text-align: center;
    }
    QPushButton#mcpBadge:hover { background: %3; }
    QPushButton#mcpBadge:pressed { padding-top: 1px; }
  )")
                            .arg(text, bg, hover));
}

bool ChatWidget::isDarkTheme() const
{
  return isDarkMode();
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
  if (agentButton) {
    agentButton->setIcon(QIcon::fromTheme(QStringLiteral("chokusen-agent")));
  }
  if (historyButton) historyButton->setIcon(QIcon::fromTheme(QStringLiteral("chokusen-history")));
  if (clearChatButton) clearChatButton->setIcon(QIcon::fromTheme(QStringLiteral("chokusen-recycle")));
  if (layoutButton) layoutButton->setIcon(makeSidebarIcon(dark));
  updateAgentButton();
  updateMcpBadge();
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
      QPushButton#headerHistoryButton, QPushButton#headerClearButton,
      QPushButton#headerLayoutButton {
        background: transparent;
        border: none;
        border-radius: 4px;
      }
      QPushButton#headerHistoryButton:hover, QPushButton#headerClearButton:hover,
      QPushButton#headerLayoutButton:hover {
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
      QFrame#attachmentChip {
        background: #2a2d2e;
        border: 1px solid #3c3c3c;
        border-radius: 8px;
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
      QPushButton#agentButton {
        background: transparent;
        border: none;
        border-radius: 8px;
        color: #c8c8c8;
        font-size: 11.5px;
        font-weight: 400;
        padding: 4px 8px 4px 6px;
        text-align: left;
      }
      QPushButton#agentButton:hover, QPushButton#agentButton:pressed {
        background: #2a2d2e;
      }
      QPushButton#agentButton::menu-indicator {
        image: none;
        width: 0px;
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
      QPushButton#headerHistoryButton, QPushButton#headerClearButton,
      QPushButton#headerLayoutButton {
        background: transparent;
        border: none;
        border-radius: 4px;
      }
      QPushButton#headerHistoryButton:hover, QPushButton#headerClearButton:hover,
      QPushButton#headerLayoutButton:hover {
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
      QFrame#attachmentChip {
        background: #ffffff;
        border: 1px solid #e0e0e0;
        border-radius: 8px;
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
      QPushButton#agentButton {
        background: transparent;
        border: none;
        border-radius: 8px;
        color: #3a3a3a;
        font-size: 11.5px;
        font-weight: 400;
        padding: 4px 8px 4px 6px;
        text-align: left;
      }
      QPushButton#agentButton:hover, QPushButton#agentButton:pressed {
        background: #ececec;
      }
      QPushButton#agentButton::menu-indicator {
        image: none;
        width: 0px;
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
    // Defer full F6 render until the assistant turn finishes (once per turn).
    pendingPreviewRender = true;
  }
}

void ChatWidget::flushPendingPreview()
{
  if (!pendingPreviewRender) return;
  pendingPreviewRender = false;
  MainWindow *mw = nullptr;
  for (auto *win : scadApp->windowManager.getWindows()) {
    mw = win;
    break;
  }
  if (mw) {
    mw->startAIFullRender([](const AIRenderResult&) {});
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
    detail = QString::fromStdString("Tool: set_editor_code\nResult: " + result);
  } else if (name == "trigger_preview") {
    summary = tr("Queued full render");
    detail = QString::fromStdString("Tool: trigger_preview\nResult: " + result);
  } else if (name == "get_preview_image") {
    summary = tr("Captured 3D preview");
    detail = result.rfind("IMAGE_PNG_BASE64:", 0) == 0
               ? tr("Tool: get_preview_image\nResult: PNG image (%1 bytes base64).")
                   .arg(static_cast<int>(result.size()))
               : QString::fromStdString("Tool: get_preview_image\nResult: " + result);
  } else if (name == "get_skill") {
    summary = tr("Loaded CAD skill");
    detail = tr("Tool: get_skill\nResult: %1 chars.").arg(static_cast<int>(result.size()));
  } else {
    summary = tr("Executed tool: %1").arg(QString::fromStdString(name));
    detail = QString::fromStdString("Tool: " + name + "\nResult: " +
                                    (result.size() > 800 ? result.substr(0, 800) + "…" : result));
  }

  if (activeAIBubble) {
    activeAIBubble->addToolCall(summary, detail);
  }

  scrollArea->verticalScrollBar()->setValue(scrollArea->verticalScrollBar()->maximum());
}

std::string ChatWidget::formatRenderResult(const AIRenderResult& rr) const
{
  std::ostringstream os;
  auto appendFacts = [&]() {
    if (rr.hasBoundingBox) {
      os << " Bounding box (mm): " << rr.bboxSize[0] << " x " << rr.bboxSize[1] << " x "
         << rr.bboxSize[2] << ".";
    }
    if (rr.dimension == 3) {
      os << " Facets: " << rr.facets << ".";
    }
  };

  if (rr.success) {
    os << "Success: code applied and the F6 render completed with no errors.";
    appendFacts();
    if (rr.warningCount > 0) {
      os << " Warnings: " << rr.warningCount << ".";
      if (!rr.log.empty()) os << "\n" << rr.log;
    }
    os << "\nThe model is on screen. Do not call set_editor_code again unless the user asks "
          "for a change.";
    return os.str();
  }

  if (rr.empty && rr.errorCount == 0) {
    os << "[render-error] Code applied but the render produced NO top-level geometry. Ensure "
          "there is a top-level call to your assembly (or a root union), and that a difference() "
          "did not subtract everything. Fix the code and call set_editor_code again.";
    if (!rr.log.empty()) os << "\n" << rr.log;
    return os.str();
  }

  os << "[render-error] Code applied but the F6 render reported " << rr.errorCount
     << " error(s)";
  if (rr.warningCount > 0) os << " and " << rr.warningCount << " warning(s)";
  os << ". Fix the OpenSCAD code and call set_editor_code again with the corrected full script.";
  if (!rr.log.empty()) os << "\n" << rr.log;
  return os.str();
}

std::string ChatWidget::renderAppliedCodeAndDescribe()
{
  MainWindow *mw = nullptr;
  for (auto *win : scadApp->windowManager.getWindows()) {
    mw = win;
    break;
  }
  if (!mw) {
    // No window available: fall back to the deferred end-of-turn render (no feedback).
    return "Success: Code applied to the editor. A full render (F6) will run when the reply "
           "finishes.";
  }

  // We render synchronously here and wait, so avoid a second render at end of turn.
  pendingPreviewRender = false;

  AIRenderResult rr;
  bool got = false;
  QEventLoop loop;
  activeRenderLoop = &loop;
  mw->startAIFullRender([this, &rr, &got, &loop](const AIRenderResult& r) {
    rr = r;
    got = true;
    if (activeRenderLoop == &loop) {
      activeRenderLoop = nullptr;
    }
    loop.quit();
  });
  if (!got) {
    loop.exec();
  }
  activeRenderLoop = nullptr;

  if (!got) {
    // Loop was quit by Stop before the render finished.
    return "Cancelled: the render was interrupted by the user.";
  }

  cacheRenderResult(rr);

  if (activeAIBubble) {
    activeAIBubble->updateText(rr.success ? tr("Applied code to the editor…")
                                          : tr("Render reported problems…"));
  }
  return formatRenderResult(rr);
}

void ChatWidget::cacheRenderResult(const AIRenderResult& rr)
{
  lastRenderValid = true;
  lastRenderSuccess = rr.success;
  lastRenderEmpty = rr.empty;
  lastRenderErrors = rr.errorCount;
  lastRenderWarnings = rr.warningCount;
  lastRenderHasBBox = rr.hasBoundingBox;
  lastRenderBBox[0] = rr.bboxSize[0];
  lastRenderBBox[1] = rr.bboxSize[1];
  lastRenderBBox[2] = rr.bboxSize[2];
  lastRenderFacets = rr.facets;
  lastRenderLog = rr.log;
}

std::string ChatWidget::formatModelInfo() const
{
  std::ostringstream os;
  if (!lastRenderValid) {
    MainWindow *mw = nullptr;
    for (auto *win : scadApp->windowManager.getWindows()) {
      mw = win;
      break;
    }
    if (mw) {
      const AIRenderResult rr = mw->collectAIRenderResult();
      os << "success=" << (rr.success ? "true" : "false");
      os << " empty=" << (rr.empty ? "true" : "false");
      os << " errors=" << rr.errorCount << " warnings=" << rr.warningCount;
      if (rr.hasBoundingBox) {
        os << " bbox_mm=[" << rr.bboxSize[0] << ", " << rr.bboxSize[1] << ", " << rr.bboxSize[2]
           << "]";
      }
      os << " facets=" << rr.facets;
      if (!rr.log.empty()) os << "\nlog:\n" << rr.log;
      return os.str();
    }
    return "No render result yet. Call set_editor_code or trigger_preview first.";
  }
  os << "success=" << (lastRenderSuccess ? "true" : "false");
  os << " empty=" << (lastRenderEmpty ? "true" : "false");
  os << " errors=" << lastRenderErrors << " warnings=" << lastRenderWarnings;
  if (lastRenderHasBBox) {
    os << " bbox_mm=[" << lastRenderBBox[0] << ", " << lastRenderBBox[1] << ", " << lastRenderBBox[2]
       << "]";
  }
  os << " facets=" << lastRenderFacets;
  if (!lastRenderLog.empty()) os << "\nlog:\n" << lastRenderLog;
  return os.str();
}

std::string ChatWidget::listSkillNames() const
{
  try {
    const auto dir = PlatformUtils::resourcePath("skills");
    if (dir.empty() || !std::filesystem::is_directory(dir)) {
      return "No skills directory found.";
    }
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
      if (!entry.is_directory()) continue;
      const auto skillMd = entry.path() / "SKILL.md";
      if (!std::filesystem::exists(skillMd)) continue;
      nlohmann::json item = nlohmann::json::object();
      item["name"] = entry.path().filename().string();
      item["has_compact"] = std::filesystem::exists(entry.path() / "SKILL.compact.md");
      arr.push_back(item);
    }
    return arr.dump(2);
  } catch (const std::exception& e) {
    return std::string("Error listing skills: ") + e.what();
  }
}

std::string ChatWidget::loadSkillFile(const std::string& name, bool compact) const
{
  if (name.empty() || name.find("..") != std::string::npos || name.find('/') != std::string::npos ||
      name.find('\\') != std::string::npos) {
    return "Error: invalid skill name.";
  }
  try {
    const auto dir = PlatformUtils::resourcePath("skills") / name;
    const auto file = dir / (compact ? "SKILL.compact.md" : "SKILL.md");
    if (!std::filesystem::exists(file)) {
      return "Error: skill file not found: " + file.string();
    }
    std::ifstream in(file);
    if (!in) return "Error: cannot read skill file.";
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
  } catch (const std::exception& e) {
    return std::string("Error loading skill: ") + e.what();
  }
}

std::string ChatWidget::capturePreviewImageBase64(int maxWidth) const
{
  MainWindow *mw = nullptr;
  for (auto *win : scadApp->windowManager.getWindows()) {
    mw = win;
    break;
  }
  if (!mw || !mw->qglview) {
    return "Error: No 3D view available.";
  }
  QImage img = mw->qglview->grabFrame();
  if (img.isNull()) {
    return "Error: Failed to capture preview image.";
  }
  if (maxWidth > 0 && img.width() > maxWidth) {
    img = img.scaledToWidth(maxWidth, Qt::SmoothTransformation);
  }
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  if (!img.save(&buffer, "PNG")) {
    return "Error: Failed to encode PNG.";
  }
  return std::string("IMAGE_PNG_BASE64:") + bytes.toBase64().toStdString();
}

std::string ChatWidget::executeTool(const std::string& name, const std::string& arguments_json)
{
  if (activeResponseText) {
    activeResponseText->clear();
  }
  if (activeAIBubble) {
    activeAIBubble->updateText(tr("Working…"));
  }

  MainWindow *mw = nullptr;
  for (auto *win : scadApp->windowManager.getWindows()) {
    mw = win;
    break;
  }

  nlohmann::json args = nlohmann::json::object();
  if (!arguments_json.empty()) {
    try {
      args = nlohmann::json::parse(arguments_json);
      if (!args.is_object()) args = nlohmann::json::object();
    } catch (...) {
      args = nlohmann::json::object();
    }
  }

  std::string result_val;

  if (name == "get_editor_code") {
    if (mw && mw->activeEditor) {
      result_val = mw->activeEditor->toPlainText().toStdString();
    } else {
      result_val = "Error: No active editor found.";
    }
  } else if (name == "set_editor_code") {
    if (!args.contains("code")) {
      return "Error: Missing required argument 'code'.";
    }
    std::string code;
    if (args["code"].is_string()) {
      code = args["code"].get<std::string>();
    } else if (args["code"].is_object() && args["code"].contains("value") &&
               args["code"]["value"].is_string()) {
      code = args["code"]["value"].get<std::string>();
    } else {
      code = args["code"].dump();
    }
    this->applyCodeChange(code);
    appliedCodeThisTurn = true;
    if (activeAIBubble) activeAIBubble->updateText(tr("Rendering…"));
    result_val = renderAppliedCodeAndDescribe();
  } else if (name == "trigger_preview") {
    if (mw) {
      if (activeAIBubble) activeAIBubble->updateText(tr("Rendering…"));
      result_val = renderAppliedCodeAndDescribe();
    } else {
      pendingPreviewRender = true;
      result_val = "Success: Full render queued; it will run once when the reply finishes.";
    }
  } else if (name == "get_model_info") {
    result_val = formatModelInfo();
  } else if (name == "get_preview_image") {
    int maxW = 1024;
    if (args.contains("max_width") && args["max_width"].is_number_integer()) {
      maxW = args["max_width"].get<int>();
    }
    result_val = capturePreviewImageBase64(maxW);
  } else if (name == "get_console_log") {
    int maxChars = 4000;
    if (args.contains("max_chars") && args["max_chars"].is_number_integer()) {
      maxChars = std::max(500, args["max_chars"].get<int>());
    }
    if (mw && mw->console) {
      QString text = mw->console->document()->toPlainText();
      if (text.size() > maxChars) {
        text = text.right(maxChars);
        result_val = "…\n" + text.toStdString();
      } else {
        result_val = text.toStdString();
      }
      if (result_val.empty()) result_val = "(console empty)";
    } else {
      result_val = "Error: Console not available.";
    }
  } else if (name == "list_skills") {
    result_val = listSkillNames();
  } else if (name == "get_skill") {
    const std::string skillName = args.value("name", "openscad-cad");
    const bool compact = args.value("compact", false);
    result_val = loadSkillFile(skillName, compact);
  } else if (name == "get_cheatsheet") {
    result_val =
      "# OpenSCAD quick cheatsheet (mm)\n"
      "- Units: millimeters. Origin often centered; XY base, +Z up.\n"
      "- Curved solids: set `$fn = 32`..`64`.\n"
      "- Modifiers (`translate`/`rotate`/`scale`/`mirror`/`color`/`hull`) wrap the NEXT child — "
      "never assign a modifier to a variable.\n"
      "- End statements with `;`. Do NOT put `;` after `module name() { ... }`.\n"
      "- Through-hole in plate thickness t: cylinder height t+2 protruding both faces "
      "(avoid coincident faces in difference()).\n"
      "- Prefer: parameters at top → one module per part → assemble with "
      "union()/difference()/hull().\n"
      "- Common: cube([x,y,z], center=true); cylinder(h=h, d=d, center=true); "
      "sphere(d=d); linear_extrude(height=h) polygon(...); rotate_extrude() ...\n"
      "- Fillet/rounding last; reduce radius if boolean/minkowski fails.\n"
      "- Always apply scripts with set_editor_code (full file). Then verify with "
      "get_model_info / get_preview_image.\n"
      "- For full workflow call get_skill name=openscad-cad (or compact=true).";
  } else if (name == "get_camera_info") {
    if (mw && mw->qglview) {
      const auto& cam = mw->qglview->cam;
      nlohmann::json j = nlohmann::json::object();
      j["projection"] =
        (cam.projection == Camera::ProjectionType::ORTHOGONAL) ? "ortho" : "perspective";
      j["fov"] = cam.fov;
      j["distance"] = cam.viewer_distance;
      j["object_translation"] = {cam.object_trans.x(), cam.object_trans.y(), cam.object_trans.z()};
      j["object_rotation"] = {cam.object_rot.x(), cam.object_rot.y(), cam.object_rot.z()};
      result_val = j.dump(2);
    } else {
      result_val = "Error: Camera not available.";
    }
  } else if (name == "pan_view") {
    if (!(mw && mw->qglview)) {
      result_val = "Error: 3D view not available.";
    } else {
      auto readNum = [&](const char *key, double fallback = 0.0) -> double {
        if (!args.contains(key)) return fallback;
        const auto& v = args[key];
        if (v.is_number()) return v.get<double>();
        if (v.is_string()) {
          try {
            return std::stod(v.get<std::string>());
          } catch (...) {
            return fallback;
          }
        }
        return fallback;
      };
      const double dx = readNum("dx");
      const double dy = readNum("dy");
      const double dz = readNum("dz");
      if (dx == 0.0 && dy == 0.0 && dz == 0.0) {
        result_val =
          "Error: pan_view needs at least one of dx, dy, dz (mm in the view plane). "
          "Example: {\"dx\": 20, \"dy\": -10} — positive dx moves the model right on screen, "
          "positive dy moves it down (grab-hand drag).";
      } else {
        // Match middle-mouse PAN_LR_UD: screen X → view X, screen Y → −view Z.
        // Optional dz uses the fore/back axis (view Y).
        mw->qglview->translate(dx, -dz, -dy, true);
        const auto& cam = mw->qglview->cam;
        nlohmann::json j = nlohmann::json::object();
        j["ok"] = true;
        j["applied_mm"] = {{"dx", dx}, {"dy", dy}, {"dz", dz}};
        j["object_translation"] = {cam.object_trans.x(), cam.object_trans.y(), cam.object_trans.z()};
        j["hint"] = "Call get_preview_image to see the new framing.";
        result_val = j.dump(2);
      }
    }
  } else if (name == "zoom_in" || name == "zoom_out") {
    if (!(mw && mw->qglview)) {
      result_val = "Error: 3D view not available.";
    } else {
      int steps = 1;
      if (args.contains("steps")) {
        if (args["steps"].is_number_integer()) steps = args["steps"].get<int>();
        else if (args["steps"].is_number()) steps = static_cast<int>(args["steps"].get<double>());
        else if (args["steps"].is_string()) {
          try {
            steps = std::stoi(args["steps"].get<std::string>());
          } catch (...) {
            steps = 1;
          }
        }
      }
      steps = std::clamp(steps, 1, 20);
      const int notch = (name == "zoom_in") ? 120 : -120;
      for (int i = 0; i < steps; ++i) {
        mw->qglview->zoom(notch, true);
      }
      nlohmann::json j = nlohmann::json::object();
      j["ok"] = true;
      j["action"] = name;
      j["steps"] = steps;
      j["distance"] = mw->qglview->cam.viewer_distance;
      j["hint"] = "Call get_preview_image to see the new framing.";
      result_val = j.dump(2);
    }
  } else if (name == "zoom_100") {
    if (!(mw && mw->qglview)) {
      result_val = "Error: 3D view not available.";
    } else {
      constexpr double kDefaultDistance = 140.0;
      mw->qglview->zoom(kDefaultDistance, false);
      nlohmann::json j = nlohmann::json::object();
      j["ok"] = true;
      j["action"] = "zoom_100";
      j["distance"] = mw->qglview->cam.viewer_distance;
      j["hint"] = "Zoom reset to default distance (pan/rotation kept). "
                  "Use view_all to frame geometry, or get_preview_image to verify.";
      result_val = j.dump(2);
    }
  } else if (name == "view_all") {
    if (mw && mw->qglview) {
      mw->qglview->viewAll();
      mw->qglview->update();
      result_val = "Success: view framed to show all geometry.";
    } else {
      result_val = "Error: 3D view not available.";
    }
  } else if (name == "reset_view") {
    if (mw && mw->qglview) {
      mw->qglview->resetView();
      mw->qglview->update();
      result_val = "Success: view reset to default.";
    } else {
      result_val = "Error: 3D view not available.";
    }
  } else if (name == "list_tools") {
    result_val =
      "get_editor_code, set_editor_code, trigger_preview, get_model_info, get_preview_image, "
      "get_console_log, list_skills, get_skill, get_cheatsheet, get_camera_info, pan_view, "
      "zoom_in, zoom_out, zoom_100, view_all, reset_view, list_tools\n"
      "Recommended workflow: get_skill(openscad-cad) → get_editor_code (if editing) → "
      "set_editor_code → get_model_info → get_preview_image → pan_view/zoom_*/view_all if needed → "
      "repair if needed.";
  } else {
    result_val = "Error: Unknown tool name '" + name + "'. Call list_tools for the catalog.";
  }

  this->logToolExecution(name, result_val);
  return result_val;
}

void ChatWidget::onAttachPressed()
{
  if (isRequestRunning) return;
  const QStringList files = QFileDialog::getOpenFileNames(
    this, _("Attach images"), QString(),
    _("Images (*.png *.jpg *.jpeg *.webp *.gif *.bmp);;All files (*)"));
  for (const QString& path : files) {
    QImage image(path);
    if (image.isNull()) continue;
    if (!addPendingImage(image)) break;
  }
}

void ChatWidget::onImagePasted(const QImage& image)
{
  if (isRequestRunning) return;
  addPendingImage(image);
}

bool ChatWidget::addPendingImage(const QImage& image)
{
  if (image.isNull()) return false;
  if (pendingImages.size() >= kMaxAttachments) {
    QMessageBox::information(this, _("Attach image"),
                             tr("You can attach up to %1 images per message.").arg(kMaxAttachments));
    return false;
  }
  pendingImages.append(image);
  refreshAttachmentStrip();
  updateComposerActionButton();
  return true;
}

void ChatWidget::clearPendingImages()
{
  pendingImages.clear();
  refreshAttachmentStrip();
  updateComposerActionButton();
}

void ChatWidget::refreshAttachmentStrip()
{
  if (!attachmentStrip || !attachmentStripLayout) return;

  while (QLayoutItem *child = attachmentStripLayout->takeAt(0)) {
    if (child->widget()) delete child->widget();
    delete child;
  }

  if (pendingImages.isEmpty()) {
    attachmentStrip->hide();
    return;
  }

  for (int i = 0; i < pendingImages.size(); ++i) {
    auto *chip = new QFrame(attachmentStrip);
    chip->setObjectName(QStringLiteral("attachmentChip"));
    chip->setFixedHeight(56);
    auto *chipLayout = new QHBoxLayout(chip);
    chipLayout->setContentsMargins(4, 4, 4, 4);
    chipLayout->setSpacing(4);

    auto *thumb = new QLabel(chip);
    thumb->setFixedSize(44, 44);
    thumb->setAlignment(Qt::AlignCenter);
    thumb->setPixmap(QPixmap::fromImage(pendingImages[i])
                       .scaled(44, 44, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    chipLayout->addWidget(thumb);

    auto *removeBtn = new QPushButton(QStringLiteral("×"), chip);
    removeBtn->setFlat(true);
    removeBtn->setFixedSize(18, 18);
    removeBtn->setCursor(Qt::PointingHandCursor);
    removeBtn->setToolTip(_("Remove image"));
    removeBtn->setStyleSheet(
      "QPushButton { border: none; color: #666; font-size: 14px; font-weight: 700; }"
      "QPushButton:hover { color: #111; }");
    const int index = i;
    connect(removeBtn, &QPushButton::clicked, this, [this, index]() {
      if (index < 0 || index >= pendingImages.size()) return;
      pendingImages.removeAt(index);
      refreshAttachmentStrip();
      updateComposerActionButton();
    });
    chipLayout->addWidget(removeBtn, 0, Qt::AlignTop);

    attachmentStripLayout->addWidget(chip);
  }
  attachmentStripLayout->addStretch(1);
  attachmentStrip->show();
}

QString ChatWidget::imageToDataUrl(const QImage& image)
{
  if (image.isNull()) return {};

  QImage scaled = image;
  constexpr int kMaxDim = 1568;
  if (scaled.width() > kMaxDim || scaled.height() > kMaxDim) {
    scaled = scaled.scaled(kMaxDim, kMaxDim, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  }

  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  if (!scaled.save(&buffer, "JPEG", 85)) {
    bytes.clear();
    buffer.open(QIODevice::WriteOnly);
    if (!scaled.save(&buffer, "PNG")) return {};
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());
  }
  return QStringLiteral("data:image/jpeg;base64,") + QString::fromLatin1(bytes.toBase64());
}

QImage ChatWidget::loadImageFromDataUrl(const QString& dataUrl)
{
  const int comma = dataUrl.indexOf(',');
  if (comma < 0) return {};
  const QByteArray bytes = QByteArray::fromBase64(dataUrl.mid(comma + 1).toLatin1());
  QImage image;
  image.loadFromData(bytes);
  return image;
}
