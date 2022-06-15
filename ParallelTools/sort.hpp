#pragma once

#include "parallel.h"
#include <algorithm>
#include <functional>

#include <hwy/contrib/sort/vqsort.h>
#include <iterator>

namespace ParallelTools {

template <class InputIt, class OutputIt, class Compare = std::less<>>
void merge(InputIt first1, InputIt last1, InputIt first2, InputIt last2,
           OutputIt d_first, Compare comp = std::less<>()) {
  /*
algorithm merge(A[i...j], B[k...ℓ], C[p...q]) is
inputs A, B, C : array
i, j, k, ℓ, p, q : indices

let m = j - i,
n = ℓ - k

if m < n then
swap A and B  // ensure that A is the larger array: i, j still belong to A; k, ℓ
to B swap m and n

if m ≤ 0 then
return  // base case, nothing to merge

let r = ⌊(i + j)/2⌋
let s = binary-search(A[r], B[k...ℓ])
let t = p + (r - i) + (s - k)
C[t] = A[r]

in parallel do
merge(A[i...r], B[k...s], C[p...t])
merge(A[r+1...j], B[s...ℓ], C[t+1...q])
  */
  size_t first_length = last1 - first1;
  size_t second_length = last2 - first2;

  InputIt longer_start = (first_length >= second_length) ? first1 : first2;
  InputIt longer_last = (first_length >= second_length) ? last1 : last2;

  InputIt shorter_start = (first_length < second_length) ? first1 : first2;
  InputIt shorter_last = (first_length < second_length) ? last1 : last2;
  if (shorter_last - shorter_start < 10000) {
    std::merge(first1, last1, first2, last2, d_first, comp);
    return;
  }
  InputIt r = longer_start + ((longer_last - longer_start) / 2);
  InputIt s = std::lower_bound(shorter_start, shorter_last, *r, comp);
  OutputIt out_mid = d_first + (r - longer_start) + (s - shorter_start);
  *out_mid = *r;
  cilk_spawn ParallelTools::merge(longer_start, r, shorter_start, s, d_first,
                                  comp);
  ParallelTools::merge(r + 1, longer_last, s, shorter_last, out_mid + 1, comp);
  cilk_sync;
}

// TODO(wheatman) make a better parallel sort
template <class RandomIt, class Compare = std::less<>>
void sort(RandomIt first, RandomIt last, Compare comp = std::less<>()) {
#if CILK != 1
  return std::sort(first, last, comp);
#endif
  using E = typename std::iterator_traits<RandomIt>::value_type;
  if (last - first < 10000) {
    std::sort(first, last, comp);
  } else {
    RandomIt mid = first + ((last - first) / 2);
    cilk_spawn ParallelTools::sort(first, mid, comp);
    ParallelTools::sort(mid, last, comp);
    E *tmp = (E *)malloc((last - first) * sizeof(E));
    cilk_sync;
    merge(first, mid, mid, last, tmp, comp);
    ParallelTools::parallel_for(0, last - first,
                                [&](size_t i) { first[i] = tmp[i]; });
    free(tmp);
  }
}

template <class RandomIt, class UnaryPredicate>
RandomIt partition(RandomIt first, RandomIt last, UnaryPredicate p,
                   size_t level = 0) {
  if (std::distance(first, last) < 100000) {
    return std::partition(first, last, p);
  }
  auto middle = std::next(first, std::distance(first, last) / 2);
  auto left_break =
      cilk_spawn ParallelTools::partition(first, middle, p, level + 1);
  auto right_break = ParallelTools::partition(middle, last, p, level + 1);
  cilk_sync;
  auto new_middle = left_break + (right_break - middle);
  auto flip_point =
      std::next(left_break, std::distance(left_break, right_break) / 2);
  size_t flip_length = std::distance(left_break, flip_point);
  cilk_for(size_t i = 0; i < flip_length; i++) {
    std::swap(left_break[i], right_break[-i - 1]);
  }
  return new_middle;
}

template <class RandomIt, class Compare = std::less<>>
void qsort(RandomIt first, RandomIt last, Compare comp = std::less<>(),
           size_t level = 0) {
#if CILK != 1
  return std::sort(first, last, comp);
#endif
  if (last - first < 10000 || level > 128) {
    std::sort(first, last, comp);
  } else {
    auto pivot = *std::next(first, std::distance(first, last) / 2);
    RandomIt mid = ParallelTools::partition(
        first, last, [pivot, comp](const auto &em) { return comp(em, pivot); },
        level);
    cilk_spawn ParallelTools::qsort(first, mid, comp, level + 1);
    ParallelTools::qsort(mid, last, comp, level + 1);
    cilk_sync;
  }
}

template <class RandomIt> void isort_asc(RandomIt first, RandomIt last) {
#if CILK != 1
  return std::sort(first, last);
#endif
  using E = typename std::iterator_traits<RandomIt>::value_type;
  if (last - first < 100000) {
    hwy::Sorter sorter;
    sorter(first, last - first, hwy::SortAscending());
  } else {
    RandomIt mid = first + ((last - first) / 2);
    cilk_spawn ParallelTools::isort_asc(first, mid);
    ParallelTools::isort_asc(mid, last);
    E *tmp = (E *)malloc((last - first) * sizeof(E));
    cilk_sync;
    merge(first, mid, mid, last, tmp);
    ParallelTools::parallel_for(0, last - first,
                                [&](size_t i) { first[i] = tmp[i]; });
    free(tmp);
  }
}

} // namespace ParallelTools