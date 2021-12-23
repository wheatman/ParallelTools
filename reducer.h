#pragma once

#include "parallel.h"

#include <new>
#include <vector>

template <class F> class Reducer {

#ifdef __cpp_lib_hardware_interference_size
  using std::hardware_constructive_interference_size;
  using std::hardware_destructive_interference_size;
#else
  // 64 bytes on x86-64 │ L1_CACHE_BYTES │ L1_CACHE_SHIFT │ __cacheline_aligned
  // │
  // ...
  static constexpr std::size_t hardware_constructive_interference_size = 64;
  static constexpr std::size_t hardware_destructive_interference_size = 64;
#endif

  struct aligned_f {
    alignas(hardware_destructive_interference_size) F f;
  };
  std::vector<aligned_f> data;

public:
  Reducer(size_t num_workers) { data.resize(num_workers); }
  void update(F new_values) {
    int worker_num = getWorkerNum();
    data[worker_num].f.update(new_values);
  }
  F get() const {
    F output;
    for (const auto &d : data) {
      output.update(d.f);
    }
    return output;
  }
};

template <class T> class Reducer_sum {
  static_assert(std::is_integral<T>::value, "Integral required.");
  struct F {
    T value;
    void update(const F new_value) { value += new_value.value; }
    F(T t) : value(t) {}
    F() : value(0) {}
  };
  Reducer<F> reducer;

public:
  Reducer_sum(size_t num_workers) : reducer(num_workers) {}
  void add(T new_value) { reducer.update(new_value); }
  void inc() { reducer.update(1); }
  T get() const { return reducer.get().value; }
};

template <class T> class Reducer_max {
  struct F {
    T value;
    void update(const F new_value) { value = std::max(value, new_value.value); }
    F(T t) : value(t) {}
    F() : value(0) {}
  };
  Reducer<F> reducer;

public:
  Reducer_max(size_t num_workers) : reducer(num_workers) {}
  void update(T new_value) { reducer.update(new_value); }
  T get() const { return reducer.get().value; }
};