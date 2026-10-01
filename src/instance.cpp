#include "instance.h"

#include <QEventLoop>
#include <QLocalSocket>
#include <QTimer>

#include <unistd.h>

namespace contacts {

QString instanceSocketName() { return QStringLiteral("org.omelia.contacts-%1").arg(getuid()); }

InstanceLock::InstanceLock(QString name) : name_(std::move(name)) {
  server_.setSocketOptions(QLocalServer::AbstractNamespaceOption);
}

InstanceLock::~InstanceLock() { server_.close(); }

bool InstanceLock::ping(const QString& activationToken) {
  QLocalSocket probe;
  probe.setSocketOptions(QLocalSocket::AbstractNamespaceOption);
  QEventLoop loop;
  QTimer timer;
  timer.setSingleShot(true);
  QObject::connect(&probe, &QLocalSocket::connected, &loop, &QEventLoop::quit);
  QObject::connect(&probe, &QLocalSocket::errorOccurred, &loop, &QEventLoop::quit);
  QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
  probe.connectToServer(name_);
  timer.start(1000);
  if (probe.state() == QLocalSocket::ConnectingState) loop.exec();
  if (probe.state() != QLocalSocket::ConnectedState) return false;
  probe.write("raise " + activationToken.toUtf8() + "\n");
  probe.flush();
  QEventLoop reply;
  QTimer replyTimer;
  replyTimer.setSingleShot(true);
  QObject::connect(&probe, &QLocalSocket::readyRead, &reply, &QEventLoop::quit);
  QObject::connect(&probe, &QLocalSocket::disconnected, &reply, &QEventLoop::quit);
  QObject::connect(&replyTimer, &QTimer::timeout, &reply, &QEventLoop::quit);
  replyTimer.start(1000);
  if (probe.bytesAvailable() == 0) reply.exec();
  return true;
}

bool InstanceLock::claim(const RaiseFn& onRaise, const QString& activationToken) {
  // Two launches at once both find nobody. Only one of them can listen, and
  // the other asks that one on its second pass.
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (ping(activationToken)) return false;
    if (server_.listen(name_)) break;
  }
  if (!server_.isListening()) return true;

  QObject::connect(&server_, &QLocalServer::newConnection, &server_, [this, onRaise] {
    while (QLocalSocket* socket = server_.nextPendingConnection()) {
      QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
      QObject::connect(socket, &QLocalSocket::readyRead, socket, [this, socket, onRaise] {
        if (!socket->canReadLine()) return;
        const QByteArray line = socket->readLine().trimmed();
        if (!line.startsWith("raise")) return;
        ++raises_;
        if (onRaise) onRaise(QString::fromUtf8(line.mid(5).trimmed()));
        socket->write("ok\n");
        socket->flush();
        socket->disconnectFromServer();
      });
    }
  });
  return true;
}

}  // namespace contacts
