#include "gui/ai/ChatWidget.h"
#include "gui/qtgettext.h"
#include "json/json.hpp"
#include "core/AIFreeAgents.h"
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
#include <QClipboard>
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

constexpr const char *kSavedChatsSettingsKey = "ai/savedChats";
constexpr const char *kComposerAgentSettingsKey = "ai/composerAgent";
constexpr const char *kApiKeyAgentId = "__api_key__";

struct ComposerAgentOption {
  QString id;
  QString title;
  QString subtitle;
};

QList<ComposerAgentOption> composerAgentOptions()
{
  QList<ComposerAgentOption> opts;
  for (const auto& name : AIFreeAgents::freePresetNames()) {
    const QString qname = QString::fromStdString(name);
    QString subtitle = QObject::tr("Free tier");
    if (qname.startsWith(QStringLiteral("Gemini"))) {
      subtitle = QObject::tr("Free · Google AI Studio");
    } else if (qname.startsWith(QStringLiteral("Groq"))) {
      subtitle = QObject::tr("Free · Fast open models");
    } else if (qname.startsWith(QStringLiteral("OpenRouter"))) {
      subtitle = QObject::tr("Free · Auto-picks a model");
    } else if (qname.startsWith(QStringLiteral("Ollama"))) {
      subtitle = QObject::tr("Local · No API key");
    }
    opts.push_back({qname, qname, subtitle});
  }
  return opts;
}

QString composerAgentChipLabel(const QString& agentId)
{
  if (agentId == QString::fromUtf8(kApiKeyAgentId)) {
    return QObject::tr("API Key");
  }
  if (agentId.endsWith(QStringLiteral(" Free"))) {
    return agentId.left(agentId.size() - 5);
  }
  if (agentId == QStringLiteral("Ollama Local")) {
    return QStringLiteral("Ollama");
  }
  return agentId;
}

class AgentPickerRow : public QFrame
{
public:
  AgentPickerRow(const QString& id, const QString& title, const QString& subtitle, bool selected,
                 QWidget *parent = nullptr)
    : QFrame(parent), agentId(id)
  {
    setObjectName(QStringLiteral("agentPickerRow"));
    setProperty("selected", selected);
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(40);

    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(12, 8, 12, 8);
    lay->setSpacing(10);

    auto *textCol = new QVBoxLayout();
    textCol->setContentsMargins(0, 0, 0, 0);
    textCol->setSpacing(1);

    auto *titleLabel = new QLabel(title, this);
    titleLabel->setObjectName(QStringLiteral("agentPickerTitle"));
    titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    auto *subLabel = new QLabel(subtitle, this);
    subLabel->setObjectName(QStringLiteral("agentPickerSubtitle"));
    subLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    textCol->addWidget(titleLabel);
    textCol->addWidget(subLabel);
    lay->addLayout(textCol, 1);

    auto *check = new QLabel(selected ? QStringLiteral("✓") : QString(), this);
    check->setObjectName(QStringLiteral("agentPickerCheck"));
    check->setFixedWidth(16);
    check->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    check->setAttribute(Qt::WA_TransparentForMouseEvents);
    lay->addWidget(check);
  }

  QString agentId;
  std::function<void(const QString&)> onActivated;

protected:
  void mousePressEvent(QMouseEvent *event) override
  {
    if (event->button() == Qt::LeftButton && onActivated) {
      onActivated(agentId);
    }
    QFrame::mousePressEvent(event);
  }

  void enterEvent(QEnterEvent *event) override
  {
    setProperty("hovered", true);
    style()->unpolish(this);
    style()->polish(this);
    QFrame::enterEvent(event);
  }

  void leaveEvent(QEvent *event) override
  {
    setProperty("hovered", false);
    style()->unpolish(this);
    style()->polish(this);
    QFrame::leaveEvent(event);
  }
};

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
  QPalette pal = QApplication::palette();
  return pal.color(QPalette::Window).lightness() < 128;
}

