#pragma once

#include "gui/Dock.h"

class ChatWidget;

class AIDock : public Dock
{
  Q_OBJECT

public:
  AIDock(QWidget *parent = nullptr);
  virtual ~AIDock();

  ChatWidget *chatWidget() const { return chat; }

protected:
  void showEvent(QShowEvent *event) override;

private:
  ChatWidget *chat;
};
