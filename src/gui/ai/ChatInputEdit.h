#pragma once

#include <QPlainTextEdit>
#include <QImage>

class QKeyEvent;
class QMimeData;

class ChatInputEdit : public QPlainTextEdit
{
  Q_OBJECT

public:
  ChatInputEdit(QWidget *parent = nullptr);

protected:
  void keyPressEvent(QKeyEvent *event) override;
  void insertFromMimeData(const QMimeData *source) override;
  bool canInsertFromMimeData(const QMimeData *source) const override;

signals:
  void sendPressed();
  void imagePasted(const QImage& image);
};
