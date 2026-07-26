#include "gui/ai/ChatInputEdit.h"
#include <QFrame>
#include <QKeyEvent>
#include <QMimeData>
#include <QUrl>
#include <QImageReader>

ChatInputEdit::ChatInputEdit(QWidget *parent) : QPlainTextEdit(parent)
{
  setPlaceholderText(tr("Describe the model you want to build…"));
  setFrameStyle(QFrame::NoFrame);
  setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setTabChangesFocus(false);
  document()->setDocumentMargin(0);
  setMinimumHeight(40);
  setMaximumHeight(96);
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

bool ChatInputEdit::canInsertFromMimeData(const QMimeData *source) const
{
  if (!source) return false;
  if (source->hasImage()) return true;
  if (source->hasUrls()) {
    for (const QUrl& url : source->urls()) {
      if (!url.isLocalFile()) continue;
      const QString path = url.toLocalFile();
      if (QImageReader(path).canRead()) return true;
    }
  }
  return QPlainTextEdit::canInsertFromMimeData(source);
}

void ChatInputEdit::insertFromMimeData(const QMimeData *source)
{
  if (!source) return;

  if (source->hasImage()) {
    const QImage image = qvariant_cast<QImage>(source->imageData());
    if (!image.isNull()) {
      emit imagePasted(image);
      return;
    }
  }

  if (source->hasUrls()) {
    bool pastedImage = false;
    for (const QUrl& url : source->urls()) {
      if (!url.isLocalFile()) continue;
      QImage image(url.toLocalFile());
      if (image.isNull()) continue;
      emit imagePasted(image);
      pastedImage = true;
    }
    if (pastedImage) return;
  }

  QPlainTextEdit::insertFromMimeData(source);
}
