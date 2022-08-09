#include <algorithm>
#include <atomic>
#include <inttypes.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define num_tries 3
class Lock {
  std::atomic<bool> flag;

public:
  bool try_lock() {
    bool value = false;
    return flag.compare_exchange_strong(value, true);
  }
  void lock() {
    int tries = 0;
    bool success = false;

    while (!success) {
      tries = 0;
      while (tries < num_tries) {
        bool value = false;
        success = flag.compare_exchange_weak(value, true);

        if (success) {
          return;
        } else {
          tries++;
        }
      }
      if (!success) {
        sched_yield();
      }
    }
  }
  void unlock() { flag = false; }
};

class local_counter {
public:
  std::atomic<int64_t> counter = 0;
  int64_t padding[7];
};

template <int num_counters = 8, int threshold = 8> class partitioned_counter {
  local_counter *local_counters;
  std::atomic<int64_t> *global_counter;

public:
  partitioned_counter(std::atomic<int64_t> *global_counter_)
      : global_counter(global_counter_) {
    local_counters = new local_counter[num_counters];
    return;
  }

  void sync() {
    for (uint32_t i = 0; i < num_counters; i++) {
      int64_t c = local_counters[i].counter.exchange(0);
      *global_counter += c;
    }
  }

  void add(int64_t count, uint8_t counter_id) {
    counter_id = counter_id % num_counters;
    int64_t cur_count =
        local_counters[counter_id].counter.fetch_add(count) + count;
    if (cur_count > threshold || cur_count < -threshold) {
      int64_t c = local_counters[counter_id].counter.exchange(0);
      *global_counter += c;
    }
  }

  ~partitioned_counter() {
    sync();
    delete[] local_counters;
  }
};

class ReaderWriterLock {

public:
  ReaderWriterLock() : readers(0), writer(0), pc_counter(&readers) {}

  /**
   * Try to acquire a lock and spin until the lock is available.
   */
  void read_lock(int cpuid = -1) {

    pc_counter.add(1, cpuid);

    while (writer.test()) {
      pc_counter.add(-1, cpuid);
      writer.wait(true);
      pc_counter.add(1, cpuid);
    }
  }

  void read_unlock(int cpuid) {
    pc_counter.add(-1, cpuid);
    return;
  }

  /**
   * Try to acquire a write lock and spin until the lock is available.
   * Then wait till reader count is 0.
   */
  void write_lock() {
    // acquire write lock.
    writer.wait(true);
    while (writer.test_and_set()) {
    }
    // wait for readers to finish
    do {
      pc_counter.sync();
    } while (readers);
  }

  bool try_upgrade_release_on_fail(int cpuid) {
    // acquire write lock.

    if (writer.test_and_set()) {
      pc_counter.add(-1, cpuid);
      return false;
    }

    pc_counter.add(-1, cpuid);

    // wait for readers to finish
    do {
      pc_counter.sync();
    } while (readers);

    return true;
  }

  void write_unlock(void) {
    writer.clear();
    writer.notify_all();
    return;
  }

private:
  std::atomic<int64_t> readers;
  std::atomic_flag writer;
  partitioned_counter<8, 8> pc_counter;
};