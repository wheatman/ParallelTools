// This code is part of the Problem Based Benchmark Suite (PBBS)
// Copyright (c) 2010 Guy Blelloch and the PBBS team
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights (to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#ifndef _BENCH_UTILS_INCLUDED
#define _BENCH_UTILS_INCLUDED
#include "../parallel.h"
#include <algorithm>
#include <iostream>

// Needed to make frequent large allocations efficient with standard
// malloc implementation.  Otherwise they are allocated directly from
// vm.
#include <malloc.h>
static int __ii = mallopt(M_MMAP_MAX, 0);
static int __jj = mallopt(M_TRIM_THRESHOLD, -1);

namespace ParallelTools {

namespace utils {

// returns the log base 2 rounded up (works on ints or longs or unsigned
// versions)
template <class T> static int log2Up(T i) {
  int a = 0;
  T b = i - 1;
  while (b > 0) {
    b = b >> 1u;
    a++;
  }
  return a;
}

#if defined(MCX16)
// ET should be 128 bits and 128-bit aligned
template <class ET> inline bool CAS128(ET *a, ET b, ET c) {
  return __sync_bool_compare_and_swap_16((__int128 *)a, *((__int128 *)&b),
                                         *((__int128 *)&c));
}
#endif

// The conditional should be removed by the compiler
// this should work with pointer types, or pairs of integers
template <class ET> inline bool CAS(ET *ptr, ET oldv, ET newv) {
  if (sizeof(ET) == 1) {
    return __sync_bool_compare_and_swap_1((bool *)ptr, *((bool *)&oldv),
                                          *((bool *)&newv));
  } else if (sizeof(ET) == 8) {
    return __sync_bool_compare_and_swap_8((long *)ptr, *((long *)&oldv),
                                          *((long *)&newv));
  } else if (sizeof(ET) == 4) {
    return __sync_bool_compare_and_swap_4((int *)ptr, *((int *)&oldv),
                                          *((int *)&newv));
  }
#if defined(MCX16)
  else if (sizeof(ET) == 16) {
    return utils::CAS128(ptr, oldv, newv);
  }
#endif
  else {
    std::cout << "CAS bad length" << std::endl;
    abort();
  }
}

template <class ET> inline bool CAS_GCC(ET *ptr, ET oldv, ET newv) {
  if (sizeof(ET) == 4) {
    return __sync_bool_compare_and_swap((int *)ptr, *((int *)&oldv),
                                        *((int *)&newv));
  } else if (sizeof(ET) == 8) {
    return __sync_bool_compare_and_swap((long *)ptr, *((long *)&oldv),
                                        *((long *)&newv));
  }
#ifdef MCX16
  else if (sizeof(ET) == 16)
    return __sync_bool_compare_and_swap_16(
        (__int128 *)ptr, *((__int128 *)&oldv), *((__int128 *)&newv));
#endif
  else {
    std::cout << "CAS bad length" << std::endl;
    abort();
  }
}

template <class E> struct identityF {
  E operator()(const E &x) { return x; }
};

template <class E> struct addF {
  E operator()(const E &a, const E &b) const { return a + b; }
};

template <class E> struct maxF {
  E operator()(const E &a, const E &b) const { return (a > b) ? a : b; }
};

template <class E> struct minF {
  E operator()(const E &a, const E &b) const { return (a < b) ? a : b; }
};

template <class E1, class E2> struct firstF {
  E1 operator()(std::pair<E1, E2> a) { return a.first; }
};

template <class E1, class E2> struct secondF {
  E2 operator()(std::pair<E1, E2> a) { return a.second; }
};

} // namespace utils

} // namespace ParallelTools

#endif // _BENCH_UTILS_INCLUDED
