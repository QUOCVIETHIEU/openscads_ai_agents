#include "gui/project/ProjectFileIconProvider.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <functional>

namespace {

// Monochrome outline icons (VS Code default style): one neutral grey per theme,
// no fills, no accent colors.

QColor strokeColor(bool dark)
{
  return dark ? QColor("#c5c5c5") : QColor("#616161");
}

QIcon paintIcon(bool dark, const std::function<void(QPainter&, qreal, const QPen&)>& draw)
{
  QIcon icon;
  const QPen pen(strokeColor(dark), 1.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  for (qreal dpr : {1.0, 2.0, 3.0}) {
    const int logical = 16;
    const int px = qMax(1, qRound(logical * dpr));
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    pm.setDevicePixelRatio(dpr);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    draw(p, logical, pen);
    p.end();
    icon.addPixmap(pm);
  }
  return icon;
}

QIcon folderIcon(bool dark)
{
  return paintIcon(dark, [](QPainter& p, qreal s, const QPen& pen) {
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    QPainterPath path;
    path.moveTo(s * 0.12, s * 0.78);
    path.lineTo(s * 0.12, s * 0.26);
    path.lineTo(s * 0.40, s * 0.26);
    path.lineTo(s * 0.48, s * 0.36);
    path.lineTo(s * 0.88, s * 0.36);
    path.lineTo(s * 0.88, s * 0.78);
    path.closeSubpath();
    p.drawPath(path);
  });
}

QIcon fileIcon(bool dark, bool withTextLines = true)
{
  return paintIcon(dark, [=](QPainter& p, qreal s, const QPen& pen) {
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    // Page with folded corner
    QPainterPath page;
    page.moveTo(s * 0.24, s * 0.10);
    page.lineTo(s * 0.58, s * 0.10);
    page.lineTo(s * 0.78, s * 0.30);
    page.lineTo(s * 0.78, s * 0.88);
    page.lineTo(s * 0.24, s * 0.88);
    page.closeSubpath();
    p.drawPath(page);
    p.drawLine(QPointF(s * 0.58, s * 0.10), QPointF(s * 0.58, s * 0.30));
    p.drawLine(QPointF(s * 0.58, s * 0.30), QPointF(s * 0.78, s * 0.30));
    if (withTextLines) {
      p.drawLine(QPointF(s * 0.34, s * 0.48), QPointF(s * 0.68, s * 0.48));
      p.drawLine(QPointF(s * 0.34, s * 0.62), QPointF(s * 0.68, s * 0.62));
      p.drawLine(QPointF(s * 0.34, s * 0.76), QPointF(s * 0.56, s * 0.76));
    }
  });
}

QIcon codeFileIcon(bool dark)
{
  return paintIcon(dark, [](QPainter& p, qreal s, const QPen& pen) {
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    QPainterPath page;
    page.moveTo(s * 0.24, s * 0.10);
    page.lineTo(s * 0.58, s * 0.10);
    page.lineTo(s * 0.78, s * 0.30);
    page.lineTo(s * 0.78, s * 0.88);
    page.lineTo(s * 0.24, s * 0.88);
    page.closeSubpath();
    p.drawPath(page);
    p.drawLine(QPointF(s * 0.58, s * 0.10), QPointF(s * 0.58, s * 0.30));
    p.drawLine(QPointF(s * 0.58, s * 0.30), QPointF(s * 0.78, s * 0.30));
    // < >
    p.drawLine(QPointF(s * 0.44, s * 0.50), QPointF(s * 0.34, s * 0.62));
    p.drawLine(QPointF(s * 0.34, s * 0.62), QPointF(s * 0.44, s * 0.74));
    p.drawLine(QPointF(s * 0.58, s * 0.50), QPointF(s * 0.68, s * 0.62));
    p.drawLine(QPointF(s * 0.68, s * 0.62), QPointF(s * 0.58, s * 0.74));
  });
}

QIcon meshFileIcon(bool dark)
{
  return paintIcon(dark, [](QPainter& p, qreal s, const QPen& pen) {
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    // Isometric cube
    const QPointF top(s * 0.50, s * 0.12);
    const QPointF left(s * 0.16, s * 0.32);
    const QPointF right(s * 0.84, s * 0.32);
    const QPointF bottomLeft(s * 0.16, s * 0.68);
    const QPointF bottomRight(s * 0.84, s * 0.68);
    const QPointF bottom(s * 0.50, s * 0.88);
    const QPointF center(s * 0.50, s * 0.50);
    QPainterPath hexagon;
    hexagon.moveTo(top);
    hexagon.lineTo(right);
    hexagon.lineTo(bottomRight);
    hexagon.lineTo(bottom);
    hexagon.lineTo(bottomLeft);
    hexagon.lineTo(left);
    hexagon.closeSubpath();
    p.drawPath(hexagon);
    p.drawLine(top, center);
    p.drawLine(left, center);
    p.drawLine(right, center);
    p.drawLine(center, bottom);
  });
}

QIcon imageFileIcon(bool dark)
{
  return paintIcon(dark, [](QPainter& p, qreal s, const QPen& pen) {
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(s * 0.14, s * 0.18, s * 0.72, s * 0.64), 1.5, 1.5);
    p.drawEllipse(QPointF(s * 0.36, s * 0.40), s * 0.07, s * 0.07);
    QPainterPath mountain;
    mountain.moveTo(s * 0.22, s * 0.74);
    mountain.lineTo(s * 0.46, s * 0.48);
    mountain.lineTo(s * 0.58, s * 0.60);
    mountain.lineTo(s * 0.70, s * 0.50);
    mountain.lineTo(s * 0.84, s * 0.74);
    p.drawPath(mountain);
  });
}

QIcon gearFileIcon(bool dark)
{
  return paintIcon(dark, [](QPainter& p, qreal s, const QPen& pen) {
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    const QPointF c(s * 0.50, s * 0.50);
    p.drawEllipse(c, s * 0.16, s * 0.16);
    // Spokes
    for (int i = 0; i < 8; ++i) {
      const qreal angle = i * M_PI / 4.0;
      const QPointF dir(qCos(angle), qSin(angle));
      p.drawLine(c + dir * (s * 0.24), c + dir * (s * 0.34));
    }
  });
}

}  // namespace

void ProjectFileIconProvider::setDarkMode(bool dark)
{
  dark_ = dark;
}

QIcon ProjectFileIconProvider::icon(IconType type) const
{
  if (type == Folder) return folderIcon(dark_);
  if (type == File) return fileIcon(dark_);
  return QFileIconProvider::icon(type);
}

QIcon ProjectFileIconProvider::icon(const QFileInfo& info) const
{
  if (info.isDir()) {
    return folderIcon(dark_);
  }

  const QString suffix = info.suffix().toLower();

  if (suffix == QLatin1String("scad")) {
    return codeFileIcon(dark_);
  }
  if (suffix == QLatin1String("json")) {
    return gearFileIcon(dark_);
  }
  if (suffix == QLatin1String("stl") || suffix == QLatin1String("3mf") ||
      suffix == QLatin1String("obj") || suffix == QLatin1String("amf") ||
      suffix == QLatin1String("off")) {
    return meshFileIcon(dark_);
  }
  if (suffix == QLatin1String("png") || suffix == QLatin1String("jpg") ||
      suffix == QLatin1String("jpeg") || suffix == QLatin1String("webp") ||
      suffix == QLatin1String("gif") || suffix == QLatin1String("bmp") ||
      suffix == QLatin1String("svg")) {
    return imageFileIcon(dark_);
  }

  return fileIcon(dark_);
}
