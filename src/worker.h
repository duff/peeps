#pragma once

#include <QThread>

#include <functional>
#include <utility>

namespace omacontacts {

// The global thread pool joins every task when the process exits. A Google
// request stuck inside Qt's network thread would then keep the process alive
// after its window is gone. These threads are not joined on the way out.
inline void runInBackground(std::function<void()> work) {
  QThread* thread = QThread::create(std::move(work));
  QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
  thread->start();
}

}  // namespace omacontacts
