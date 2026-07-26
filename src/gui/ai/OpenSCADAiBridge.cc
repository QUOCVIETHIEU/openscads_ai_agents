#include "gui/ai/OpenSCADAiBridge.h"

#include "json/json.hpp"
#include "platform/PlatformUtils.h"

#include <QByteArray>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <fstream>

namespace {

void writeSessionStats(int toolCalls, bool appliedCode)
{
  const std::string path = PlatformUtils::userConfigPath() + "/ai_bridge_session.json";
  try {
    nlohmann::json j = nlohmann::json::object();
    j["toolCalls"] = toolCalls;
    j["appliedCode"] = appliedCode;
    std::ofstream out(path);
    out << j.dump(2);
  } catch (...) {
  }
}

struct ClientBuffer {
  QByteArray data;
};

}  // namespace

OpenSCADAiBridge& OpenSCADAiBridge::instance()
{
  static OpenSCADAiBridge bridge;
  return bridge;
}

OpenSCADAiBridge::OpenSCADAiBridge(QObject *parent) : QObject(parent)
{
  server = new QTcpServer(this);
  connect(server, &QTcpServer::newConnection, this, &OpenSCADAiBridge::onNewConnection);
}

void OpenSCADAiBridge::setToolExecutor(ToolExecutor exec)
{
  executor = std::move(exec);
}

void OpenSCADAiBridge::setDesiredPort(int port)
{
  desiredPort = (port > 0 && port <= 65535) ? port : 0;
}

bool OpenSCADAiBridge::start()
{
  if (server->isListening()) return true;
  lastErrorText.clear();
  const auto port = static_cast<quint16>(desiredPort);
  if (!server->listen(QHostAddress::LocalHost, port)) {
    lastErrorText = server->errorString();
    if (desiredPort != 0) {
      lastErrorText = QStringLiteral("Port %1: %2").arg(desiredPort).arg(server->errorString());
    }
    return false;
  }
  listenPort = server->serverPort();
  writeBridgeInfo();
  writeSessionStats(0, false);
  return true;
}

void OpenSCADAiBridge::stop()
{
  if (server->isListening()) server->close();
  listenPort = 0;
  // Clear the published URL so external clients don't hit a stale endpoint.
  writeBridgeInfo();
}

bool OpenSCADAiBridge::restart()
{
  if (server->isListening()) server->close();
  listenPort = 0;
  return start();
}

void OpenSCADAiBridge::writeBridgeInfo()
{
  const std::string path = PlatformUtils::userConfigPath() + "/ai_bridge.json";
  try {
    nlohmann::json j = nlohmann::json::object();
    j["url"] = isRunning() ? baseUrl().toStdString() : std::string();
    j["port"] = listenPort;
    std::ofstream out(path);
    out << j.dump(2);
  } catch (...) {
  }
}

bool OpenSCADAiBridge::isRunning() const
{
  return server && server->isListening();
}

QString OpenSCADAiBridge::baseUrl() const
{
  return QStringLiteral("http://127.0.0.1:%1").arg(listenPort);
}

void OpenSCADAiBridge::resetSessionStats()
{
  toolCalls.store(0);
  appliedCode.store(false);
  writeSessionStats(0, false);
}

