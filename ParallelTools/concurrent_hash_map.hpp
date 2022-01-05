#pragma once

#include "parallel.h"
#include "reducer.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ParallelTools {
template <class Key, class T, class Hash = std::hash<Key>,
          class KeyEqual = std::equal_to<Key>>
class concurrent_hash_map {
private:
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
  using Map = std::pair<std::unordered_map<Key, T, Hash, KeyEqual>, std::mutex>;
  struct aligned_map {
    alignas(hardware_destructive_interference_size) Map m;
  };
  std::vector<aligned_map> maps;

  static size_t bucket_hash(Key k) { return Hash{}(k) >> 32UL; }

  template <class U> size_t log2_up(U i) {
    size_t a = 0;
    U b = i - 1;
    while (b > 0) {
      b = b >> 1U;
      a++;
    }
    return a;
  }

public:
  concurrent_hash_map(int blow_up_factor = 10)
      : maps(1UL << log2_up(ParallelTools::getWorkers() * blow_up_factor)) {}

  std::pair<bool, T *> insert(Key k, T value) {
    size_t bucket = bucket_hash(k) % maps.size();
    maps[bucket].m.second.lock();
    auto pair = maps[bucket].m.first.insert({k, value});
    maps[bucket].m.second.unlock();
    return {pair.second, &(pair.first->second)};
  }

  void remove(Key k) {
    size_t bucket = bucket_hash(k) % maps.size();
    maps[bucket].m.second.lock();
    maps[bucket].m.first.erase(k);
    maps[bucket].m.second.unlock();
  }

  T value(Key k, T null_value) {
    size_t bucket = bucket_hash(k) % maps.size();
    maps[bucket].m.second.lock();
    auto it = maps[bucket].m.first.find(k);
    T value;
    if (it == maps[bucket].m.first.end()) {
      value = null_value;
    } else {
      value = it->second;
    }
    maps[bucket].m.second.unlock();
    return value;
  }
  bool contains(Key k) {
    size_t bucket = bucket_hash(k) % maps.size();
    maps[bucket].m.second.lock();
    bool has = maps[bucket].m.first.contains(k);
    maps[bucket].m.second.unlock();
    return has;
  }

  template <typename F> void for_each(F f) {
    ParallelTools::parallel_for(0, maps.size(), [&](size_t i) {
      for (auto &[key, value] : maps[i].m.first) {
        f(key, value);
      }
    });
  }
};
template <class Key, class T, class Hash = std::hash<Key>,
          class KeyEqual = std::equal_to<Key>>
class concurrent_hash_multimap {
private:
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
  using Map =
      std::pair<std::unordered_multimap<Key, T, Hash, KeyEqual>, std::mutex>;
  using iterator =
      typename std::unordered_multimap<Key, T, Hash, KeyEqual>::iterator;
  struct aligned_map {
    alignas(hardware_destructive_interference_size) Map m;
  };
  std::vector<aligned_map> maps;

  static size_t bucket_hash(Key k) { return Hash{}(k) >> 32UL; }

  template <class U> size_t log2_up(U i) {
    size_t a = 0;
    U b = i - 1;
    while (b > 0) {
      b = b >> 1U;
      a++;
    }
    return a;
  }

public:
  concurrent_hash_multimap(int blow_up_factor = 10)
      : maps(1UL << log2_up(ParallelTools::getWorkers() * blow_up_factor)) {}

  void insert(Key k, T value) {
    size_t bucket = bucket_hash(k) % maps.size();
    maps[bucket].m.second.lock();
    maps[bucket].m.first.insert({k, value});
    maps[bucket].m.second.unlock();
  }

  std::pair<iterator, iterator> equal_range(Key k) {
    size_t bucket = bucket_hash(k) % maps.size();
    maps[bucket].m.second.lock();
    auto p = maps[bucket].m.first.equal_range(k);
    maps[bucket].m.second.unlock();
    return p;
  }
};
} // namespace ParallelTools