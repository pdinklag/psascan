/**
 * @file    src/psascan_src/inmem_psascan_src/initial_partial_sufsort.hpp
 * @section LICENCE
 *
 * This file is part of a modified pSAscan
 * See: https://github.com/pdinklag/psascan
 *
 * Copyright (C) 2014-2020
 *   Dominik Kempa <dominik.kempa (at) gmail.com>
 *   Juha Karkkainen <juha.karkkainen (at) cs.helsinki.fi>
 *   Patrick Dinklage <patrick.dinklage (at) tu-dortmund.de>
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

#ifndef __SRC_PSASCAN_SRC_INMEM_PSASCAN_SRC_INITIAL_PARTIAL_SUFSORT_HPP_INCLUDED
#define __SRC_PSASCAN_SRC_INMEM_PSASCAN_SRC_INITIAL_PARTIAL_SUFSORT_HPP_INCLUDED

#include <algorithm>
#include <thread>

#include "../bitvector.hpp"

#ifdef USE_LIBSAIS
    #include "sais_template.hpp"    
#else
    #include "divsufsort_template.hpp"
#endif

#include "bwtsa.hpp"
#include "parallel_shrink.hpp"
#include "parallel_expand.hpp"
#include "parallel_copy.hpp"


namespace psascan_private {
namespace inmem_psascan_private {

//==============================================================================
// Rename the given block using its gt bitvector.
//==============================================================================
void rename_block(unsigned char *text, long text_length, long block_beg,
    long block_length, bitvector *gt, bool &renaming_error) {
  long block_end = block_beg + block_length;
  long beg_rev = text_length - block_end;
  unsigned char *block = text + block_beg;
  unsigned char last = block[block_length - 1];
  bool err = false;
  for (long i = 0; i + 1 < block_length; ++i)
    if (block[i] > last || (block[i] == last && gt->get(beg_rev + i + 1))) {
      if (block[i] == 255)
        err = true;
      ++block[i];
    }
  if (block[block_length - 1] == 255)
    err = true;
  ++block[block_length - 1];

  if (err)
    renaming_error = true;
}


//==============================================================================
// Re-rename block back to original.
//==============================================================================
void rerename_block(unsigned char *block, long block_length) {
  unsigned char last = block[block_length - 1] - 1;
  for (long i = 0; i < block_length; ++i)
    if (block[i] > last) --block[i];
}


//==============================================================================
// Given gt bitvectors, compute partial suffix arrays of blocks.
//==============================================================================
template<typename saidx_t>
void initial_partial_sufsort(unsigned char *, long, bitvector *,
    bwtsa_t<saidx_t> *, long, long, bool) {
  fprintf(stderr, "Error: initial_partial_sufsort: given saidx_t is "
      "not supported, sizeof(saidx_t) = %ld\n", (long)sizeof(saidx_t));
  std::exit(EXIT_FAILURE);
}

template<>
void initial_partial_sufsort(unsigned char *text, long text_length,
    bitvector* gt, bwtsa_t<uint40> *bwtsa, long max_block_size,
    long max_threads, bool has_tail) {
  long double start = utils::wclock();
  long n_blocks = (text_length + max_block_size - 1) / max_block_size;

  //----------------------------------------------------------------------------
  // STEP 1: Rename the blocks in parallel.
  //----------------------------------------------------------------------------

  if (n_blocks > 1 || has_tail) {
    fprintf(stderr, "  Renaming blocks: ");
    start = utils::wclock();
    bool *renaming_error = new bool[n_blocks];
    std::fill(renaming_error, renaming_error + n_blocks, false);
    std::thread **threads = new std::thread*[n_blocks];
    for (long i = 0; i < n_blocks; ++i) {
      long block_end = text_length - (n_blocks - 1 - i) * max_block_size;
      long block_beg = std::max(0L, block_end - max_block_size);
      long block_size = block_end - block_beg;

      threads[i] = new std::thread(rename_block, text, text_length, block_beg,
          block_size, gt, std::ref(renaming_error[i]));
    }

    for (long i = 0; i < n_blocks; ++i) threads[i]->join();
    for (long i = 0; i < n_blocks; ++i) delete threads[i];
    delete[] threads;

    fprintf(stderr, "%.2Lf\n", utils::wclock() - start);

    bool err = false;
    for (long i = 0; i < n_blocks; ++i)
      if (renaming_error[i]) err = true;
    delete[] renaming_error;

    if (err) {
      fprintf(stdout, "\n\nError: byte with value 255 was detected in the input text!\n"
          "See the section on limitations in the README for more information.\n");
      std::fflush(stdout);
      std::exit(EXIT_FAILURE);
    }
  }

  if (max_block_size >= (2L << 30)) {  // Use 64-bit divsufsort.
    fprintf(stdout, "\nError: 2GiB+ partial suffix arrays are not "
        "yet supported by the internal-memory pSAscan.\n");
    std::fflush(stdout);
    std::exit(EXIT_FAILURE);
  } else {  // Use 32-bit divsufsort.
    int *temp_sa = (int *)bwtsa;

    //--------------------------------------------------------------------------
    // STEP 2: Compute suffix arrays in parallel.
    //--------------------------------------------------------------------------
    #ifdef USE_LIBSAIS
    fprintf(stderr, "  Running libsais in parallel: ");
    #else
    fprintf(stderr, "  Running divsufsort32 in parallel: ");
    #endif
    start = utils::wclock();
    std::thread **threads = new std::thread*[n_blocks];
    for (long i = 0; i < n_blocks; ++i) {
      long block_end = text_length - (n_blocks - 1 - i) * max_block_size;
      long block_beg = std::max(0L, block_end - max_block_size);
      long block_size = block_end - block_beg;

      threads[i] = new std::thread(
        #ifdef USE_LIBSAIS
        run_sais<int>,
        #else
        run_divsufsort<int>,
        #endif
        text + block_beg, temp_sa + block_beg, block_size);
    }

    for (long i = 0; i < n_blocks; ++i) threads[i]->join();
    for (long i = 0; i < n_blocks; ++i) delete threads[i];
    delete[] threads;

    fprintf(stderr, "%.2Lf\n", utils::wclock() - start);

    fprintf(stderr, "  Expanding 32-bit integers to bwtsa objects: ");
    start = utils::wclock();
    parallel_expand<int, bwtsa_t<uint40> >(temp_sa, text_length, max_threads);
    fprintf(stderr, "%.2Lf\n", utils::wclock() - start);
  }

  //----------------------------------------------------------------------------
  // STEP 3: Restore the original text.
  //----------------------------------------------------------------------------
  if (n_blocks > 1 || has_tail) {
    fprintf(stderr, "  Rerenaming blocks: ");
    start = utils::wclock();
    std::thread **threads = new std::thread*[n_blocks];
    for (long i = 0; i < n_blocks; ++i) {
      long block_end = text_length - (n_blocks - 1 - i) * max_block_size;
      long block_beg = std::max(0L, block_end - max_block_size);
      long block_size = block_end - block_beg;

      threads[i] = new std::thread(rerename_block,
          text + block_beg, block_size);
    }

    for (long i = 0; i < n_blocks; ++i) threads[i]->join();
    for (long i = 0; i < n_blocks; ++i) delete threads[i];
    delete[] threads;

    fprintf(stderr, "%.2Lf\n", utils::wclock() - start);
  }
}

template<>
void initial_partial_sufsort(unsigned char *text, long text_length,
    bitvector* gt, bwtsa_t<int> *bwtsa, long max_block_size, long max_threads,
    bool has_tail) {
  long double start = utils::wclock();
  long n_blocks = (text_length + max_block_size - 1) / max_block_size;

  //----------------------------------------------------------------------------
  // STEP 1: Rename the blocks in parallel.
  //----------------------------------------------------------------------------
  if (n_blocks > 1 || has_tail) {
    fprintf(stderr, "  Renaming blocks: ");
    start = utils::wclock();
    bool *renaming_error = new bool[n_blocks];
    std::fill(renaming_error, renaming_error + n_blocks, false);
    std::thread **threads = new std::thread*[n_blocks];
    for (long i = 0; i < n_blocks; ++i) {
      long block_end = text_length - (n_blocks - 1 - i) * max_block_size;
      long block_beg = std::max(0L, block_end - max_block_size);
      long block_size = block_end - block_beg;

      threads[i] = new std::thread(rename_block, text, text_length, block_beg,
          block_size, gt, std::ref(renaming_error[i]));
    }

    for (long i = 0; i < n_blocks; ++i) threads[i]->join();
    for (long i = 0; i < n_blocks; ++i) delete threads[i];
    delete[] threads;

    fprintf(stderr, "%.2Lf\n", utils::wclock() - start);

    bool err = false;
    for (long i = 0; i < n_blocks; ++i)
      if (renaming_error[i]) err = true;
    delete[] renaming_error;

    if (err) {
      fprintf(stdout, "\n\nError: byte with value 255 was detected in the input text!\n"
          "See the section on limitations in the README for more information.\n");
      std::fflush(stdout);
      std::exit(EXIT_FAILURE);
    }
  }
  
  int *temp_sa = (int *)bwtsa;

  //----------------------------------------------------------------------------
  // STEP 2: Compute suffix arrays in parallel.
  //----------------------------------------------------------------------------
  #ifdef USE_LIBSAIS
  fprintf(stderr, "  Running libsais in parallel: ");
  #else
  fprintf(stderr, "  Running divsufsort32 in parallel: ");
  #endif
  start = utils::wclock();
  std::thread **threads = new std::thread*[n_blocks];
  for (long i = 0; i < n_blocks; ++i) {
    long block_end = text_length - (n_blocks - 1 - i) * max_block_size;
    long block_beg = std::max(0L, block_end - max_block_size);
    long block_size = block_end - block_beg;

    threads[i] = new std::thread(
        #ifdef USE_LIBSAIS
        run_sais<int>,
        #else
        run_divsufsort<int>,
        #endif
        text + block_beg, temp_sa + block_beg, block_size);
  }

  for (long i = 0; i < n_blocks; ++i) threads[i]->join();
  for (long i = 0; i < n_blocks; ++i) delete threads[i];
  delete[] threads;
  fprintf(stderr, "%.2Lf\n", utils::wclock() - start);

  fprintf(stderr, "  Expanding 32-bit integers to bwtsa objects: ");
  start = utils::wclock();
  parallel_expand<int, bwtsa_t<int> >(temp_sa, text_length, max_threads);
  fprintf(stderr, "%.2Lf\n", utils::wclock() - start);

  //----------------------------------------------------------------------------
  // STEP 3: Restore the original text.
  //----------------------------------------------------------------------------
  if (n_blocks > 1 || has_tail) {
    fprintf(stderr, "  Rerenaming blocks: ");
    start = utils::wclock();
    threads = new std::thread*[n_blocks];
    for (long i = 0; i < n_blocks; ++i) {
      long block_end = text_length - (n_blocks - 1 - i) * max_block_size;
      long block_beg = std::max(0L, block_end - max_block_size);
      long block_size = block_end - block_beg;

      threads[i] = new std::thread(rerename_block,
          text + block_beg, block_size);
    }

    for (long i = 0; i < n_blocks; ++i) threads[i]->join();
    for (long i = 0; i < n_blocks; ++i) delete threads[i];
    delete[] threads;

    fprintf(stderr, "%.2Lf\n", utils::wclock() - start);
  }
}

}  // namespace inmem_psascan_private
}  // namespace psascan_private

#endif  // __SRC_PSASCAN_SRC_INMEM_PSASCAN_SRC_INITIAL_PARTIAL_SUFSORT_HPP_INCLUDED
