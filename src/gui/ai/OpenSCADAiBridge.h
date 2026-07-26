#pragma once

#include <QObject>
#include <atomic>
#include <functional>
#include <string>

class QTcpServer;
class QTcpSocket;

/*!
 * Localhost HTTP bridge so Cursor Agent MCP tools can call into OpenSCAD.
 * Endpoints:
 *   GET  /v1/health
 *   POST /v1/tools/call   {"name":"...","arguments":{...}}
 */
class OpenSCADAiBridge : public QObject
{
  Q_OBJECT

public:
  using ToolExecutor = std::function<std::string(const std::string& name, const std::string& arguments_json)>;

  static OpenSCADAiBridge& instance();

  void setToolExecutor(ToolExecutor executor);
  /*! Preferred listen port for the next start(); 0 = pick automatically. */
  void setDesiredPort(int port);
  bool start();
  void stop();
  bool restart();
  bool isRunning() const;
  QString baseUrl() const;
  int port() const { return listenPort; }
  QString lastError() const { return lastErrorText; }

  void resetSessionStats();
  int toolCallCount() const { return toolCalls.load(); }
  bool appliedCodeThisSession() const { return appliedCode.load(); }

private:
  explicit OpenSCADAiBridge(QObject *parent = nullptr);
  void onNewConnection();
  void handleClient(QTcpSocket *socket);
  void writeHttp(QTcpSocket *socket, int status, const QString& contentType, const QByteArray& body);
  QByteArray handleRequest(const QString& method, const QString& path, const QByteArray& body);

  void writeBridgeInfo();

  QTcpServer *server = nullptr;
  ToolExecutor executor;
  int listenPort = 0;
  int desiredPort = 0;
  QString lastErrorText;
  std::atomic<int> toolCalls{0};
  std::atomic<bool> appliedCode{false};
};