void OpenSCADAiBridge::onNewConnection()
{
  while (server->hasPendingConnections()) {
    QTcpSocket *socket = server->nextPendingConnection();
    if (!socket) continue;
    auto *buf = new ClientBuffer();
    connect(socket, &QTcpSocket::readyRead, this, [this, socket, buf]() {
      buf->data += socket->readAll();
      const int headerEnd = buf->data.indexOf("\r\n\r\n");
      if (headerEnd < 0) return;

      const QByteArray headers = buf->data.left(headerEnd);
      int contentLength = 0;
      for (const QByteArray& line : headers.split('\n')) {
        const QByteArray t = line.trimmed();
        if (t.toLower().startsWith("content-length:")) {
          contentLength = t.mid(15).trimmed().toInt();
        }
      }
      const int totalNeeded = headerEnd + 4 + contentLength;
      if (buf->data.size() < totalNeeded) return;

      const QByteArray raw = buf->data.left(totalNeeded);
      buf->data.remove(0, totalNeeded);

      const int firstLineEnd = raw.indexOf("\r\n");
      if (firstLineEnd < 0) {
        writeHttp(socket, 400, QStringLiteral("application/json"),
                  QByteArrayLiteral("{\"ok\":false,\"error\":\"bad request\"}"));
        return;
      }
      const QByteArray firstLine = raw.left(firstLineEnd);
      const QList<QByteArray> parts = firstLine.split(' ');
      if (parts.size() < 2) {
        writeHttp(socket, 400, QStringLiteral("application/json"),
                  QByteArrayLiteral("{\"ok\":false,\"error\":\"bad request line\"}"));
        return;
      }
      const QString method = QString::fromUtf8(parts[0]);
      const QString path = QString::fromUtf8(parts[1]);
      const QByteArray body = raw.mid(headerEnd + 4, contentLength);

      const QByteArray response = handleRequest(method, path, body);
      writeHttp(socket, 200, QStringLiteral("application/json"), response);
    });
    connect(socket, &QTcpSocket::disconnected, this, [socket, buf]() {
      delete buf;
      socket->deleteLater();
    });
  }
}

void OpenSCADAiBridge::writeHttp(QTcpSocket *socket, int status, const QString& contentType,
                                 const QByteArray& body)
{
  QByteArray reason = (status == 200) ? "OK" : (status == 400) ? "Bad Request" : "Error";
  QByteArray header;
  header += "HTTP/1.1 " + QByteArray::number(status) + " " + reason + "\r\n";
  header += "Content-Type: " + contentType.toUtf8() + "\r\n";
  header += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
  header += "Connection: close\r\n\r\n";
  socket->write(header);
  socket->write(body);
  socket->disconnectFromHost();
}

QByteArray OpenSCADAiBridge::handleRequest(const QString& method, const QString& path,
                                           const QByteArray& body)
{
  nlohmann::json out = nlohmann::json::object();

  if (method == QStringLiteral("GET") && path == QStringLiteral("/v1/health")) {
    out["ok"] = true;
    out["service"] = "openscad-ai-bridge";
    return QByteArray::fromStdString(out.dump());
  }

  if (method == QStringLiteral("POST") && path == QStringLiteral("/v1/tools/call")) {
    if (!executor) {
      out["ok"] = false;
      out["error"] = "No tool executor registered (AI chat not ready).";
      return QByteArray::fromStdString(out.dump());
    }
    try {
      auto req = nlohmann::json::parse(body.isEmpty() ? "{}" : body.toStdString());
      const std::string name = req.value("name", "");
      nlohmann::json args = nlohmann::json::object();
      if (req.contains("arguments")) {
        if (req["arguments"].is_string()) {
          try {
            args = nlohmann::json::parse(req["arguments"].get<std::string>());
          } catch (...) {
            args = nlohmann::json::object();
          }
        } else if (req["arguments"].is_object()) {
          args = req["arguments"];
        }
      }
      if (name.empty()) {
        out["ok"] = false;
        out["error"] = "Missing tool name.";
        return QByteArray::fromStdString(out.dump());
      }

      const std::string result = executor(name, args.dump());
      const int calls = toolCalls.fetch_add(1) + 1;
      if (name == "set_editor_code") appliedCode.store(true);
      writeSessionStats(calls, appliedCode.load());

      out["ok"] = true;
      out["result"] = result;
      return QByteArray::fromStdString(out.dump());
    } catch (const std::exception& e) {
      out["ok"] = false;
      out["error"] = e.what();
      return QByteArray::fromStdString(out.dump());
    } catch (...) {
      out["ok"] = false;
      out["error"] = "Unknown error in tool call.";
      return QByteArray::fromStdString(out.dump());
    }
  }

  out["ok"] = false;
  out["error"] = "Not found";
  return QByteArray::fromStdString(out.dump());
}