// ChatWidget implementation
ChatWidget::ChatWidget(QWidget *parent) : QWidget(parent)
{
  setupUi(this);

  currentSessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  {
    QSettings settings;
    const QString saved = settings.value(QString::fromUtf8(kComposerAgentSettingsKey)).toString();
    if (saved == QString::fromUtf8(kApiKeyAgentId)) {
      composerAgentId = saved;
    } else {
      bool knownFree = false;
      for (const auto& name : AIFreeAgents::freePresetNames()) {
        if (saved == QString::fromStdString(name)) {
          knownFree = true;
          break;
        }
      }
      composerAgentId = knownFree ? saved : QString::fromUtf8(kApiKeyAgentId);
    }
  }

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

  if (!ensureComposerAgentApiKey()) {
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

  applyComposerAgentOverride();
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
        QString display_err = QString::fromStdString(error_msg);
        if (display_err.contains(QStringLiteral("Free limit reached"), Qt::CaseInsensitive) ||
            display_err.contains(QStringLiteral("rate limited"), Qt::CaseInsensitive) ||
            display_err.contains(QStringLiteral("temporarily limited"), Qt::CaseInsensitive)) {
          display_err = tr("⚠️ Free agent limited\n\n") + display_err;
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
        this->clearComposerAgentOverride();
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
          activeAIBubble = nullptr;
          activeResponseText = nullptr;
          this->clearComposerAgentOverride();
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
            mw->startAIFullRender([finishTurn]() { finishTurn(); });
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
  clearComposerAgentOverride();
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

  const bool isApiKey = (composerAgentId == QString::fromUtf8(kApiKeyAgentId));
  QString label = composerAgentChipLabel(composerAgentId);
  if (label.size() > 16) {
    label = label.left(14) + QStringLiteral("…");
  }
  // Cursor-style chip: icon + name + chevron
  agentButton->setText(label + QStringLiteral("  ▾"));
  agentButton->setIcon(QIcon::fromTheme(QStringLiteral("chokusen-agent")));
  agentButton->setIconSize(QSize(15, 15));
  agentButton->setToolTip(
    isApiKey ? tr("Using the model configured in AI Settings (API Key)")
             : tr("Using free agent: %1").arg(composerAgentId));
}

void ChatWidget::selectComposerAgent(const QString& id)
{
  if (id.isEmpty()) return;
  composerAgentId = id;
  QSettings settings;
  settings.setValue(QString::fromUtf8(kComposerAgentSettingsKey), composerAgentId);
  updateAgentButton();
}

bool ChatWidget::ensureComposerAgentApiKey()
{
  std::string profileName;
  if (composerAgentId == QString::fromUtf8(kApiKeyAgentId)) {
    profileName = AIFreeAgents::activeProfileName();
  } else {
    profileName = composerAgentId.toStdString();
  }
  if (profileName.empty()) return true;
  if (!AIFreeAgents::requiresApiKey(profileName)) return true;

  const std::string existing = AIFreeAgents::readProfileApiKey(profileName);
  if (!existing.empty()) return true;

  const QString title = tr("API key for %1").arg(QString::fromStdString(profileName));
  const QString hint = QString::fromStdString(AIFreeAgents::apiKeySignupHint(profileName));
  const QString message =
    tr("%1 still needs an API key (free tier keys are fine).\n\n%2")
      .arg(QString::fromStdString(profileName), hint);

  bool ok = false;
  const QString key =
    QInputDialog::getText(this, title, message, QLineEdit::Password, QString(), &ok);
  if (!ok) return false;
  const QString trimmed = key.trimmed();
  if (trimmed.isEmpty()) {
    QMessageBox::information(
      this, title,
      tr("No key entered. Get a free Groq key at console.groq.com/keys, then try again — "
         "or open Agents → Open AI Settings…"));
    return false;
  }

  std::string err;
  if (!AIFreeAgents::writeProfileApiKey(profileName, trimmed.toStdString(), err)) {
    QMessageBox::warning(this, title,
                         tr("Could not save API key:\n%1").arg(QString::fromStdString(err)));
    return false;
  }
  return true;
}

void ChatWidget::showAgentPicker()
{
  if (!agentButton || isRequestRunning) return;

  const bool dark = isDarkTheme();

  // Transparent host so the drop shadow isn't clipped by the popup chrome.
  auto *host = new QWidget(nullptr, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
  host->setAttribute(Qt::WA_DeleteOnClose);
  host->setAttribute(Qt::WA_TranslucentBackground);

  auto *popup = new QFrame(host);
  popup->setObjectName(QStringLiteral("agentPickerPopup"));
  popup->setMinimumWidth(272);
  popup->setMaximumWidth(320);

  auto *hostLay = new QVBoxLayout(host);
  hostLay->setContentsMargins(14, 14, 14, 18);
  hostLay->setSpacing(0);
  hostLay->addWidget(popup);

  auto *root = new QVBoxLayout(popup);
  root->setContentsMargins(6, 6, 6, 6);
  root->setSpacing(2);

  auto *header = new QLabel(tr("Agents"), popup);
  header->setObjectName(QStringLiteral("agentPickerHeader"));
  root->addWidget(header);

  auto pick = [this, host](const QString& id) {
    selectComposerAgent(id);
    host->close();
  };

  for (const auto& opt : composerAgentOptions()) {
    auto *row = new AgentPickerRow(opt.id, opt.title, opt.subtitle, opt.id == composerAgentId, popup);
    row->onActivated = pick;
    root->addWidget(row);
  }

  auto *sep = new QFrame(popup);
  sep->setObjectName(QStringLiteral("agentPickerSep"));
  sep->setFrameShape(QFrame::NoFrame);
  sep->setFixedHeight(1);
  root->addWidget(sep);

  auto *apiRow =
    new AgentPickerRow(QString::fromUtf8(kApiKeyAgentId), tr("API Key"), tr("Settings model"),
                       composerAgentId == QString::fromUtf8(kApiKeyAgentId), popup);
  apiRow->onActivated = pick;
  root->addWidget(apiRow);

  auto *footerSep = new QFrame(popup);
  footerSep->setObjectName(QStringLiteral("agentPickerSep"));
  footerSep->setFrameShape(QFrame::NoFrame);
  footerSep->setFixedHeight(1);
  root->addWidget(footerSep);

  auto *settingsBtn = new QPushButton(tr("Open AI Settings…"), popup);
  settingsBtn->setObjectName(QStringLiteral("agentPickerFooter"));
  settingsBtn->setCursor(Qt::PointingHandCursor);
  settingsBtn->setFlat(true);
  connect(settingsBtn, &QPushButton::clicked, this, [this, host]() {
    host->close();
    onSettingsPressed();
  });
  root->addWidget(settingsBtn);

  if (dark) {
    popup->setStyleSheet(QStringLiteral(R"(
      QFrame#agentPickerPopup {
        background: #252526;
        border: 1px solid #3c3c3c;
        border-radius: 12px;
      }
      QLabel#agentPickerHeader {
        color: #8c8c8c;
        font-size: 10px;
        font-weight: 400;
        letter-spacing: 0.2px;
        padding: 4px 10px 4px 10px;
      }
      QFrame#agentPickerRow {
        background: transparent;
        border: none;
        border-radius: 8px;
      }
      QFrame#agentPickerRow[hovered="true"] {
        background: #2a2d2e;
      }
      QFrame#agentPickerRow[selected="true"] {
        background: #2a2d2e;
      }
      QLabel#agentPickerTitle {
        color: #e0e0e0;
        font-size: 12px;
        font-weight: 400;
      }
      QLabel#agentPickerSubtitle {
        color: #8c8c8c;
        font-size: 10px;
        font-weight: 400;
      }
      QLabel#agentPickerCheck {
        color: #89b4fa;
        font-size: 12px;
        font-weight: 400;
      }
      QFrame#agentPickerSep {
        background: #3c3c3c;
        border: none;
        margin: 4px 8px;
        max-height: 1px;
      }
      QPushButton#agentPickerFooter {
        background: transparent;
        border: none;
        border-radius: 8px;
        color: #cccccc;
        font-size: 12px;
        font-weight: 400;
        text-align: left;
        padding: 8px 12px;
      }
      QPushButton#agentPickerFooter:hover {
        background: #2a2d2e;
      }
    )"));
  } else {
    popup->setStyleSheet(QStringLiteral(R"(
      QFrame#agentPickerPopup {
        background: #ffffff;
        border: 1px solid #e6e6e6;
        border-radius: 12px;
      }
      QLabel#agentPickerHeader {
        color: #8a8a8a;
        font-size: 10px;
        font-weight: 400;
        letter-spacing: 0.2px;
        padding: 4px 10px 4px 10px;
      }
      QFrame#agentPickerRow {
        background: transparent;
        border: none;
        border-radius: 8px;
      }
      QFrame#agentPickerRow[hovered="true"] {
        background: #f3f3f3;
      }
      QFrame#agentPickerRow[selected="true"] {
        background: #f5f5f5;
      }
      QLabel#agentPickerTitle {
        color: #1f1f1f;
        font-size: 12px;
        font-weight: 400;
      }
      QLabel#agentPickerSubtitle {
        color: #8a8a8a;
        font-size: 10px;
        font-weight: 400;
      }
      QLabel#agentPickerCheck {
        color: #3b82f6;
        font-size: 12px;
        font-weight: 400;
      }
      QFrame#agentPickerSep {
        background: #ececec;
        border: none;
        margin: 4px 8px;
        max-height: 1px;
      }
      QPushButton#agentPickerFooter {
        background: transparent;
        border: none;
        border-radius: 8px;
        color: #333333;
        font-size: 12px;
        font-weight: 400;
        text-align: left;
        padding: 8px 12px;
      }
      QPushButton#agentPickerFooter:hover {
        background: #f3f3f3;
      }
    )"));
  }

  auto *shadow = new QGraphicsDropShadowEffect(popup);
  shadow->setBlurRadius(28);
  shadow->setOffset(0, 10);
  shadow->setColor(dark ? QColor(0, 0, 0, 140) : QColor(0, 0, 0, 48));
  popup->setGraphicsEffect(shadow);

  host->adjustSize();
  const QPoint btnGlobal = agentButton->mapToGlobal(QPoint(0, 0));
  // Account for host margins so the card aligns near the chip.
  QPoint pos(btnGlobal.x() - 18, btnGlobal.y() - host->height() + 6);
  if (QScreen *screen = QGuiApplication::screenAt(btnGlobal)) {
    const QRect avail = screen->availableGeometry();
    if (pos.x() + host->width() > avail.right()) {
      pos.setX(avail.right() - host->width() - 4);
    }
    if (pos.x() < avail.left()) pos.setX(avail.left() + 4);
    if (pos.y() < avail.top()) {
      pos.setY(btnGlobal.y() + agentButton->height() - 6);
    }
  }
  host->move(pos);
  host->show();
}

