#pragma once

#include <QFileIconProvider>
#include <QIcon>

/*! Cursor / VS Code Seti-style file icons for the project explorer. */
class ProjectFileIconProvider : public QFileIconProvider
{
public:
  ProjectFileIconProvider() = default;

  QIcon icon(IconType type) const override;
  QIcon icon(const QFileInfo& info) const override;

  void setDarkMode(bool dark);

private:
  bool dark_ = false;  // retained for API; icons follow the active Qt icon theme
};
