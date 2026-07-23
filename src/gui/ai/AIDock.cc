#include "gui/ai/AIDock.h"
#include "gui/ai/ChatWidget.h"
#include <QShowEvent>

AIDock::AIDock(QWidget *parent) : Dock(parent)
{
  this->setObjectName("aiDock");
  this->chat = new ChatWidget(this);
  setWidget(this->chat);
}

AIDock::~AIDock()
{
}

void AIDock::showEvent(QShowEvent *event)
{
  Dock::showEvent(event);
}
