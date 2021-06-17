/******************************************************************************
    Copyright (C) Martin Karsten 2015-2021

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/
#ifndef _Poller_h_
#define _Poller_h_ 1

#include "runtime/Debug.h"
#include "runtime/Stats.h"

class BaseProcessor;

#include <pthread.h>
#include <unistd.h>      // close
#if __FreeBSD__
#include <sys/event.h>
#else // __linux__ below
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#endif

class EventScope;
class Fibre;
class Fred;

struct Poller {
#if __FreeBSD__
  typedef struct kevent EventType;
  enum Operation : ssize_t { Create = EV_ADD, Modify = EV_ADD, Remove = EV_DELETE };
  enum Direction : ssize_t { Input = EVFILT_READ, Output = EVFILT_WRITE };
  enum Variant   : ssize_t { Level = 0, Edge = EV_CLEAR, Oneshot = EV_ONESHOT };
#else // __linux__ below
  typedef epoll_event   EventType; // man 2 epoll_ctl: EPOLLERR, EPOLLHUP not needed
  enum Operation : ssize_t { Create = EPOLL_CTL_ADD, Modify = EPOLL_CTL_MOD, Remove = EPOLL_CTL_DEL };
  enum Direction : ssize_t { Input = EPOLLIN | EPOLLPRI | EPOLLRDHUP, Output = EPOLLOUT };
  enum Variant   : ssize_t { Level = 0, Edge = EPOLLET, Oneshot = EPOLLONESHOT };
#endif
};

class BasePoller : public Poller {
protected:
  static const int maxPoll = 1024;
  EventType     events[maxPoll];
  int           pollFD;

  EventScope&   eventScope;
  volatile bool pollTerminate;

  PollerStats* stats;

  template<bool Blocking>
  inline int doPoll();

  template<bool Enqueue = true>
  inline Fred* notifyOne(EventType& ev);

  inline void notifyAll(int evcnt);

public:
  BasePoller(EventScope& es, cptr_t parent, const char* n = "BasePoller") : eventScope(es), pollTerminate(false) {
    stats = new PollerStats(this, parent, n);
#if __FreeBSD__
    pollFD = SYSCALLIO(kqueue());
#else // __linux__ below
    pollFD = SYSCALLIO(epoll_create1(EPOLL_CLOEXEC));
#endif
    DBG::outl(DBG::Level::Polling, "Poller ", FmtHex(this), " create ", pollFD);
  }

  ~BasePoller() {
    SYSCALL(close(pollFD));
  }

  void setupFD(int fd, Operation op, Direction dir, Variant var) {
    DBG::outl(DBG::Level::Polling, "Poller ", FmtHex(this), " setup ", fd, " at ", pollFD, " with ", op, '/', dir, '/', var);
    stats->regs.count(op != Remove);
#if __FreeBSD__
    struct kevent ev;
    EV_SET(&ev, fd, dir, op | (op == Remove ? 0 : var), 0, 0, 0);
    SYSCALL(kevent(pollFD, &ev, 1, nullptr, 0, nullptr));
#else // __linux__ below
    epoll_event ev;
    ev.events = dir | var;
    ev.data.fd = fd;
    SYSCALL(epoll_ctl(pollFD, op, fd, op == Remove ? nullptr : &ev));
#endif
  }
};

class PollerFibre : public BasePoller {
  Fibre* pollFibre;
  inline void pollLoop();
  static void pollLoopSetup(PollerFibre*);

public:
  PollerFibre(EventScope&, BaseProcessor&, cptr_t parent, bool cluster = true);
  ~PollerFibre();
  void start();
};

class BaseThreadPoller : public BasePoller {
  pthread_t pollThread;

protected:
  BaseThreadPoller(EventScope& es, cptr_t parent, const char* n) : BasePoller(es, parent, n) {}
  void start(void *(*loopSetup)(void*)) {
    SYSCALL(pthread_create(&pollThread, nullptr, loopSetup, this));
  }

  template<typename T>
  static inline void pollLoop(T& This);

public:
  pthread_t getSysThreadId() { return pollThread; }
  void terminate(_friend<EventScope>);
};

class PollerThread : public BaseThreadPoller {
  static void* pollLoopSetup(void*);

public:
  PollerThread(EventScope& es, BaseProcessor&, cptr_t parent) : BaseThreadPoller(es, parent, "PollerThread") {}
  void prePoll(_friend<BaseThreadPoller>) {}
  void start() { BaseThreadPoller::start(pollLoopSetup); }
};

class MasterPoller : public BaseThreadPoller {
  int timerFD;
  static void* pollLoopSetup(void*);

public:
#if __FreeBSD__
  static const int extraTimerFD = 1;
#else
  static const int extraTimerFD = 0;
#endif

  MasterPoller(EventScope& es, unsigned long fdCount, _friend<EventScope>) : BaseThreadPoller(es, &es, "MasterPoller") {
    BaseThreadPoller::start(pollLoopSetup);
#if __FreeBSD__
    timerFD = fdCount - 1;
#else
    timerFD = SYSCALLIO(timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK | TFD_CLOEXEC));
    setupFD(timerFD, Create, Input, Edge);
#endif
  }

#if __linux__
  ~MasterPoller() { SYSCALL(close(timerFD)); }
#endif

  inline void prePoll(_friend<BaseThreadPoller>);

  void setTimer(const Time& timeout) {
#if __FreeBSD__
    struct kevent ev;
    EV_SET(&ev, timerFD, EVFILT_TIMER, EV_ADD | EV_ONESHOT, NOTE_USECONDS | NOTE_ABSTIME, timeout.toUS(), 0);
    SYSCALL(kevent(pollFD, &ev, 1, nullptr, 0, nullptr));
#else
    itimerspec tval = { {0,0}, timeout };
    SYSCALL(timerfd_settime(timerFD, TFD_TIMER_ABSTIME, &tval, nullptr));
#endif
  }
};

#endif /* _Poller_h_ */
