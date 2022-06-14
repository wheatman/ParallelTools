#pragma once

#include "parallel.h"
#include <vector>

namespace ParallelTools {

/*
void prefix(size_t* p) {
__m512i x = _mm512_loadu_si512((__m512i*)p);
__m512i zero = _mm512_setzero();
__m512i shift1 = _mm512_alignr_epi64(x, zero, 7);
x = _mm512_add_epi64(x, shift1);
__m512i shift2 = _mm512_alignr_epi64(x, zero, 6);
x = _mm512_add_epi64(x, shift2);

__m512i shift4 = _mm512_alignr_epi64(x, zero, 4);
x = _mm512_add_epi64(x, shift4);
_mm512_storeu_si512(p, x);
}

__m512i accumulate(size_t* p, __m512i s) {
__m512i d = _mm512_set1_epi64(p[7]);
__m512i x = _mm512_loadu_si512((__m512i*)p);
x = _mm512_add_epi64(s, x);
_mm512_storeu_si512(p, x);
return _mm512_add_epi64(s, d);
}


  if (num_buckets == 256) {
for (size_t i = 0; i < 256; i+=8) {
  prefix(&counts[i]);
}
__m512i s = _mm512_setzero();
for (size_t i = 0; i < 256; i+=8) {
  s = accumulate(&counts[i], s);
}

*/

template <typename Range, typename Value, typename Scan, typename Combine>
Value parallel_scan(const Range &range, const Value &identity, const Scan &scan,
                    const Combine &combine, size_t min_serial_size) {
  // TODO
}
} // namespace ParallelTools