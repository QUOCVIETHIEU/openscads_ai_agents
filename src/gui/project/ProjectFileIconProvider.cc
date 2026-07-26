#include "gui/project/ProjectFileIconProvider.h"

#include <QIcon>
#include <QString>

namespace {

QIcon themeIcon(const char *name)
{
  return QIcon::fromTheme(QString::fromLatin1(name));
}

}  // namespace

void ProjectFileIconProvider::setDarkMode(bool dark)
{
  dark_ = dark;
}

QIcon ProjectFileIconProvider::icon(IconType type) const
{
  // Cursor-style: directories are identified by their expand chevron alone.
  if (type == Folder) return {};
  if (type == File) return themeIcon("explorer-file");
  return QFileIconProvider::icon(type);
}

QIcon ProjectFileIconProvider::icon(const QFileInfo& info) const
{
  if (info.isDir()) {
    return {};
  }

  const QString suffix = info.suffix().toLower();

  if (suffix == QLatin1String("scad")) {
    return themeIcon("explorer-scad");
  }
  if (suffix == QLatin1String("md") || suffix == QLatin1String("markdown")) {
    return themeIcon("explorer-markdown");
  }
  if (suffix == QLatin1String("json")) {
    return themeIcon("explorer-json");
  }
  if (suffix == QLatin1String("stl") || suffix == QLatin1String("3mf") ||
      suffix == QLatin1String("obj") || suffix == QLatin1String("amf") ||
      suffix == QLatin1String("off")) {
    return themeIcon("explorer-mesh");
  }
  if (suffix == QLatin1String("png") || suffix == QLatin1String("jpg") ||
      suffix == QLatin1String("jpeg") || suffix == QLatin1String("webp") ||
      suffix == QLatin1String("gif") || suffix == QLatin1String("bmp") ||
      suffix == QLatin1String("svg")) {
    return themeIcon("explorer-image");
  }

  return themeIcon("explorer-file");
}
