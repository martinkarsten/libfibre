/******************************************************************************
    Copyright (C) Martin Karsten 2015-2019

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
#ifndef _Cluster_h_
#define _Cluster_h_ 1

#include "runtime/BlockingSync.h"
#include "runtime/Scheduler.h"
#include "libfibre/OsProcessor.h"
#include "libfibre/Poller.h"

/**
A Cluster object provides a scheduling scope and uses processors (pthreads)
to execute fibres.  It also manages I/O pollers and provides a
stop-the-world pause mechanism.
*/
class Cluster : public Scheduler {
  EventScope&    scope;

#if TESTING_CLUSTER_POLLER_FIBRE
  typedef PollerFibre  PollerType;
#else
  typedef PollerThread PollerType;
#endif
  PollerType*    pollVec;
  size_t         pollCount;

  BaseProcessor*  pauseProc;
  TaskSemaphore   pauseSem;
  WorkerSemaphore confirmSem;
  WorkerSemaphore sleepSem;

  ClusterStats*  stats;

  void start() {
    for (size_t p = 0; p < pollCount; p += 1) pollVec[p].start();
  }

  Cluster(EventScope& es, size_t p, _friend<Cluster>) : scope(es), pollCount(p), pauseProc(nullptr) {
    stats = new ClusterStats(this);
    pollVec = (PollerType*)new char[sizeof(PollerType[pollCount])];
    for (size_t p = 0; p < pollCount; p += 1) new (&pollVec[p]) PollerType(scope, stagingProc);
  }

public:
  /** Constructor: create Cluster in current EventScope. */
  Cluster(size_t pollerCount = 1) : Cluster(Context::CurrEventScope(), pollerCount) {}
  /** Constructor: create Cluster in specfied EventScope. */
  Cluster(EventScope& es, size_t pollerCount = 1) : Cluster(es, pollerCount, _friend<Cluster>()) { start(); }

  // special constructor and start routine for bootstrapping event scope
  Cluster(EventScope& es, size_t pollerCount, _friend<EventScope>) : Cluster(es, pollerCount, _friend<Cluster>()) {}
  void startPolling(_friend<EventScope>) { start(); }

  void addWorkers(size_t cnt) {
    for (size_t i = 0; i < cnt; i += 1) {
      new OsProcessor(*this, _friend<Cluster>());
      // add processor to ring, then start?
    }
  }

  void registerWorker(funcvoid1_t initFunc = nullptr, ptr_t arg = nullptr, bool idle = true) {
    // TODO
  }

  size_t getWorkerSysIDs(pthread_t* tid = nullptr, size_t cnt = 0) {
    ScopedLock<WorkerLock> sl(ringLock);
    BaseProcessor* p = placeProc;
    for (size_t i = 0; i < cnt && i < ringCount; i += 1) {
      tid[i] = reinterpret_cast<OsProcessor*>(p)->getSysID();
      p = ProcessorRing::next(*p);
    }
    return ringCount;
  }

  EventScope& getEventScope() { return scope; }
  PollerType& getPoller(size_t hint) { return pollVec[hint % pollCount]; }
  size_t getPollerCount() { return pollCount; }

  /** Pause all OsProcessors (except caller). */
  void pause() {
    ringLock.acquire();
    stats->procs.count(ringCount);
    pauseProc = &Context::CurrProcessor();
    for (size_t p = 1; p < ringCount; p += 1) pauseSem.V();
    for (size_t p = 1; p < ringCount; p += 1) confirmSem.P();
  }

  /** Pause all OsProcessors. */
  void resume() {
    for (size_t p = 1; p < ringCount; p += 1) sleepSem.V();
    ringLock.release();
  }

  static void maintenance(Cluster* cl);
};

#endif /* _Cluster_h_ */
