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

#ifdef __cplusplus
extern "C" {
#endif

typedef struct local_counter {
  int64_t counter;
  int64_t padding[7];
} local_counter;

typedef struct local_counter lctr_t;

typedef struct partitioned_counter {
  lctr_t *local_counters;
  int64_t *global_counter;
  uint32_t num_counters;
  int32_t threshold;
} partitioned_counter;

typedef struct partitioned_counter pc_t;

#define PC_ERROR -1

/* on success returns 0.
 * If allocation fails returns PC_ERROR
 */
int pc_init(pc_t *pc, int64_t *global_counter, uint32_t num_counters,
            int32_t threshold) {
  uint32_t num_cpus = (int)sysconf(_SC_NPROCESSORS_ONLN);
  if (num_cpus < 0) {
    perror("sysconf");
    return PC_ERROR;
  }
  pc->num_counters =
      num_counters == 0 ? num_cpus : std::min(num_cpus, num_counters);

  pc->local_counters =
      (lctr_t *)calloc(pc->num_counters, sizeof(*pc->local_counters));
  if (pc->local_counters == NULL) {
    perror("Couldn't allocate memory for local counters.");
    return PC_ERROR;
  }
  /*printf("Padding check: 0: %p 1: %p\n", (void*)&pc->local_counters[0],*/
  /*(void*)&pc->local_counters[1]);*/
  pc->global_counter = global_counter;
  pc->threshold = threshold;

  return 0;
}

inline void pc_sync(pc_t *pc) {
  for (uint32_t i = 0; i < pc->num_counters; i++) {
    int64_t c = __atomic_exchange_n(&pc->local_counters[i].counter, 0,
                                    __ATOMIC_SEQ_CST);
    __atomic_fetch_add(pc->global_counter, c, __ATOMIC_SEQ_CST);
  }
}

inline void pc_destructor(pc_t *pc) {
  pc_sync(pc);
  lctr_t *lc = pc->local_counters;
  pc->local_counters = NULL;
  free(lc);
}

inline void pc_add(pc_t *pc, int64_t count, int cpuid = -1) {
  if (cpuid == -1) {
    cpuid = sched_getcpu();
  }
  uint32_t counter_id = cpuid % pc->num_counters;
  int64_t cur_count = __atomic_add_fetch(
      &pc->local_counters[counter_id].counter, count, __ATOMIC_SEQ_CST);
  if (cur_count > pc->threshold || cur_count < -pc->threshold) {
    int64_t new_count = __atomic_exchange_n(
        &pc->local_counters[counter_id].counter, 0, __ATOMIC_SEQ_CST);
    __atomic_fetch_add(pc->global_counter, new_count, __ATOMIC_SEQ_CST);
  }
}

#ifdef __cplusplus
}
#endif

#define PF_NO_LOCK (0x01)
#define PF_TRY_ONCE_LOCK (0x02)
#define PF_WAIT_FOR_LOCK (0x04)

#define GET_PF_NO_LOCK(flag) (flag & PF_NO_LOCK)
#define GET_PF_TRY_ONCE_LOCK(flag) (flag & PF_TRY_ONCE_LOCK)
#define GET_PF_WAIT_FOR_LOCK(flag) (flag & PF_WAIT_FOR_LOCK)

class SpinLock {
public:
  SpinLock() { locked = 0; }

  /**
   * Try to acquire a lock once and return even if the lock is busy.
   * If spin flag is set, then spin until the lock is available.
   */
  bool lock(uint8_t flag) {
    if (GET_PF_WAIT_FOR_LOCK(flag) != PF_WAIT_FOR_LOCK) {
      return !__sync_lock_test_and_set(&locked, 1);
    } else {
      while (__sync_lock_test_and_set(&locked, 1))
        while (locked)
          ;
      return true;
    }

    return false;
  }

  void unlock(void) {
    __sync_lock_release(&locked);
    return;
  }

private:
  volatile int locked;
};

class ReaderWriterLock {
public:
  ReaderWriterLock() : readers(0), writer(0) {
    pc_init(&pc_counter, &readers, 8, 8);
  }

  /**
   * Try to acquire a lock and spin until the lock is available.
   */
  bool read_lock(uint8_t flag, int cpuid = -1) {
    if (GET_PF_WAIT_FOR_LOCK(flag) != PF_WAIT_FOR_LOCK) {
      if (writer == 0) {
        pc_add(&pc_counter, 1, cpuid);
        return true;
      }
    } else {
      while (writer != 0)
        ;
      pc_add(&pc_counter, 1, cpuid);
      return true;
    }

    return false;
  }

  void read_unlock(int cpuid = -1) {
    pc_add(&pc_counter, -1, cpuid);
    return;
  }

  /**
   * Try to acquire a write lock and spin until the lock is available.
   * Then wait till reader count is 0.
   */
  bool write_lock(uint8_t flag) {
    // acquire write lock.
    if (GET_PF_WAIT_FOR_LOCK(flag) != PF_WAIT_FOR_LOCK) {
      if (__sync_lock_test_and_set(&writer, 1))
        return false;
    } else {
      while (__sync_lock_test_and_set(&writer, 1))
        while (writer != 0)
          ;
    }
    // wait for readers to finish
    do {
      pc_sync(&pc_counter);
    } while (readers);

    return true;
  }

  bool try_upgrade_release_on_fail(int cpuid = -1) {
    // acquire write lock.

    if (__sync_lock_test_and_set(&writer, 1)) {
      pc_add(&pc_counter, -1, cpuid);
      return false;
    }

    pc_add(&pc_counter, -1, cpuid);

    // wait for readers to finish
    do {
      pc_sync(&pc_counter);
    } while (readers);

    return true;
  }

  void write_unlock(void) {
    __sync_lock_release(&writer);
    return;
  }

private:
  int64_t readers;
  volatile int writer;
  pc_t pc_counter;
};