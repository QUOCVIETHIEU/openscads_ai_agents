#include "gui/BottomPanelHeader.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSizePolicy>
#include <QToolButton>
#include <functional>

#include "gui/qtgettext.h"
#include "openscad_gui.h"

namespace {

QIcon paintIcon(int logicalSize, const std::function<void(QPainter&, int)>& paint)
{
  QIcon icon;
  for (qreal dpr : {1.0, 2.0, 3.0}) {
    const int px = qMax(1, qRound(logicalSize * dpr));
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    pm.setDevicePixelRatio(dpr);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    paint(p, logicalSize);
    p.end();
    icon.addPixmap(pm);
  }
  return icon;
}

}  // namespace

BottomPanelHeader::BottomPanelHeader(QWidget *parent) : QWidget(parent)
{
  setObjectName(QStringLiteral("bottomPanelHeader"));
  setFixedHeight(35);

  layout = new QHBoxLayout(this);
  layout->setContentsMargins(8, 0, 6, 0);
  layout->setSpacing(0);

  consoleTab = makeTabButton(_("Console"));
  errorLogTab = makeTabButton(_("Error Log"));

  errorBadge = new QLabel(this);
  errorBadge->setObjectName(QStringLiteral("bottomPanelBadge"));
  errorBadge->setAlignment(Qt::AlignCenter);
  errorBadge->setFixedHeight(16);
  errorBadge->hide();

  errorTabWrap = new QWidget(this);
  auto *errorLay = new QHBoxLayout(errorTabWrap);
  errorLay->setContentsMargins(0, 0, 0, 0);
  errorLay->setSpacing(4);
  errorLay->addWidget(errorLogTab);
  errorLay->addWidget(errorBadge, 0, Qt::AlignVCenter);

  layout->addWidget(consoleTab, 0, Qt::AlignVCenter);
  layout->addSpacing(4);
  layout->addWidget(errorTabWrap, 0, Qt::AlignVCenter);
  layout->addStretch(1);

  clearBtn = makeIconButton(_("Clear"));
  maximizeBtn = makeIconButton(_("Maximize Panel"));
  moreBtn = makeIconButton(_("More Actions..."));
  closeBtn = makeIconButton(_("Close Panel"));

  layout->addWidget(clearBtn, 0, Qt::AlignVCenter);
  layout->addWidget(maximizeBtn, 0, Qt::AlignVCenter);
  layout->addWidget(moreBtn, 0, Qt::AlignVCenter);
  layout->addWidget(closeBtn, 0, Qt::AlignVCenter);

  connect(consoleTab, &QToolButton::clicked, this, [this]() { setActiveTab(ConsoleTab); });
  connect(errorLogTab, &QToolButton::clicked, this, [this]() { setActiveTab(ErrorLogTab); });
  connect(clearBtn, &QToolButton::clicked, this, &BottomPanelHeader::clearClicked);
  connect(maximizeBtn, &QToolButton::clicked, this, &BottomPanelHeader::maximizeClicked);
  connect(closeBtn, &QToolButton::clicked, this, &BottomPanelHeader::closeClicked);

  auto *moreMenu = new QMenu(this);
  moreMenu->addAction(_("Clear Console"), this, &BottomPanelHeader::moreClearConsole);
  moreMenu->addAction(_("Save Console..."), this, &BottomPanelHeader::moreSaveConsole);
  moreBtn->setMenu(moreMenu);
  moreBtn->setPopupMode(QToolButton::InstantPopup);

  applyTheme();
  setActiveTab(ConsoleTab);
}

QToolButton *BottomPanelHeader::makeTabButton(const QString& text)
{
  auto *btn = new QToolButton(this);
  btn->setText(text);
  btn->setCheckable(true);
  btn->setAutoRaise(true);
  btn->setCursor(Qt::PointingHandCursor);
  btn->setFocusPolicy(Qt::NoFocus);
  btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
  return btn;
}

QToolButton *BottomPanelHeader::makeIconButton(const QString& tooltip)
{
  auto *btn = new QToolButton(this);
  btn->setToolTip(tooltip);
  btn->setAutoRaise(true);
  btn->setCursor(Qt::PointingHandCursor);
  btn->setFocusPolicy(Qt::NoFocus);
  btn->setFixedSize(28, 28);
  btn->setIconSize(QSize(16, 16));
  return btn;
}

void BottomPanelHeader::setActiveTab(int tab)
{
  activeTabIndex = tab;
  consoleTab->setChecked(tab == ConsoleTab);
  errorLogTab->setChecked(tab == ErrorLogTab);
  emit tabChanged(tab);
  applyTheme();  // refresh selected tab underline colors
}

void BottomPanelHeader::setErrorCount(int count)
{
  if (count <= 0) {
    errorBadge->hide();
    errorBadge->clear();
    return;
  }
  errorBadge->setText(count > 99 ? QStringLiteral("99+") : QString::number(count));
  errorBadge->setMinimumWidth(count > 9 ? 22 : 16);
  errorBadge->show();
}

void BottomPanelHeader::setMaximized(bool maximized)
{
  isMaximized = maximized;
  maximizeBtn->setToolTip(maximized ? _("Restore Panel") : _("Maximize Panel"));
  rebuildIcons();
}

