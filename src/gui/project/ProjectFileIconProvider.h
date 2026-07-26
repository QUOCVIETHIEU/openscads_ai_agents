#pragma once

#include <QFileIconProvider>
#include <QIcon>

/*! Compact VS Code / Seti-style icons for the project explorer tree. */
class ProjectFileIconProvider : public QFileIconProvider
{
public:
  ProjectFileIconProvider() = default;

  QIcon icon(IconType type) const override;
  QIcon icon(const QFileInfo& info) const override;

  void setDarkMode(bool dark);

private:
  bool dark_ = false;
};
