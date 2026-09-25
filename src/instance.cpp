#include "instance.h"

#include <QDir>
#include <QEventLoop>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QTimer>

namespace contacts {

QString instanceSocketName() {
  QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
  if (dir.isEmpty()) dir = QDir::tempPath();
  QDir().mkpath(dir);
  return dir + QStringLiteral("/contacts.sock");
}

InstanceLock::InstanceLock(QString name) : name_(std::move(name)) {
  server_.setSocketOptions(QLocalServer::UserAccessOption);
}

InstanceLock::~InstanceLock() {
  server_.close();
  if (listening_) QLocalServer::removeServer(name_);
}

bool InstanceLock::claim(const std::function<void()>& onRaise) {
  auto ping = [this] {
    QLocalSocket probe;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&probe, &QLocalSocket::connected, &loop, &QEventLoop::quit);
    QObject::connect(&probe, &QLocalSocket::errorOccurred, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    probe.connectToServer(name_);
    timer.start(1000);
    if (probe.state() != QLocalSocket::ConnectedState) loop.exec();
    if (probe.state() != QLocalSocket::ConnectedState) return false;
    probe.write("raise\n");
    probe.flush();
    QEventLoop reply;
    QTimer replyTimer;
    replyTimer.setSingleShot(true);
    QObject::connect(&probe, &QLocalSocket::readyRead, &reply, &QEventLoop::quit);
    QObject::connect(&replyTimer, &QTimer::timeout, &reply, &QEventLoop::quit);
    replyTimer.start(1000);
    if (probe.bytesAvailable() == 0) reply.exec();
    return true;
  };
  if (ping()) return false;

  QLocalServer::removeServer(name_);
  if (!server_.listen(name_)) return ping() ? false : true;
  listening_ = true;

  QObject::connect(&server_, &QLocalServer::newConnection, &server_, [this, onRaise] {
    while (QLocalSocket* socket = server_.nextPendingConnection()) {
      QObject::connect(socket, &QLocalSocket::readyRead, socket, [this, socket, onRaise] {
        if (!socket->readAll().contains("raise")) return;
        ++raises_;
        if (onRaise) onRaise();
        socket->write("ok\n");
        socket->flush();
        socket->disconnectFromServer();
        socket->deleteLater();
      });
    }
  });
  return true;
}

}  // namespace contacts