void BottomPanelHeader::rebuildIcons()
{
  // Lighter stroke weight to match the compact terminal chrome
  const QColor g = dark ? QColor("#b0b0b0") : QColor("#6a6a6a");

  clearBtn->setIcon(paintIcon(16, [g](QPainter& p, int s) {
    QPen pen(g, 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    // thin trash can
    p.drawLine(QPointF(s * 0.24, s * 0.30), QPointF(s * 0.76, s * 0.30));
    p.drawLine(QPointF(s * 0.36, s * 0.30), QPointF(s * 0.40, s * 0.20));
    p.drawLine(QPointF(s * 0.64, s * 0.30), QPointF(s * 0.60, s * 0.20));
    p.drawLine(QPointF(s * 0.40, s * 0.20), QPointF(s * 0.60, s * 0.20));
    p.drawRoundedRect(QRectF(s * 0.30, s * 0.30, s * 0.40, s * 0.50), 1.0, 1.0);
    p.drawLine(QPointF(s * 0.43, s * 0.42), QPointF(s * 0.43, s * 0.68));
    p.drawLine(QPointF(s * 0.57, s * 0.42), QPointF(s * 0.57, s * 0.68));
  }));

  maximizeBtn->setIcon(paintIcon(16, [g, up = !isMaximized](QPainter& p, int s) {
    QPen pen(g, 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    const qreal cx = s * 0.5;
    if (up) {
      p.drawLine(QPointF(cx, s * 0.28), QPointF(cx, s * 0.72));
      p.drawLine(QPointF(cx, s * 0.28), QPointF(s * 0.32, s * 0.46));
      p.drawLine(QPointF(cx, s * 0.28), QPointF(s * 0.68, s * 0.46));
    } else {
      p.drawLine(QPointF(cx, s * 0.28), QPointF(cx, s * 0.72));
      p.drawLine(QPointF(cx, s * 0.72), QPointF(s * 0.32, s * 0.54));
      p.drawLine(QPointF(cx, s * 0.72), QPointF(s * 0.68, s * 0.54));
    }
  }));

  moreBtn->setIcon(paintIcon(16, [g](QPainter& p, int s) {
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    const qreal r = 1.15;
    const qreal cy = s * 0.5;
    p.drawEllipse(QPointF(s * 0.25, cy), r, r);
    p.drawEllipse(QPointF(s * 0.50, cy), r, r);
    p.drawEllipse(QPointF(s * 0.75, cy), r, r);
  }));

  closeBtn->setIcon(paintIcon(16, [g](QPainter& p, int s) {
    QPen pen(g, 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.drawLine(QPointF(s * 0.30, s * 0.30), QPointF(s * 0.70, s * 0.70));
    p.drawLine(QPointF(s * 0.70, s * 0.30), QPointF(s * 0.30, s * 0.70));
  }));
}

void BottomPanelHeader::applyTheme()
{
  dark = isDarkMode();
  const QString bg = dark ? QStringLiteral("#252526") : QStringLiteral("#f3f3f3");
  const QString panel = dark ? QStringLiteral("#1e1e1e") : QStringLiteral("#f8f8f8");
  const QString sep = dark ? QStringLiteral("#2b2b2b") : QStringLiteral("#e5e5e5");
  const QString muted = dark ? QStringLiteral("#969696") : QStringLiteral("#6e6e6e");
  const QString fg = dark ? QStringLiteral("#ffffff") : QStringLiteral("#1e1e1e");
  const QString hover = dark ? QStringLiteral("#2a2d2e") : QStringLiteral("#e8e8e8");
  const QString accent = dark ? QStringLiteral("#007acc") : QStringLiteral("#005fb8");
  const QString badgeBg = QStringLiteral("#007acc");

  setStyleSheet(QStringLiteral(R"(
    QWidget#bottomPanelHeader {
      background: %1;
      border: none;
      border-top: 1px solid %2;
      border-bottom: 1px solid %2;
    }
    QToolButton {
      background: transparent;
      border: none;
      border-radius: 0px;
      color: %3;
      font-size: 12px;
      padding: 0px 10px;
      margin: 0px;
      min-height: 34px;
      max-height: 34px;
    }
    QToolButton:hover {
      background: %4;
      color: %5;
    }
    QToolButton:checked {
      color: %5;
      background: transparent;
      border-bottom: 1px solid %6;
      font-weight: 600;
    }
    QToolButton#bottomPanelIconBtn, QToolButton {
      /* icon buttons keep square hit area via fixed size */
    }
    QLabel#bottomPanelBadge {
      background: %7;
      color: #ffffff;
      border-radius: 8px;
      font-size: 10px;
      font-weight: 700;
      padding: 0px 5px;
      min-height: 16px;
      max-height: 16px;
    }
  )")
                  .arg(bg, sep, muted, hover, fg, accent, badgeBg));

  // Mark icon buttons so padding does not stretch them oddly
  for (QToolButton *b : {clearBtn, maximizeBtn, moreBtn, closeBtn}) {
    b->setStyleSheet(QStringLiteral(
      "QToolButton { min-width: 28px; max-width: 28px; padding: 0px; margin: 0px 1px; }"
      "QToolButton:hover { background: %1; border-radius: 4px; }"
      "QToolButton::menu-indicator { image: none; width: 0px; }")
                       .arg(hover));
  }

  // Selected tab uses panel color continuity under the content
  Q_UNUSED(panel);
  rebuildIcons();
}
