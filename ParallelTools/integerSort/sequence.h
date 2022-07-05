// This code is part of the Problem Based Benchmark Suite (PBBS)
// Copyright (c) 2011 Guy Blelloch and the PBBS team
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

#ifndef A_SEQUENCE_INCLUDED
#define A_SEQUENCE_INCLUDED

#include "../parallel.h"
#include "utils.h"
#include <iostream>

// For fast popcount
#include <immintrin.h>
#include <x86intrin.h>

namespace ParallelTools {
using namespace std;

#define _BSIZE 2048
#define _SCAN_LOG_BSIZE 10
#define _SCAN_BSIZE (1 << _SCAN_LOG_BSIZE)

template <class T> struct _seq {
  T *A;
  long n;
  _seq() {
    A = NULL;
    n = 0;
  }
  _seq(T *_A, long _n) : A(_A), n(_n) {}
  void del() { free(A); }
};

namespace sequence {

template <class intT> struct boolGetA {
  bool *A;
  boolGetA(bool *AA) : A(AA) {}
  intT operator()(intT i) { return (intT)A[i]; }
};

template <class ET, class intT> struct getA {
  ET *A;
  getA(ET *AA) : A(AA) {}
  ET operator()(intT i) { return A[i]; }
};

template <class IT, class OT, class intT, class F> struct getAF {
  IT *A;
  F f;
  getAF(IT *AA, F ff) : A(AA), f(ff) {}
  OT operator()(intT i) { return f(A[i]); }
};

#define nblocks(_n, _bsize) (1 + ((_n)-1) / (_bsize))

#define blocked_for(_i, _s, _e, _bsize, _body)                                 \
  {                                                                            \
    intT _ss = _s;                                                             \
    intT _ee = _e;                                                             \
    intT _n = _ee - _ss;                                                       \
    intT _l = nblocks(_n, _bsize);                                             \
    parallel_for(0, _l, [&](intT _i) {                                         \
      intT _s = _ss + _i * (_bsize);                                           \
      intT _e = min(_s + (_bsize), _ee);                                       \
      _body                                                                    \
    });                                                                        \
  }

template <class OT, class intT, class F, class G>
OT reduceSerial(intT s, intT e, F f, G g) {
  OT r = g(s);
  for (intT j = s + 1; j < e; j++)
    r = f(r, g(j));
  return r;
}

template <class OT, class intT, class F, class G>
OT reduce(intT s, intT e, F f, G g) {
  intT l = nblocks(e - s, _SCAN_BSIZE);
  if (l <= 1)
    return reduceSerial<OT>(s, e, f, g);
  OT *Sums = (OT *)malloc(l * sizeof(OT));
  blocked_for(i, s, e, _SCAN_BSIZE, Sums[i] = reduceSerial<OT>(s, e, f, g););
  OT r = reduce<OT>((intT)0, l, f, getA<OT, intT>(Sums));
  free(Sums);
  return r;
}

template <class OT, class intT, class F> OT reduce(OT *A, intT n, F f) {
  return reduce<OT>((intT)0, n, f, getA<OT, intT>(A));
}

template <class OT, class intT, class F> OT reduce(OT *A, intT s, intT e, F f) {
  return reduce<OT>(s, e, f, getA<OT, intT>(A));
}

template <class OT, class intT> OT plusReduce(OT *A, intT n) {
  return reduce<OT>((intT)0, n, utils::addF<OT>(), getA<OT, intT>(A));
}

template <class intT> intT sum(bool *In, intT n) {
  return reduce<intT>((intT)0, n, utils::addF<intT>(), boolGetA<intT>(In));
}

// g is the map function (applied to each element)
// f is the reduce function
// need to specify OT since it is not an argument
template <class OT, class IT, class intT, class F, class G>
OT mapReduce(IT *A, intT n, F f, G g) {
  return reduce<OT>((intT)0, n, f, getAF<IT, OT, intT, G>(A, g));
}

template <class ET, class intT, class F, class G>
ET scanSerial(ET *Out, intT s, intT e, F f, G g, ET zero, bool inclusive,
              bool back) {
  ET r = zero;

  if (inclusive) {
    if (back)
      for (long i = e - 1; i >= s; i--)
        Out[i] = r = f(r, g(i));
    else
      for (intT i = s; i < e; i++)
        Out[i] = r = f(r, g(i));
  } else {
    if (back)
      for (long i = e - 1; i >= s; i--) {
        ET t = g(i);
        Out[i] = r;
        r = f(r, t);
      }
    else
      for (intT i = s; i < e; i++) {
        ET t = g(i);
        Out[i] = r;
        r = f(r, t);
      }
  }
  return r;
}

template <class ET, class intT, class F>
ET scanSerial(ET *In, ET *Out, intT n, F f, ET zero) {
  return scanSerial(Out, (intT)0, n, f, getA<ET, intT>(In), zero, false, false);
}

// back indicates it runs in reverse direction
template <class ET, class intT, class F, class G>
ET scan(ET *Out, intT s, intT e, F f, G g, ET zero, bool inclusive, bool back) {
  intT n = e - s;
  intT l = nblocks(n, _SCAN_BSIZE);
  if (l <= 2)
    return scanSerial(Out, s, e, f, g, zero, inclusive, back);
  ET *Sums = (ET *)malloc(nblocks(n, _SCAN_BSIZE) * sizeof(ET));
  blocked_for(i, s, e, _SCAN_BSIZE, Sums[i] = reduceSerial<ET>(s, e, f, g););
  ET total = scan(Sums, (intT)0, l, f, getA<ET, intT>(Sums), zero, false, back);
  blocked_for(i, s, e, _SCAN_BSIZE,
              scanSerial(Out, s, e, f, g, Sums[i], inclusive, back););
  free(Sums);
  return total;
}

template <class ET, class intT, class F>
ET scan(ET *In, ET *Out, intT n, F f, ET zero) {
  return scan(Out, (intT)0, n, f, getA<ET, intT>(In), zero, false, false);
}

template <class ET, class intT, class F>
ET scanBack(ET *In, ET *Out, intT n, F f, ET zero) {
  return scan(Out, (intT)0, n, f, getA<ET, intT>(In), zero, false, true);
}

template <class ET, class intT, class F>
ET scanI(ET *In, ET *Out, intT n, F f, ET zero) {
  return scan(Out, (intT)0, n, f, getA<ET, intT>(In), zero, true, false);
}

template <class ET, class intT, class F>
ET scanIBack(ET *In, ET *Out, intT n, F f, ET zero) {
  return scan(Out, (intT)0, n, f, getA<ET, intT>(In), zero, true, true);
}

} // namespace sequence
} // namespace ParallelTools
#endif // _A_SEQUENCE_INCLUDED
