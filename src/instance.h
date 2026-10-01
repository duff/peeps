#pragma once

#include <QLocalServer>
#include <QString>

#include <functional>

namespace contacts {

// A name in Linux's abstract socket namespace, one per user. It has no file,
// so nothing can delete it out from under a running window and a crash
// leaves nothing stale behind.
QString instanceSocketName();

// One process owns the contacts window. A later launch asks that process to
// come forward and then exits, instead of starting another sync. It passes
// along the XDG activation token its launcher gave it, which is what lets a
// Wayland window take focus.
class InstanceLock {
 public:
  using RaiseFn = std::function<void(const QString& activationToken)>;

  explicit InstanceLock(QString name);
  ~InstanceLock();

  InstanceLock(const InstanceLock&) = delete;
  InstanceLock& operator=(const InstanceLock&) = delete;

  // True when this process now owns the window. False after handing the
  // launch to the process that already does.
  bool claim(const RaiseFn& onRaise, const QString& activationToken = {});
  int raises() const { return raises_; }

 private:
  bool ping(const QString& activationToken);

  QString name_;
  QLocalServer server_;
  int raises_ = 0;
};

}  // namespace contacts
