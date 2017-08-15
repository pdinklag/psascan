/**
 * @file    src/psascan_src/inmem_psascan_src/parallel_expand.hpp
 * @section LICENCE
 *
 * This file is part of pSAscan v0.2.0
 * See: http://www.cs.helsinki.fi/group/pads/
 *
 * Copyright (C) 2014-2017
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

#ifndef __SRC_PSASCAN_SRC_INMEM_PSASCAN_SRC_PARALLEL_EXPAND_HPP_INCLUDED
#define __SRC_PSASCAN_SRC_INMEM_PSASCAN_SRC_PARALLEL_EXPAND_HPP_INCLUDED

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <algorithm>
#include <thread>

#include "bwtsa.hpp"


namespace psascan_private {
namespace inmem_psascan_private {

template<typename src_type, typename dest_type>
void parallel_expand_aux(const src_type *src, dest_type *dest, std::uint64_t length) {
  for (std::uint64_t i = length; i > 0; --i)
    dest[i - 1] = (dest_type)src[i - 1];
}

// Specialization
template<>
void parallel_expand_aux(const std::int32_t *src, bwtsa_t<uint40> *dest, std::uint64_t length) {
  for (std::uint64_t i = length; i > 0; --i)
    dest[i - 1].m_sa = src[i - 1];
}

// Specialization
template<>
void parallel_expand_aux(const std::int32_t *src, bwtsa_t<std::int32_t> *dest, std::uint64_t length) {
  for (std::uint64_t i = length; i > 0; --i)
    dest[i - 1].m_sa = src[i - 1];
}

// Specialization
template<>
void parallel_expand_aux(const std::int64_t *src, bwtsa_t<std::int64_t> *dest, std::uint64_t length) {
  for (std::uint64_t i = length; i > 0; --i)
    dest[i - 1].m_sa = src[i - 1];
}

template<typename src_type, typename dest_type>
dest_type *parallel_expand(src_type *tab, std::uint64_t length, std::uint64_t max_threads) {
  dest_type *result = (dest_type *)tab;
  if (length < (1UL << 20)) {
    parallel_expand_aux<src_type, dest_type>(tab, (dest_type *)tab, length);
    return result;
  }

  // Compute the index of the smallest element (of type src_type)
  // that lies past the end of the last element of tab
  // (after converting all elements to type dest_type).
  std::uint64_t bytes_before_expanding = length * sizeof(src_type);
  std::uint64_t split = (bytes_before_expanding + sizeof(dest_type) - 1) / sizeof(dest_type);

  // Move the elements in the range [split, length) in parallel.
  // This is safe (no element overwriting) because of how we
  // computed the split.
  std::uint64_t elems = length - split;
  std::uint64_t max_block_size = (elems + max_threads - 1) / max_threads;
  std::uint64_t n_blocks = (elems + max_block_size - 1) / max_block_size;

  std::thread **threads = new std::thread*[n_blocks];
  for (std::uint64_t i = 0; i < n_blocks; ++i) {
    std::uint64_t block_beg = split + i * max_block_size;
    std::uint64_t block_end = std::min(block_beg + max_block_size, length);
    std::uint64_t block_size = block_end - block_beg;

    threads[i] = new std::thread(parallel_expand_aux<src_type, dest_type>,
        tab + block_beg, result + block_beg, block_size);
  }

  for (std::uint64_t i = 0; i < n_blocks; ++i) threads[i]->join();
  for (std::uint64_t i = 0; i < n_blocks; ++i) delete threads[i];
  delete[] threads;

  // Recursively expand the first split elements.
  parallel_expand<src_type, dest_type>(tab, split, max_threads);

  return result;
}

}  // namespace inmem_psascan_private
}  // namespace psascan_private

#endif  // __SRC_PSASCAN_SRC_INMEM_PSASCAN_SRC_PARALLEL_EXPAND_HPP_INCLUDED
