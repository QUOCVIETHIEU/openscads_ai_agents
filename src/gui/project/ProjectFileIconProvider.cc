#include "gui/project/ProjectFileIconProvider.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <functional>

namespace {

QIcon paintIcon(const QColor& fill, const QColor& stroke,
                const std::function<void(QPainter&, int, const QColor&, const QColor&)>& draw)
{
  QIcon icon;
  for (qreal dpr : {1.0, 2.0, 3.0}) {
    const int logical = 16;
    const int px = qMax(1, qRound(logical * dpr));
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    pm.setDevicePixelRatio(dpr);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    draw(p, logical, fill, stroke);
    p.end();
    icon.addPixmap(pm);
  }
  return icon;
}

QIcon folderIcon(bool dark, const QColor& accent)
{
  const QColor stroke = dark ? QColor("#c5c5c5") : QColor("#5a5a5a");
  const QColor fill = accent.isValid()
                        ? accent
                        : (dark ? QColor("#c09553") : QColor("#dcb67a"));
  return paintIcon(fill, stroke, [](QPainter& p, int s, const QColor& f, const QColor& /*st*/) {
    p.setPen(Qt::NoPen);
    p.setBrush(f);
    // Tab
    QPainterPath tab;
    tab.moveTo(s * 0.12, s * 0.34);
    tab.lineTo(s * 0.12, s * 0.26);
    tab.lineTo(s * 0.38, s * 0.26);
    tab.lineTo(s * 0.46, s * 0.34);
    tab.closeSubpath();
    p.drawPath(tab);
    // Body
    p.drawRoundedRect(QRectF(s * 0.12, s * 0.34, s * 0.76, s * 0.48), 1.4, 1.4);
  });
}

QIcon fileIcon(bool dark, const QColor& accent, const QString& badge = {})
{
  const QColor page = dark ? QColor("#3c3c3c") : QColor("#ffffff");
  const QColor stroke = dark ? QColor("#9d9d9d") : QColor("#6e6e6e");
  const QColor accentUse = accent.isValid() ? accent : (dark ? QColor("#75beff") : QColor("#007acc"));
  return paintIcon(page, stroke, [=](QPainter& p, int s, const QColor& f, const QColor& st) {
    p.setPen(QPen(st, 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(f);
    const QRectF body(s * 0.22, s * 0.10, s * 0.56, s * 0.78);
    p.drawRoundedRect(body, 1.2, 1.2);
    // Fold corner
    p.setBrush(dark ? QColor("#2a2a2a") : QColor("#f0f0f0"));
    QPainterPath fold;
    fold.moveTo(s * 0.52, s * 0.10);
    fold.lineTo(s * 0.78, s * 0.36);
    fold.lineTo(s * 0.52, s * 0.36);
    fold.closeSubpath();
    p.drawPath(fold);
    p.setPen(QPen(st, 1.0));
    p.drawLine(QPointF(s * 0.52, s * 0.10), QPointF(s * 0.52, s * 0.36));
    p.drawLine(QPointF(s * 0.52, s * 0.36), QPointF(s * 0.78, s * 0.36));

    if (!badge.isEmpty()) {
      p.setPen(Qt::NoPen);
      p.setBrush(accentUse);
      p.drawRoundedRect(QRectF(s * 0.18, s * 0.58, s * 0.64, s * 0.28), 2.0, 2.0);
      p.setPen(Qt::white);
      QFont font = p.font();
      font.setBold(true);
      font.setPixelSize(qMax(6, int(s * 0.42)));
      p.setFont(font);
      p.drawText(QRectF(s * 0.18, s * 0.56, s * 0.64, s * 0.32), Qt::AlignCenter, badge);
    }
  });
}

QIcon imageIcon(bool dark)
{
  const QColor stroke = dark ? QColor("#9d9d9d") : QColor("#6e6e6e");
  const QColor fill = dark ? QColor("#3c3c3c") : QColor("#ffffff");
  const QColor accent = dark ? QColor("#c586c0") : QColor("#9b59b6");
  return paintIcon(fill, stroke, [=](QPainter& p, int s, const QColor& f, const QColor& st) {
    p.setPen(QPen(st, 1.0));
    p.setBrush(f);
    p.drawRoundedRect(QRectF(s * 0.14, s * 0.18, s * 0.72, s * 0.64), 1.5, 1.5);
    p.setBrush(accent);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(s * 0.36, s * 0.40), s * 0.10, s * 0.10);
    QPainterPath mountain;
    mountain.moveTo(s * 0.22, s * 0.72);
    mountain.lineTo(s * 0.46, s * 0.46);
    mountain.lineTo(s * 0.58, s * 0.58);
    mountain.lineTo(s * 0.70, s * 0.48);
    mountain.lineTo(s * 0.86, s * 0.72);
    mountain.closeSubpath();
    p.setBrush(dark ? QColor("#75beff") : QColor("#3498db"));
    p.drawPath(mountain);
  });
}

QColor folderAccentForName(const QString& name, bool dark)
{
  const QString n = name.toLower();
  if (n == QLatin1String("design")) return dark ? QColor("#75beff") : QColor("#3794ff");
  if (n == QLatin1String("assets")) return dark ? QColor("#c586c0") : QColor("#a855f7");
  if (n == QLatin1String("skills")) return dark ? QColor("#89d185") : QColor("#3fa266");
  if (n == QLatin1String("rules")) return dark ? QColor("#e2c08d") : QColor("#d29922");
  if (n == QLatin1String("exports")) return dark ? QColor("#f14c4c") : QColor("#e74c3c");
  if (n == QLatin1String("system")) return dark ? QColor("#9d9d9d") : QColor("#7a7a7a");
  return QColor();  // default folder gold
}

}  // namespace

void ProjectFileIconProvider::setDarkMode(bool dark)
{
  dark_ = dark;
}

QIcon ProjectFileIconProvider::icon(IconType type) const
{
  if (type == Folder) return folderIcon(dark_, QColor());
  if (type == File) return fileIcon(dark_, QColor());
  return QFileIconProvider::icon(type);
}

QIcon ProjectFileIconProvider::icon(const QFileInfo& info) const
{
  if (info.isDir()) {
    return folderIcon(dark_, folderAccentForName(info.fileName(), dark_));
  }

  const QString suffix = info.suffix().toLower();
  const QString name = info.fileName().toLower();

  if (suffix == QLatin1String("scad")) {
    return fileIcon(dark_, dark_ ? QColor("#e2c08d") : QColor("#e67e22"), QStringLiteral("SC"));
  }
  if (suffix == QLatin1String("md") || name == QLatin1String("readme") ||
      name.startsWith(QLatin1String("readme."))) {
    return fileIcon(dark_, dark_ ? QColor("#75beff") : QColor("#007acc"), QStringLiteral("MD"));
  }
  if (suffix == QLatin1String("json")) {
    return fileIcon(dark_, dark_ ? QColor("#cbcb41") : QColor("#b5a014"), QStringLiteral("{}"));
  }
  if (suffix == QLatin1String("stl") || suffix == QLatin1String("3mf") ||
      suffix == QLatin1String("obj") || suffix == QLatin1String("amf") ||
      suffix == QLatin1String("off")) {
    return fileIcon(dark_, dark_ ? QColor("#89d185") : QColor("#27ae60"), QStringLiteral("3D"));
  }
  if (suffix == QLatin1String("png") || suffix == QLatin1String("jpg") ||
      suffix == QLatin1String("jpeg") || suffix == QLatin1String("webp") ||
      suffix == QLatin1String("gif") || suffix == QLatin1String("bmp") ||
      suffix == QLatin1String("svg")) {
    return imageIcon(dark_);
  }
  if (suffix == QLatin1String("txt") || suffix == QLatin1String("csv")) {
    return fileIcon(dark_, dark_ ? QColor("#9d9d9d") : QColor("#6e6e6e"), QStringLiteral("TXT"));
  }

  return fileIcon(dark_, QColor());
}