void ChatWidget::applyComposerAgentOverride()
{
  if (!aiService) return;
  if (composerAgentId == QString::fromUtf8(kApiKeyAgentId)) {
    aiService->clearTurnProfileOverride();
  } else {
    aiService->setTurnProfileOverride(composerAgentId.toStdString());
  }
}

void ChatWidget::clearComposerAgentOverride()
{
  if (aiService) {
    aiService->clearTurnProfileOverride();
  }
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
  agentButton->setToolTip(_("Choose which agent handles this chat"));
  connect(agentButton, &QPushButton::clicked, this, &ChatWidget::showAgentPicker);
  toolbarLayout->addWidget(agentButton);
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
  if (agentButton) {
    agentButton->setIcon(QIcon::fromTheme(QStringLiteral("chokusen-agent")));
  }
  if (historyButton) historyButton->setIcon(QIcon::fromTheme(QStringLiteral("chokusen-history")));
  if (clearChatButton) clearChatButton->setIcon(QIcon::fromTheme(QStringLiteral("chokusen-recycle")));
  if (layoutButton) layoutButton->setIcon(makeSidebarIcon(dark));
  updateAgentButton();
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
    mw->startAIFullRender([]() {});
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
    detail = tr("Tool: set_editor_code\nResult: Applied code to the active editor. Full render (F6) runs when the reply finishes.");
  } else if (name == "trigger_preview") {
    summary = tr("Queued full render");
    detail = QString::fromStdString("Tool: trigger_preview\nResult: " + result);
  } else {
    summary = tr("Executed tool: %1").arg(QString::fromStdString(name));
    detail = QString::fromStdString("Tool: " + name + "\nResult: " + result);
  }

  if (activeAIBubble) {
    activeAIBubble->addToolCall(summary, detail);
  }

  scrollArea->verticalScrollBar()->setValue(scrollArea->verticalScrollBar()->maximum());
}

std::string ChatWidget::executeTool(const std::string& name, const std::string& arguments_json)
{
  // Drop any partial prose from the tool-call turn; only the final reply is shown in chat.
  if (activeResponseText) {
    activeResponseText->clear();
  }

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
    result_val =
      "Success: Code applied to the editor. Full render (F6) will run once when the reply finishes.";
  } else if (name == "trigger_preview") {
    // Queue a single F6 render for the end of the turn instead of rendering mid-reply.
    pendingPreviewRender = true;
    result_val = "Success: Full render queued; it will run once when the reply finishes.";
  } else {
    result_val = "Error: Unknown tool name '" + name + "'.";
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
