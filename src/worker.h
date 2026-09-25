#pragma once

#include <QElapsedTimer>
#include <QThread>

#include <atomic>
#include <cstdlib>
#include <functional>
#include <utility>

namespace contacts {

// The global thread pool joins every task when the process exits. A Google
// request stuck inside Qt's network thread would then keep the process alive
// after its window is gone. These threads are not joined on the way out.
inline std::atomic<bool>& backgroundStopFlag() {
  static std::atomic<bool> stop{false};
  return stop;
}

inline std::atomic<int>& backgroundLiveCount() {
  static std::atomic<int> live{0};
  return live;
}

inline bool backgroundStopped() { return backgroundStopFlag().load(std::memory_order_acquire); }

inline void runInBackground(std::function<void()> work) {
  if (backgroundStopped()) return;
  backgroundLiveCount().fetch_add(1, std::memory_order_acq_rel);
  QThread* thread = QThread::create([work = std::move(work)] {
    struct Guard {
      ~Guard() { backgroundLiveCount().fetch_sub(1, std::memory_order_acq_rel); }
    } guard;
    if (backgroundStopped()) return;
    work();
  });
  QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
  thread->start();
}

// Call before QApplication is destroyed. A background Google request still
// inside Qt's TLS setup will abort if that plugin is torn down under it.
inline void finishBackground(int code) {
  backgroundStopFlag().store(true, std::memory_order_release);
  QElapsedTimer timer;
  timer.start();
  while (backgroundLiveCount().load(std::memory_order_acquire) > 0 && timer.elapsed() < 2000) QThread::msleep(20);
  if (backgroundLiveCount().load(std::memory_order_acquire) > 0) std::_Exit(code);
}

}  // namespace contacts
