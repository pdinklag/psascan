/**
 * @file    psascan_src/inmem_psascan_src/parallel_expand.h
 * @author  Dominik Kempa <dominik.kempa (at) gmail.com>
 *
 * @section LICENCE
 *
 * This file is part of pSAscan. See: http://www.cs.helsinki.fi/group/pads/
 * Copyright (C) 2014-2015
 *   Juha Karkkainen <juha.karkkainen (at) cs.helsinki.fi>
 *   Dominik Kempa <dominik.kempa (at) gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 **/

#ifndef __PSASCAN_SRC_INMEM_PSASCAN_SRC_PARALLEL_EXPAND_H_INCLUDED
#define __PSASCAN_SRC_INMEM_PSASCAN_SRC_PARALLEL_EXPAND_H_INCLUDED

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <thread>


namespace psascan_private {
namespace inmem_psascan_private {

template<typename T, typename S>
void parallel_expand_aux(const T *src, S *dest, long length) {
  for (long i = 0; i < length; ++i)
    dest[i] = (S)src[i];
}

// Requires sizeof(T) < sizeof(S).
template<typename T, typename S>
S *parallel_expand(T *tab, long length, long max_threads) {
  S *result = (S *)tab;

  long diff = (long)sizeof(S) - (long)sizeof(T);
  if (!diff) {
    fprintf(stderr, "Error: expanding requires sizeof(T) < sizeof(S)\n");
    std::exit(EXIT_FAILURE);
  }

  if (length < (1L << 20)) {
    // Move the elelements sequentially.
    for (long i = length - 1; i >= 0; --i)
      result[i] = (S)tab[i];

    return result;
  }

  // Compute the index of the smallest element (of type T)
  // that lies past the end of the last element of tab
  // (after converting all elements to type S).
  long bytes_before_expanding = length * sizeof(T);
  long split = (bytes_before_expanding + sizeof(S) - 1) / sizeof(S);

  // Move the elements in the range [split, length) in parallel.
  // This is safe (no element overwriting) because of how we
  // computed the split.
  long elems = length - split;
  long max_block_size = (elems + max_threads - 1) / max_threads;
  long n_blocks = (elems + max_block_size - 1) / max_block_size;

  std::thread **threads = new std::thread*[n_blocks];
  for (long i = 0; i < n_blocks; ++i) {
    long block_beg = split + i * max_block_size;
    long block_end = std::min(block_beg + max_block_size, length);
    long block_size = block_end - block_beg;

    threads[i] = new std::thread(parallel_expand_aux<T, S>,
        tab + block_beg, result + block_beg, block_size);
  }

  for (long i = 0; i < n_blocks; ++i) threads[i]->join();
  for (long i = 0; i < n_blocks; ++i) delete threads[i];
  delete[] threads;


  // Recursively expand the first split elements..
  parallel_expand<T, S>(tab, split, max_threads);

  return result;
}

}  // namespace inmem_psascan_private
}  // namespace psascan_private

#endif  // __PSASCAN_SRC_INMEM_PSASCAN_SRC_PARALLEL_SHRINK_H_INCLUDED
