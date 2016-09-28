/**
 * @file    psascan_src/inmem_psascan_src/inmem_compute_gap.hpp
 * @section LICENCE
 *
 * This file is part of pSAscan v0.2.0
 * See: http://www.cs.helsinki.fi/group/pads/
 *
 * Copyright (C) 2014-2016
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

#ifndef __PSASCAN_SRC_INMEM_PSASCAN_SRC_INMEM_COMPUTE_GAP_HPP_INCLUDED
#define __PSASCAN_SRC_INMEM_PSASCAN_SRC_INMEM_COMPUTE_GAP_HPP_INCLUDED

#include <cstdio>
#include <cstdint>
#include <map>
#include <vector>
#include <thread>
#include <algorithm>

#include "../bitvector.hpp"
#include "../gap_buffer.hpp"
#include "../io/multifile.hpp"
#include "rank.hpp"
#include "inmem_gap_array.hpp"
#include "inmem_compute_initial_ranks.hpp"
#include "inmem_stream.hpp"
#include "inmem_update.hpp"
#include "inmem_bwt_from_sa.hpp"
#include "pagearray.hpp"
#include "bwtsa.hpp"
#include "sparse_isa.hpp"


namespace psascan_private {
namespace inmem_psascan_private {

template<typename saidx_t, unsigned pagesize_log>
void inmem_compute_gap(const std::uint8_t *text, long text_length, long left_block_beg,
    long left_block_size, long right_block_size,
    const pagearray<bwtsa_t<saidx_t>, pagesize_log> &bwtsa,
    bitvector *gt, inmem_gap_array* &gap, std::uint64_t max_threads, bool need_gt, long i0,
    long gap_buf_size, long double &rank_init_time, long double &streaming_time,
    std::uint64_t **block_rank_matrix, long lrange_beg, long lrange_size, long rrange_size) {
  long lrange_end = lrange_beg + lrange_size;
  long rrange_end = lrange_end + rrange_size;

  //----------------------------------------------------------------------------
  // STEP 1: build rank data structure over BWT.
  //----------------------------------------------------------------------------
  fprintf(stderr, "    Build rank: ");
  long double start = utils::wclock();
  typedef rank4n<saidx_t, pagesize_log> rank_type;
  rank_type *rank = new rank_type(&bwtsa, left_block_size, max_threads);
  rank_init_time = utils::wclock() - start;
  fprintf(stderr, "total: %.2Lfs\n", rank_init_time);

  //----------------------------------------------------------------------------
  // STEP 2: compute symbol counts and the last symbol of the left block.
  //----------------------------------------------------------------------------
  std::uint64_t *count = new std::uint64_t[256];
  for (std::uint32_t j = 0; j < 256; ++j)
    count[j] = rank->rank(left_block_size, (std::uint8_t)j);

  const std::uint8_t *left_block = text + left_block_beg;
  std::uint8_t last = left_block[left_block_size - 1];
  ++count[last];
  --count[0];

  for (std::uint64_t i = 0, s = 0, t; i < 256; ++i) {
    t = count[i];
    count[i] = s;
    s += t;
  }

  //----------------------------------------------------------------------------
  // STEP 3: compute starting positions for all streaming threads.
  //----------------------------------------------------------------------------
  long left_block_end = left_block_beg + left_block_size;
  long right_block_beg = left_block_end;
  std::uint64_t right_block_end = left_block_end + right_block_size;

  long max_stream_block_size = (right_block_size + max_threads - 1) / max_threads;
  while (max_stream_block_size & 7) ++max_stream_block_size;
  std::uint64_t n_threads = (right_block_size + max_stream_block_size - 1) / max_stream_block_size;

  fprintf(stderr, "    Compute initial ranks: ");
  start = utils::wclock();
  std::vector<long> initial_ranks(n_threads);
  std::vector<std::pair<long, long> > initial_ranges(n_threads);
  std::thread **threads = new std::thread*[n_threads];

  // 3.a
  //
  // Compute the last starting position using the matrix of initial ranks.
  typedef pagearray<bwtsa_t<saidx_t>, pagesize_log> pagearray_bwtsa_type;
  long last_stream_block_beg = right_block_beg + (n_threads - 1) * max_stream_block_size;
  long last_stream_block_end = right_block_end;

  initial_ranks[n_threads - 1] = 0L;
  for (long j = lrange_beg; j < lrange_end; ++j)
    initial_ranks[n_threads - 1] += block_rank_matrix[j][rrange_end - 1];

  // 3.b
  //
  // Compute the starting position for all
  // starting positions other than the last one.
  std::uint64_t prev_stream_block_size = last_stream_block_end - last_stream_block_beg;
  for (std::int64_t i = (std::int64_t)n_threads - 2; i >= 0; --i) {
    std::uint64_t stream_block_beg = right_block_beg + i * max_stream_block_size;
    std::uint64_t stream_block_end = std::min(stream_block_beg + max_stream_block_size, right_block_end);
    std::uint64_t stream_block_size = stream_block_end - stream_block_beg;
    const std::uint8_t *pat = text + stream_block_end;

    threads[i] = new std::thread(compute_range<pagearray_bwtsa_type>,
        text, left_block_beg, left_block_size, pat, prev_stream_block_size,
        std::ref(bwtsa), std::ref(initial_ranges[i]));

    prev_stream_block_size = stream_block_size;
  }

  for (std::uint64_t i = 0; i + 1 < n_threads; ++i) threads[i]->join();
  for (std::uint64_t i = 0; i + 1 < n_threads; ++i) delete threads[i];
  delete[] threads;
  fprintf(stderr, "%.2Lfs ", utils::wclock() - start);

  bool nontrivial_range = false;
  for (std::uint64_t j = 0; j + 1 < n_threads; ++j)
    if (initial_ranges[j].first != initial_ranges[j].second)
      nontrivial_range = true;

  if (nontrivial_range) {
    // 3.c
    //
    // Build the data structure allowing answering ISA queries.
    start = utils::wclock();
    typedef pagearray<bwtsa_t<saidx_t>, pagesize_log> pagearray_type;
    typedef sparse_isa<pagearray_type, rank_type, 12U> sparse_isa_type;
    sparse_isa_type *sp_isa = new sparse_isa_type(&bwtsa, text +
        left_block_beg, rank, left_block_size, i0, max_threads);
    fprintf(stderr, "%.3Lfs ", utils::wclock() - start);

    // 3.d
    //
    // Narrow nontrivial ranges to single elements.
    start = utils::wclock();
    prev_stream_block_size = last_stream_block_end - last_stream_block_beg;
    std::uint64_t prev_rank = initial_ranks[n_threads - 1];
    for (long i = n_threads - 2; i >= 0; --i) {
      std::uint64_t stream_block_beg = right_block_beg + i * max_stream_block_size;
      std::uint64_t stream_block_end = std::min(stream_block_beg + max_stream_block_size, right_block_end);
      std::uint64_t stream_block_size = stream_block_end - stream_block_beg;
      std::uint64_t suf_start = stream_block_end;

      long left = initial_ranges[i].first;
      long right = initial_ranges[i].second;

      // Keep refining the range [left..right) until it's empty.
      while (left != right) {
        // Valid values for mid are in [left..right).
        long mid = (left + right) / 2;

        // Check if suffix starting at position suf_start is larger
        // than the one starting at block_beg + bwtsa[mid].sa in the text.
        // We know they have a common prefix of length prev_stream_block_size.
        if ((long)bwtsa[mid].m_sa + (long)prev_stream_block_size >= left_block_size) {
          if (gt->get(text_length - 1 - (suf_start + left_block_size - (long)bwtsa[mid].m_sa - 1))) left = mid + 1;
          else right = mid;
        } else {
          long j = bwtsa[mid].m_sa + prev_stream_block_size;
          if (sp_isa->query(j) < prev_rank) left = mid + 1;
          else right = mid;
        }
      }

      initial_ranks[i] = left;
      prev_rank = left;
      prev_stream_block_size = stream_block_size;
    }

    delete sp_isa;
    fprintf(stderr, "%.3Lfs ", utils::wclock() - start);
  } else {
    for (std::uint64_t j = 0; j + 1 < n_threads; ++j)
      initial_ranks[j] = initial_ranges[j].first;
  }
  fprintf(stderr, "\n");

  //----------------------------------------------------------------------------
  // STEP 4: allocate gap array. The gap array is indexed from 0 to
  //         left_block_size so the number of elements is left_block_size + 1.
  //----------------------------------------------------------------------------
  start = utils::wclock();
  gap = new inmem_gap_array(left_block_size + 1);

  //----------------------------------------------------------------------------
  // STEP 5: allocate buffers, buffer polls and auxiliary arrays.
  //----------------------------------------------------------------------------

  // Allocate gap buffers.
  long n_gap_buffers = 2 * n_threads;
  gap_buffer<saidx_t> **gap_buffers = new gap_buffer<saidx_t>*[n_gap_buffers];
  for (long i = 0; i < n_gap_buffers; ++i)
    gap_buffers[i] = new gap_buffer<saidx_t>(gap_buf_size, max_threads);

  // Create poll of empty and full buffers.
  gap_buffer_poll<saidx_t> *empty_gap_buffers = new gap_buffer_poll<saidx_t>();
  gap_buffer_poll<saidx_t> *full_gap_buffers = new gap_buffer_poll<saidx_t>(n_threads);

  // Add empty buffers to empty poll.
  for (long i = 0; i < n_gap_buffers; ++i)
    empty_gap_buffers->add(gap_buffers[i]);

  // Allocate temp arrays and oracles.
  long max_buffer_elems = gap_buf_size / sizeof(saidx_t);
  saidx_t *temp = (saidx_t *)malloc(max_buffer_elems * n_threads * sizeof(saidx_t));
  std::uint32_t *oracle = (std::uint32_t *)malloc(max_buffer_elems * n_threads * sizeof(std::uint32_t));
  long double allocations_time = utils::wclock() - start;
  if (allocations_time > 0.05L)
    fprintf(stderr, "    Allocations: %.2Lfs\n", allocations_time);

  //----------------------------------------------------------------------------
  // STEP 6: run the parallel streaming.
  //----------------------------------------------------------------------------

  // Start streaming threads.
  fprintf(stderr, "    Stream: ");
  start = utils::wclock();
  threads = new std::thread*[n_threads];
  for (std::uint64_t t = 0; t < n_threads; ++t) {
    std::uint64_t beg = right_block_beg + t * max_stream_block_size;
    std::uint64_t end = std::min(beg + max_stream_block_size, right_block_end);

    threads[t] = new std::thread(inmem_parallel_stream<rank_type, saidx_t>,
      text, text_length, beg, end, last, count, full_gap_buffers,
      empty_gap_buffers, initial_ranks[t], i0, rank, gap->m_length, max_threads,
      gt, temp + t * max_buffer_elems, oracle + t * max_buffer_elems, need_gt);
  }

  // Start updating thread.
  std::thread *updater = new std::thread(inmem_gap_updater<saidx_t>,
      full_gap_buffers, empty_gap_buffers, gap, max_threads);

  // Wait to all threads to finish.
  for (std::uint64_t t = 0; t < n_threads; ++t) threads[t]->join();
  updater->join();
  streaming_time = utils::wclock() - start;
  long double streaming_speed =
    (right_block_size / (1024.L * 1024)) / streaming_time;
  fprintf(stderr, "%.2Lfs (%.2LfMiB/s)\n", streaming_time,
      streaming_speed);

  //----------------------------------------------------------------------------
  // STEP 7: clean up and sort gap->m_excess.
  // XXX Consider using gnu parallel sort.
  //----------------------------------------------------------------------------
  start = utils::wclock();
  free(oracle);
  free(temp);
  for (std::uint64_t i = 0; i < n_threads; ++i) delete threads[i];
  for (long i = 0; i < n_gap_buffers; ++i) delete gap_buffers[i];
  delete updater;
  delete[] threads;
  delete[] gap_buffers;
  delete empty_gap_buffers;
  delete full_gap_buffers;
  delete rank;
  delete[] count;

  std::sort(gap->m_excess.begin(), gap->m_excess.end());

  long double cleaning_time = utils::wclock() - start;
  if (cleaning_time > 0.1L)
    fprintf(stderr, "    Clean: %.2Lfs\n", cleaning_time);
}

}  // namespace inmem_psascan_private
}  // namespace psascan_private
                 
#endif  // __PSASCAN_SRC_INMEM_PSASCAN_SRC_INMEM_COMPUTE_GAP_HPP_INCLUDED