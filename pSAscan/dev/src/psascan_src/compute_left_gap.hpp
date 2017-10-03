/**
 * @file    src/psascan_src/compute_left_gap.hpp
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

#ifndef __SRC_PSASCAN_SRC_COMPUTE_LEFT_GAP_HPP_INCLUDED
#define __SRC_PSASCAN_SRC_COMPUTE_LEFT_GAP_HPP_INCLUDED

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <omp.h>

#include "utils.hpp"
#include "bitvector.hpp"
#include "ranksel_support.hpp"
#include "gap_array.hpp"
#include "parallel_utils.hpp"


namespace psascan_private {

//=============================================================================
// Compute the range_gap values corresponging to bv[part_beg..part_end).
//=============================================================================
template<typename ranksel_support_type>
void lblock_handle_bv_part(
    const std::uint64_t part_beg,
    const std::uint64_t part_end,
    const std::uint64_t range_beg,
    const gap_array_2n * const block_gap,
    const bitvector * const bv,
    const ranksel_support_type * const bv_ranksel,
    std::uint64_t * const range_gap,
    std::uint64_t * const res_sum,
    std::uint64_t * const res_rank) {

  std::uint64_t excess_ptr =
    std::lower_bound(block_gap->m_excess.begin(),
        block_gap->m_excess.end(), part_beg) -
    block_gap->m_excess.begin();

  // Initialize j.
  std::uint64_t j = part_beg;

  // Compute gap[j].
  std::uint64_t gap_j = block_gap->m_count[j];
  while (excess_ptr < block_gap->m_excess.size() &&
      block_gap->m_excess[excess_ptr] == j) {
    ++excess_ptr;
    gap_j += (1 << 16);
  }

  // Initialize sum.
  std::uint64_t sum = gap_j + 1;

  while (j + 1 != part_end && bv->get(j) == 1) {

    // Update j.
    ++j;

    // Compute gap[j].
    gap_j = block_gap->m_count[j];
    while (excess_ptr < block_gap->m_excess.size() &&
        block_gap->m_excess[excess_ptr] == j) {
      ++excess_ptr;
      gap_j += (1 << 16);
    }

    // Update sum.
    sum += gap_j + 1;
  }
  if (bv->get(j) == 0) --sum;

  // Store gap[part_beg] + .. + gap[j] and
  // bv.rank0(part_beg) (== bv.rank0(j)).
  *res_sum = sum;
  *res_rank = bv_ranksel->rank0(part_beg);

  if (j + 1 == part_end)
    return;

  sum = 0;
  std::uint64_t range_gap_ptr = (*res_rank) + 1;
  while (j + 1 != part_end) {

    // Update j.
    ++j;

    // Compute gap[j].
    gap_j = block_gap->m_count[j];
    while (excess_ptr < block_gap->m_excess.size() &&
        block_gap->m_excess[excess_ptr] == j) {
      ++excess_ptr;
      gap_j += (1 << 16);
    }

    // Update sum.
    sum += gap_j + 1;

    // Update range_gap.
    if (bv->get(j) == 0) {
      range_gap[range_gap_ptr - range_beg] = sum - 1;
      ++range_gap_ptr;
      sum = 0;
    }
  }

  if (bv->get(j) == 1)
    range_gap[range_gap_ptr - range_beg] = sum;
}


void lblock_async_write_code(
    std::uint8_t* &slab,
    std::uint64_t &length,
    std::mutex &mtx,
    std::condition_variable &cv,
    bool &avail,
    bool &finished,
    std::FILE * const f) {

  while (true) {

    // Wait until the passive buffer is available.
    std::unique_lock<std::mutex> lk(mtx);
    while (!avail && !finished)
      cv.wait(lk);

    if (!avail && finished) {

      // We're done, terminate the thread.
      lk.unlock();
      return;
    }
    lk.unlock();

    // Safely write the data to disk.
    utils::write_to_file(slab, length, f);

    // Let the caller know that the I/O thread finished writing.
    lk.lock();
    avail = false;
    lk.unlock();
    cv.notify_one();
  }
}


//=============================================================================
// Given the gap array of the block (representation using 2 bytes per elements)
// and the gap array of the left half-block wrt right half-block (bitvector
// representation), compute the gap array (wrt tail) of the left half-block
// and write to a given file using v-byte encoding.
//
// The whole computation is performed under given ram budget. It is fully
// parallelized and uses asynchronous I/O as much as possible.
//=============================================================================
void compute_left_gap(
    const std::uint64_t left_block_size,
    const std::uint64_t right_block_size,
    const gap_array_2n * const block_gap,
    bitvector * const bv,
    const std::string out_filename,
    const std::uint64_t max_threads,
    const std::uint64_t ram_budget) {

  const std::uint64_t block_size = left_block_size + right_block_size;
  const std::uint64_t left_gap_size = left_block_size + 1;
  std::FILE * const f_out = utils::file_open(out_filename, "w");

  // NOTE: we require that bv has room for one extra bit at
  // the end which we use as a sentinel. The actual value of
  // that bit prior to calling this function does not matter.
  bv->reset(block_size);  // XXX eliminate that
  const std::uint64_t bv_size = block_size + 1;

  fprintf(stderr, "  Compute gap array for left half-block: ");
  const long double compute_gap_start = utils::wclock();

  // Preprocess left_block_gap_bv for rank and
  // select queries, i.e., compute sparse_gap.
  typedef ranksel_support<> ranksel_support_type;
  ranksel_support_type *bv_ranksel =
    new ranksel_support_type(bv, bv_size);

  // Compute the values of the right gap array, one range at a time.
  std::uint64_t max_range_size =
    std::max((std::uint64_t)1, (std::uint64_t)ram_budget / 24);
  std::uint64_t n_ranges =
    (left_gap_size + max_range_size - 1) / max_range_size;

  // To ensure that asynchronous I/O is really taking
  // place, we try to make 8 parts.
  if (n_ranges < 8L) {
    max_range_size = (left_gap_size + 7L) / 8L;
    n_ranges = (left_gap_size + max_range_size - 1) / max_range_size;
  }

  std::uint64_t * const range_gap =
    (std::uint64_t *)malloc(max_range_size * sizeof(std::uint64_t));
  std::uint8_t *active_vbyte_slab =
    (std::uint8_t *)malloc(max_range_size * sizeof(std::uint64_t));
  std::uint8_t *passive_vbyte_slab =
    (std::uint8_t *)malloc(max_range_size * sizeof(std::uint64_t));
  std::uint64_t active_vbyte_slab_length = 0;
  std::uint64_t passive_vbyte_slab_length = 0;

  // Used for communication with thread doing asynchronous writes.  
  std::mutex mtx;
  std::condition_variable cv;
  bool avail = false;
  bool finished = false;

  // Start the thread doing asynchronous writes.
  std::thread *async_writer = new std::thread(lblock_async_write_code,
      std::ref(passive_vbyte_slab), std::ref(passive_vbyte_slab_length),
      std::ref(mtx), std::ref(cv), std::ref(avail), std::ref(finished),
      f_out);

  for (std::uint64_t range_id = 0; range_id < n_ranges; ++range_id) {

    // Compute the range [range_beg..range_end) of
    // values in the left gap array (which is
    // indexed [0..left_gap_size)).
    const std::uint64_t range_beg = range_id * max_range_size;
    const std::uint64_t range_end =
      std::min(range_beg + max_range_size, left_gap_size);
    const std::uint64_t range_size = range_end - range_beg;

    // Find the section in the bitvector that contains
    // the bits necessary to compute the answer.
    std::uint64_t bv_section_beg = 0;
    std::uint64_t bv_section_end = 0;
    if (range_beg > 0)
      bv_section_beg = bv_ranksel->select0(range_beg - 1) + 1;
    bv_section_end = bv_ranksel->select0(range_end - 1) + 1;
    const std::uint64_t bv_section_size = bv_section_end - bv_section_beg;

    // Split the current bitvector section into
    // equal parts. Each thread handles one part.
    const std::uint64_t max_part_size =
      (bv_section_size + max_threads - 1) / max_threads;
    const std::uint64_t n_parts =
      (bv_section_size + max_part_size - 1) / max_part_size;

    // Zero-initialize range_gap.
#ifdef _OPENMP
    #pragma omp parallel for
    for (std::uint64_t i = 0; i < range_size; ++i)
      range_gap[i] = 0;
#else
    for (std::uint64_t i = 0; i < range_size; ++i)
      range_gap[i] = 0;
#endif  // _OPENMP

    // Allocate arrays used to store the answers for part boundaries.
    std::uint64_t * const res_sum = new std::uint64_t[n_parts];
    std::uint64_t * const res_rank = new std::uint64_t[n_parts];

    std::thread ** const threads = new std::thread*[n_parts];
    for (std::uint64_t t = 0; t < n_parts; ++t) {
      std::uint64_t part_beg = bv_section_beg + t * max_part_size;
      std::uint64_t part_end = std::min(bv_section_end,
          part_beg + max_part_size);

      threads[t] = new std::thread(
          lblock_handle_bv_part<ranksel_support_type>,
          part_beg, part_end, range_beg,
          block_gap, bv, bv_ranksel, range_gap,
          res_sum + t, res_rank + t);
    }

    for (std::uint64_t t = 0; t < n_parts; ++t) threads[t]->join();
    for (std::uint64_t t = 0; t < n_parts; ++t) delete threads[t];
    delete[] threads;

    // Update range_gap with values computed at part boundaries.
    for (std::uint64_t t = 0; t < n_parts; ++t)
      range_gap[res_rank[t] - range_beg] += res_sum[t];
    delete[] res_sum;
    delete[] res_rank;

    // Convert the range_gap to the slab of vbyte encoding.
    active_vbyte_slab_length =
      parallel_utils::convert_array_to_vbyte_slab(
          range_gap, range_size, active_vbyte_slab);

    // Schedule asynchronous write of the slab.
    // First, wait for the I/O thread to finish writing.
    std::unique_lock<std::mutex> lk(mtx);
    while (avail == true)
      cv.wait(lk);

    // Set the new passive slab.
    std::swap(active_vbyte_slab, passive_vbyte_slab);
    passive_vbyte_slab_length = active_vbyte_slab_length;

    // Let the I/O thread know that the slab is waiting.
    avail = true;
    lk.unlock();
    cv.notify_one();
  }

  // Let the I/O thread know that we're done.
  std::unique_lock<std::mutex> lk(mtx);
  finished = true;
  lk.unlock();
  cv.notify_one();
  
  // Wait for the thread to finish.
  async_writer->join();

  // Clean up.
  delete async_writer;
  delete bv_ranksel;
  free(range_gap);
  free(active_vbyte_slab);
  free(passive_vbyte_slab);
  std::fclose(f_out);

  long double compute_gap_time =
    utils::wclock() - compute_gap_start;
  long double compute_gap_speed =
    (block_size / (1024.L * 1024)) / compute_gap_time;
  fprintf(stderr, "%.2Lfs (%.2LfMiB/s)\n",
      compute_gap_time, compute_gap_speed);
}

}  // namespace psascan_private

#endif  // __SRC_PSASCAN_SRC_COMPUTE_LEFT_GAP_HPP_INCLUDED
