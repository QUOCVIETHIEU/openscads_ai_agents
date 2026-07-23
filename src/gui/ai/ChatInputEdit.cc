#include "gui/ai/ChatInputEdit.h"
#include <QFrame>
#include <QKeyEvent>

ChatInputEdit::ChatInputEdit(QWidget *parent) : QPlainTextEdit(parent)
{
  setPlaceholderText(tr("Add a follow-up"));
  setFrameStyle(QFrame::NoFrame);
  setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setTabChangesFocus(false);
  document()->setDocumentMargin(0);
  setMinimumHeight(28);
  setMaximumHeight(80);
}

void ChatInputEdit::keyPressEvent(QKeyEvent *event)
{
  if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
    if (event->modifiers() & Qt::ShiftModifier) {
      QPlainTextEdit::keyPressEvent(event);
    } else {
      emit sendPressed();
    }
  } else {
    QPlainTextEdit::keyPressEvent(event);
  }
}
