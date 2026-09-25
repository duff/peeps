#pragma once

#include <QLocalServer>
#include <QString>

#include <functional>

namespace contacts {

QString instanceSocketName();

// One process owns the contacts window. A later launch asks that process to
// come forward and then exits, instead of starting another sync.
class InstanceLock {
 public:
  explicit InstanceLock(QString name);
  ~InstanceLock();

  InstanceLock(const InstanceLock&) = delete;
  InstanceLock& operator=(const InstanceLock&) = delete;

  bool claim(const std::function<void()>& onRaise);
  int raises() const { return raises_; }

 private:
  QString name_;
  QLocalServer server_;
  int raises_ = 0;
  bool listening_ = false;
};

}  // namespace contacts
