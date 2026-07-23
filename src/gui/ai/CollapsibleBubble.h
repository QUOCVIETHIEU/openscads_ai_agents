#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QPalette>
#include <QApplication>
#include <vector>
#include <QString>

class CollapsibleBubble : public QWidget
{
  Q_OBJECT
public:
  CollapsibleBubble(const QString& summary, const QString& detail, QWidget *parent = nullptr)
    : QWidget(parent)
  {
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 2, 0, 2);

    QFrame *bubbleFrame = new QFrame(this);
    bubbleFrame->setFrameShape(QFrame::NoFrame);

    bool dark = isDarkTheme();
    // VS Code-style tool call log — flat, subtle border
    QString frameStyle =
      dark ? "QFrame { background-color: #252526; border: 1px solid #3c3c3c; border-radius: 2px; }"
           : "QFrame { background-color: #f3f3f3; border: 1px solid #e5e5e5; border-radius: 2px; }";
    bubbleFrame->setStyleSheet(frameStyle);

    QVBoxLayout *frameLayout = new QVBoxLayout(bubbleFrame);
    frameLayout->setContentsMargins(8, 6, 8, 6);

    toggleButton = new QPushButton(this);
    toggleButton->setCheckable(true);
    toggleButton->setChecked(false);
    toggleButton->setFlat(true);
    toggleButton->setStyleSheet(dark ? "QPushButton { text-align: left; font-weight: 600; color: "
                                       "#9cdcfe; padding: 0; border: none; font-size: 12px; }"
                                       "QPushButton:hover { color: #4fc1ff; }"
                                     : "QPushButton { text-align: left; font-weight: 600; color: "
                                       "#0451a5; padding: 0; border: none; font-size: 12px; }"
                                       "QPushButton:hover { color: #007acc; }");

    detailsLabel = new QLabel(this);
    detailsLabel->setWordWrap(true);
    detailsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailsLabel->setStyleSheet(
      dark ? "QLabel { color: #d4d4d4; padding-top: 6px; font-size: 12px; font-family: Menlo, Monaco, 'Courier New', monospace; }"
           : "QLabel { color: #333333; padding-top: 6px; font-size: 12px; font-family: Menlo, Monaco, 'Courier New', monospace; }");
    detailsLabel->hide();

    frameLayout->addWidget(toggleButton);
    frameLayout->addWidget(detailsLabel);
    layout->addWidget(bubbleFrame);

    addToolCall(summary, detail);

    connect(toggleButton, &QPushButton::clicked, this, [this](bool checked) {
      detailsLabel->setVisible(checked);
      updateButtonText();
    });
  }

  void addToolCall(const QString& summary, const QString& detail)
  {
    toolCalls.push_back({summary, detail});
    updateContent();
  }

private:
  bool isDarkTheme() const
  {
    QPalette pal = QApplication::palette();
    return pal.color(QPalette::Window).lightness() < 128;
  }

  void updateContent()
  {
    QString detailsText;
    for (size_t i = 0; i < toolCalls.size(); ++i) {
      if (i > 0) detailsText += "\n\n";
      detailsText += QString("• %1\n%2").arg(toolCalls[i].summary, toolCalls[i].detail);
    }
    detailsLabel->setText(detailsText);
    updateButtonText();
  }

  void updateButtonText()
  {
    bool expanded = toggleButton->isChecked();
    QString arrow = expanded ? "▼" : "▶";
    QString text = QString("%1 Used %2 tool(s)").arg(arrow).arg(toolCalls.size());
    toggleButton->setText(text);
  }

  struct ToolCallLog {
    QString summary;
    QString detail;
  };

  std::vector<ToolCallLog> toolCalls;
  QPushButton *toggleButton;
  QLabel *detailsLabel;
};
