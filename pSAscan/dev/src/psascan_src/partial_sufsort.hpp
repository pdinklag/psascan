/**
 * @file    src/psascan_src/partial_sufsort.hpp
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

// XXX fix the interface issue of inmem_sascan
// XXX check if inmem_sascan can compute gt_begin reversed

#ifndef __SRC_PSASCAN_SRC_PARTIAL_SUFSORT_HPP_INCLUDED
#define __SRC_PSASCAN_SRC_PARTIAL_SUFSORT_HPP_INCLUDED

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <algorithm>
#include <vector>
#include <sys/stat.h>
#include <fcntl.h>

#include "inmem_psascan_src/inmem_psascan.hpp"
#include "utils.hpp"
#include "rank.hpp"
#include "gap_array.hpp"
#include "bitvector.hpp"
#include "half_block_info.hpp"
#include "merge_bwt.hpp"
#include "compute_gap.hpp"
#include "compute_ranks.hpp"
#include "compute_right_gap.hpp"
#include "compute_left_gap.hpp"
#include "io/scatterfile_writer.hpp"
#include "io/multifile.hpp"


namespace psascan_private {

//#define DROP_CACHE

//=============================================================================
// The main function processing the block.
//=============================================================================
// INPUT
// In addition to the block boundaries, ram budget and other integer
// parameters, the function requires that gt_begin was computed for the tail
// of the text and the multifile representation of the bitvector on disk is
// given as an input.
//
// OUTPUT
// The function produces the following output:
// - partial suffix array of the block, stored on disk as a distributed
//   file. The handle to this file is returned from the function. Note that
//   any distributed file is unnamed (the actual files created on disk have
//   names decided by the implementation of the distributed_file class), so
//   the handle is the only way of accessing distributed files (well, this
//   is actually not yet implemented).
// - gap array of the block, stored on disk as a regular file using v-byte
//   encoding. The name of the file is output_filename + ".gap." + block_id.
// - gt_begin of the new tail (which consits of the block and the old tail)
//   stored on disk as a multifile. The handle to this multifile is returned
//   via the reference.
//
// NOTE
// * The multifile representation is different from distributed file!
// * On entry to the function it holds: 5.2 * block_size <= ram_use
// * Next version of this function will return two distributed files tather
//   than one -- each holding the partial suffix of the half-block. For this
//   to work, we will have to change few this here and there, but overall it
//   should save some I/O.
//==============================================================================
template<typename block_offset_type>
void process_block(long block_beg, long block_end, long text_length, std::uint64_t ram_use,
    long max_threads, long gap_buf_size, std::string text_filename,
    std::string output_filename, std::string gap_filename,
    multifile *newtail_gt_begin_rev, const multifile *tail_gt_begin_rev,
    std::vector<half_block_info<block_offset_type> > &hblock_info, bool verbose) {
  std::uint64_t block_size = block_end - block_beg;

  if (block_end != text_length && block_size <= 1) {
    fprintf(stderr, "Error: any block other than the last one has to be of length at least two.\n");
    std::exit(EXIT_FAILURE);
  }

  long block_tail_beg = block_end;
  long block_tail_end = text_length;

  bool last_block = (block_end == text_length);
  bool first_block = (block_beg == 0);

  std::uint64_t left_block_size;
  if (!last_block) left_block_size = std::max(1UL, block_size / 2);
  else left_block_size = std::min(block_size, std::max(1UL, ram_use / 10));
  std::uint64_t right_block_size = block_size - left_block_size;
  long left_block_beg = block_beg;
  long left_block_end = block_beg + left_block_size;
  long right_block_beg = left_block_end;
  long right_block_end = block_end;
  // Invariant; left_block_size > 0.

  fprintf(stderr, "  Block size = %ld (%.2LfMiB)\n", block_size, 1.L * block_size / (1 << 20));
  fprintf(stderr, "  Left half-block size = %ld (%.2LfMiB)\n", left_block_size, 1.L * left_block_size / (1 << 20));
  fprintf(stderr, "  Right half-block size = %ld (%.2LfMiB)\n", right_block_size, 1.L * right_block_size / (1 << 20));

  std::vector<std::uint64_t> block_initial_ranks;
  unsigned char block_last_symbol = 0;

  std::uint64_t right_block_i0 = 0;
  std::uint64_t left_block_i0 = 0;

  std::string right_block_pbwt_fname = output_filename + "." + utils::random_string_hash();
  std::string right_block_gt_begin_rev_fname = output_filename + "." + utils::random_string_hash();

  half_block_info<block_offset_type> info_left;
  half_block_info<block_offset_type> info_right;

  info_left.beg = left_block_beg;
  info_left.end = left_block_end;
  if (right_block_size > 0) {
    info_right.beg = right_block_beg;
    info_right.end = right_block_end;
  }

  //----------------------------------------------------------------------------
  // STEP 1: Process right half-block.
  //
  // The output of this step if the following:
  // - if right_block_size == 0, then nothing is produced and pointers
  //   right_block_psa and right_block_gt_begin_rev remain NULL,
  // - otherwise right_block_psa and right_block_gt_begin_rev are not NULL.
  //   right_block_psa is a handler to the distributed file containing partial
  //   suffix array of the right half-block. right_block_gt_begin_rev is a
  //   handler to multifile containing gt_begin or the right block. Both
  //   structures are of negligible size. The actual data is store on disk.
  //
  // NOTE: The right half-block can be empty ONLY if the block under
  // consideration is the last block of text.
  //----------------------------------------------------------------------------
  multifile *right_block_gt_begin_rev = NULL;
  unsigned char *right_block = NULL;

  if (right_block_size > 0) {
    fprintf(stderr, "  Process right half-block:\n");

    // 1.a
    //
    // Read the right half-block from disk.
    fprintf(stderr, "    Read: ");
    right_block = (unsigned char *)malloc(right_block_size);
    long double right_block_read_start = utils::wclock();
    utils::read_at_offset(right_block, right_block_beg, right_block_size, text_filename);
    block_last_symbol = right_block[right_block_size - 1];
    long double right_block_read_time = utils::wclock() - right_block_read_start;
    long double right_block_read_io = (right_block_size / (1024.L * 1024)) / right_block_read_time;
    fprintf(stderr, "%.2Lfs (I/O: %.2LfMiB/s)\n", right_block_read_time, right_block_read_io);
 
    // 1.b
    //
    // Compute partial SA, BWT and gt_begin of the right half-block.

    // Allocate SA, BWT and gt_begin.
    unsigned char *right_block_sabwt = (unsigned char *)malloc(right_block_size * (sizeof(block_offset_type) + 1));
    block_offset_type *right_block_psa_ptr = (block_offset_type *)right_block_sabwt;
    unsigned char *right_block_bwt = (unsigned char *)(right_block_psa_ptr + right_block_size);
    bitvector *right_block_gt_begin_rev_bv = new bitvector(right_block_size);

    // Start the timer.
    fprintf(stderr, "    Internal memory sufsort: ");
    if (verbose) fprintf(stderr, "\n%s\n", std::string(60, '*').c_str());
    long double right_block_sascan_start = utils::wclock();

    // Close stderr.
    int stderr_backup = 0;
    if (!verbose) {
      std::fflush(stderr);
      stderr_backup = dup(2);
      int stderr_temp = open("/dev/null", O_WRONLY);
      dup2(stderr_temp, 2);
      close(stderr_temp);
    }

    // Run in-memory pSAscan.
    inmem_psascan_private::inmem_psascan<block_offset_type>(right_block, right_block_size, right_block_sabwt,
        max_threads, !last_block, true, right_block_gt_begin_rev_bv, 0, right_block_beg, right_block_end,
        text_length, text_filename, tail_gt_begin_rev, &right_block_i0);

    // Restore stderr.
    if (!verbose) {
      std::fflush(stderr);
      dup2(stderr_backup, 2);
      close(stderr_backup);
    }

    // Print summary.
    long double right_block_sascan_time = utils::wclock() - right_block_sascan_start;
    long double right_block_sascan_speed = (right_block_size / (1024.L * 1024)) / right_block_sascan_time;
    if (verbose) fprintf(stderr, "%s\n", std::string(60, '*').c_str());
    else fprintf(stderr, "%.2Lfs. Speed: %.2LfMiB/s\n", right_block_sascan_time, right_block_sascan_speed);
 
    // 1.c
    //
    // Compute the first term of initial ranks for the block.
    // Note the space usage.
    if (!last_block) {
      fprintf(stderr, "    Compute initial tail ranks (part 1): ");
      long double initial_ranks_first_term_start = utils::wclock();
      std::uint64_t stream_block_size =
        ((block_tail_end - right_block_end) + max_threads - 1) / max_threads;
      compute_ranks<block_offset_type>(
          right_block, right_block_bwt, right_block_psa_ptr,
          tail_gt_begin_rev, text_filename, right_block_i0,
          right_block_beg, right_block_end, text_length,
          stream_block_size, block_tail_end, 0, block_initial_ranks);

      std::uint64_t vec_size = block_initial_ranks.size();
      for (std::uint64_t j = 0; j + 1 < vec_size; ++j)
        block_initial_ranks[j] = block_initial_ranks[j + 1];
      block_initial_ranks[vec_size - 1] = 0;

      fprintf(stderr, "%.2Lfs\n", utils::wclock() - initial_ranks_first_term_start);
    }

    // 1.d
    //
    // Write the partial SA of the right half-block to disk.
    fprintf(stderr, "    Write partial SA to disk: ");
    long double right_psa_save_start = utils::wclock();
    long right_psa_max_part_length = std::max(sizeof(block_offset_type), ram_use / 20);
    info_right.psa = scatterfile<block_offset_type>(right_psa_max_part_length);

    typedef scatterfile_writer<block_offset_type> psa_writer_type;
    psa_writer_type *psa_writer = new psa_writer_type(&info_right.psa, output_filename);
    psa_writer->write(right_block_psa_ptr, right_block_size);
    delete psa_writer;

    long double right_psa_save_time = utils::wclock() - right_psa_save_start;
    long double right_psa_save_io = ((right_block_size * sizeof(block_offset_type)) / (1024.L * 1024)) / right_psa_save_time;
    fprintf(stderr, "%.2Lfs (I/O: %.2LfMiB/s)\n", right_psa_save_time, right_psa_save_io);

    // 1.e
    //
    // Write the BWT of the right half-block on disk.
    if (!last_block) {
      fprintf(stderr, "    Write BWT to disk: ");
      long double right_bwt_save_start = utils::wclock();
      utils::write_to_file(right_block_bwt, right_block_size, right_block_pbwt_fname);
      long double right_bwt_save_time = utils::wclock() - right_bwt_save_start;
      long double right_bwt_save_io = (right_block_size / (1024.L * 1024)) / right_bwt_save_time;
      fprintf(stderr, "%.2Lfs (I/O: %.2LfMiB/s)\n", right_bwt_save_time, right_bwt_save_io);
    }
    free(right_block_sabwt);

    // 1.f
    //
    // Write reversed gt_begin of the right half-block to disk.
    fprintf(stderr, "    Write gt_begin to disk: ");
    long double right_gt_begin_rev_save_start = utils::wclock();
    right_block_gt_begin_rev_bv->save(right_block_gt_begin_rev_fname);
    right_block_gt_begin_rev = new multifile();
    right_block_gt_begin_rev->add_file(text_length - right_block_end, text_length - right_block_beg,
        right_block_gt_begin_rev_fname);
    delete right_block_gt_begin_rev_bv;
    long double right_gt_begin_rev_save_time = utils::wclock() - right_gt_begin_rev_save_start;
    long double right_gt_begin_rev_save_io = (right_block_size / (8.L * (1 << 20))) / right_gt_begin_rev_save_time;
    fprintf(stderr, "%.2Lfs (I/O: %.2LfMiB/s)\n", right_gt_begin_rev_save_time, right_gt_begin_rev_save_io);

#ifdef DROP_CACHE
    utils::drop_cache();
#endif
  }


  //----------------------------------------------------------------------------
  // STEP 2: Process left half-block.
  //
  // At this point in RAM reside only handlers to gt_begin and partial SA of
  // the right half-block (if it was empty they are both NULL). Both handles
  // take negligible space, the actual data structures are stored on disk.
  //----------------------------------------------------------------------------
  fprintf(stderr, "  Process left half-block:\n");

  // 2.a
  //
  // Read the left half-block from disk.
  fprintf(stderr, "    Read: ");
  long double left_block_read_start = utils::wclock();
  unsigned char *left_block = (unsigned char *)malloc(left_block_size);
  utils::read_at_offset(left_block, left_block_beg, left_block_size, text_filename);
  unsigned char left_block_last = left_block[left_block_size - 1];
  long double left_block_read_time = utils::wclock() - left_block_read_start;
  long double left_block_read_io = (left_block_size / (1024.L * 1024)) / left_block_read_time;
  fprintf(stderr, "%.2Lfs (I/O: %.2LfMiB/s)\n", left_block_read_time, left_block_read_io);

  // 2.b
  //
  // Compute partial SA, BWT and gt_begin for left half-block.

  // Allocate SA, BWT and gt_begin.
  unsigned char *left_block_sabwt = (unsigned char *)malloc(left_block_size * (sizeof(block_offset_type) + 1) + 1);
  block_offset_type *left_block_psa_ptr = (block_offset_type *)left_block_sabwt;
  unsigned char *left_block_bwt_ptr = (unsigned char *)(left_block_psa_ptr + left_block_size);
  bitvector *left_block_gt_begin_rev_bv = NULL;
  if (!first_block) left_block_gt_begin_rev_bv = new bitvector(left_block_size);  

  // Start the timer.
  fprintf(stderr, "    Internal memory sufsort: ");
  if (verbose) fprintf(stderr, "\n%s\n", std::string(60, '*').c_str());
  long double left_block_sascan_start = utils::wclock();

  // Close stderr.
  int stderr_backup = 0;
  if (!verbose) {
    std::fflush(stderr);
    stderr_backup = dup(2);
    int stderr_temp = open("/dev/null", O_WRONLY);
    dup2(stderr_temp, 2);
    close(stderr_temp);
  }

  // Run in-memory pSAscan.
  inmem_psascan_private::inmem_psascan<block_offset_type>(left_block, left_block_size, left_block_sabwt,
      max_threads, (right_block_size > 0), !first_block, left_block_gt_begin_rev_bv, 0, left_block_beg,
      left_block_end, text_length, text_filename, right_block_gt_begin_rev, &left_block_i0, right_block);

  // Restore stderr.
  if (!verbose) {
    std::fflush(stderr);
    dup2(stderr_backup, 2);
    close(stderr_backup);
  }

  // Print summary.
  long double left_block_sascan_time = utils::wclock() - left_block_sascan_start;
  long double left_block_sascan_speed = (left_block_size / (1024.L * 1024)) / left_block_sascan_time;
  if (verbose) fprintf(stderr, "%s\n", std::string(60, '*').c_str());
  else fprintf(stderr, "%.2Lfs (%.2LfMiB/s)\n", left_block_sascan_time, left_block_sascan_speed);

  // 2.c
  //
  // Compute the second terms of block
  // initial ranks. Note the space usage.
  long after_block_initial_rank = 0;
  if (!last_block) {
    fprintf(stderr, "    Compute initial tail ranks (part 2): ");
    long double initial_ranks_second_term_start = utils::wclock();
    std::vector<std::uint64_t> block_initial_ranks_second_term;
    std::uint64_t stream_block_size =
      ((text_length - block_tail_beg) + max_threads - 1) / max_threads;
    compute_ranks<block_offset_type>(
        left_block, left_block_psa_ptr, tail_gt_begin_rev,
        text_filename, left_block_beg, left_block_end,
        text_length, block_tail_beg, stream_block_size,
        block_initial_ranks_second_term);

    after_block_initial_rank = block_initial_ranks_second_term[0];
    std::uint64_t vec_size = block_initial_ranks_second_term.size();
    for (std::uint64_t j = 0; j + 1 < vec_size; ++j)
      block_initial_ranks_second_term[j] = block_initial_ranks_second_term[j + 1];
    block_initial_ranks_second_term[vec_size - 1] = 0;

    for (std::uint64_t j = 0; j < vec_size; ++j)
      block_initial_ranks[j] += block_initial_ranks_second_term[j];
    fprintf(stderr, "%.2Lfs\n", utils::wclock() - initial_ranks_second_term_start);
  }

  // 2.d
  //
  // Write the partial SA of the left half-block to disk.
  fprintf(stderr, "    Write partial SA to disk: ");
  long double left_psa_save_start = utils::wclock();
  long left_psa_max_part_length = std::max(sizeof(block_offset_type), ram_use / 20);
  info_left.psa = scatterfile<block_offset_type>(left_psa_max_part_length);

  typedef scatterfile_writer<block_offset_type> psa_writer_type;
  psa_writer_type *psa_writer = new psa_writer_type(&info_left.psa, output_filename);
  psa_writer->write(left_block_psa_ptr, left_block_size);
  delete psa_writer;

  long double left_psa_save_time = utils::wclock() - left_psa_save_start;
  long double left_psa_save_io = ((left_block_size * sizeof(block_offset_type)) / (1024.L * 1024)) / left_psa_save_time;
  fprintf(stderr, "%.2Lfs (I/O: %.2LfMiB/s)\n", left_psa_save_time, left_psa_save_io);

  // 2.e
  //
  // Copy the BWT of the left half-block to separate array.
  unsigned char *left_block_bwt = NULL;
  if (right_block_size > 0) {
    fprintf(stderr, "    Copy BWT of left half-block to separate array: ");
    long double left_bwt_copy_start = utils::wclock();
    left_block_bwt = (unsigned char *)malloc(left_block_size);
    std::copy(left_block_bwt_ptr, left_block_bwt_ptr + left_block_size, left_block_bwt);
    fprintf(stderr, "%.2Lfs\n", utils::wclock() - left_bwt_copy_start);
  }

  // 2.f
  //
  // Write gt_begin of the left half-block to disk.
  if (!first_block) {
    fprintf(stderr, "    Write gt_begin to disk: ");
    long double left_gt_begin_rev_save_start = utils::wclock();
    std::string left_block_gt_begin_rev_fname = output_filename + "." + utils::random_string_hash();
    left_block_gt_begin_rev_bv->save(left_block_gt_begin_rev_fname);
    newtail_gt_begin_rev->add_file(text_length - left_block_end, text_length - left_block_beg, left_block_gt_begin_rev_fname);
    delete left_block_gt_begin_rev_bv;
    long double left_gt_begin_rev_save_time = utils::wclock() - left_gt_begin_rev_save_start;
    long double left_gt_begin_rev_save_io = (left_block_size / (8.L * (1 << 20))) / left_gt_begin_rev_save_time;
    fprintf(stderr, "%.2Lfs (I/O: %.2LfMiB/s)\n", left_gt_begin_rev_save_time, left_gt_begin_rev_save_io);
  }

#ifdef DROP_CACHE
  utils::drop_cache();
#endif


  //----------------------------------------------------------------------------
  // STEP 3: Compute the partial SA of the block.
  //
  // At this point in RAM we still have the left half-block, its partial SA and
  // BWT occupying 7 * left_block_size bytes in total (assuming 5-byte integers).
  //
  // There are 2 cases.
  // I  if the right half-block was empty, we just write the partial suffix
  //    array of the left half-block to disk (as a distributed file).
  // II otherwise, we first we compute the gap array of the left half-block wrt
  //    to the right half-block and then merge the partial suffix arrays of the
  //    half-blocks. Note that the partial SA of the left half-block is already
  //    in memory.
  //----------------------------------------------------------------------------
  if (right_block_size == 0) {
    hblock_info.push_back(info_left);
    free(left_block);
    free(left_block_sabwt);
    return;
  }

  //----------------------------------------------------------------------------
  // STEP 3: Compute the gap array of the left half-block wrt to the
  //         right half-block.
  //----------------------------------------------------------------------------
  fprintf(stderr, "  Compute partial gap array for left half-block:\n");
  buffered_gap_array *left_block_gap = NULL;

  // 3.a
  //
  // Compute initial ranks for streaming of the right half-block.
  // Note the space usage.
  fprintf(stderr, "    Compute initial ranks: ");
  long double initial_ranks_right_half_block_start = utils::wclock();
  std::vector<std::uint64_t> initial_ranks2;
  std::uint64_t stream_block_size =
    (right_block_size + max_threads - 1) / max_threads;
  compute_ranks<block_offset_type>(
      left_block, left_block_bwt, left_block_psa_ptr,
      right_block_gt_begin_rev, text_filename,
      left_block_i0, left_block_beg, left_block_end,
      text_length, stream_block_size, right_block_end,
      after_block_initial_rank, initial_ranks2);

  std::uint64_t vec_size = initial_ranks2.size();
  for (std::uint64_t j = 0; j + 1 < vec_size; ++j)
    initial_ranks2[j] = initial_ranks2[j + 1];
  initial_ranks2[vec_size - 1] = after_block_initial_rank;

  fprintf(stderr, "%.2Lfs\n", utils::wclock() - initial_ranks_right_half_block_start);
  free(left_block);
  free(left_block_sabwt);

  // 3.b
  //
  // Build the rank over BWT of left half-block.
  // RAM: left_block_sabwt, handles to right block psa and gt_begin.
  fprintf(stderr, "    Construct rank: ");
  long double left_block_rank_build_start = utils::wclock();
  rank4n<> *left_block_rank = new rank4n<>(left_block_bwt, left_block_size, max_threads);
  long double left_block_rank_build_time = utils::wclock() - left_block_rank_build_start;
  long double left_block_rank_build_speed = (left_block_size / (1024.L * 1024)) / left_block_rank_build_time;
  fprintf(stderr, "%.2Lfs (%.2LfMiB/s)\n", left_block_rank_build_time, left_block_rank_build_speed);

#ifdef DROP_CACHE
  utils::drop_cache();
#endif

  // 3.c
  //
  // Compute gap array of the left half-block wrt to the right half-block.
  // RAM: left_block_rank, left_block_sabwt, handles to right block psa and gt_begin.
  left_block_gap = new buffered_gap_array(left_block_size + 1, gap_filename);
  compute_gap<block_offset_type>(
      left_block_rank, left_block_size, left_block_gap,
      right_block_beg, right_block_end, text_length,
      max_threads, left_block_i0, gap_buf_size,
      left_block_last, initial_ranks2, text_filename,
      output_filename, right_block_gt_begin_rev,
      newtail_gt_begin_rev);
  delete left_block_rank;
  delete right_block_gt_begin_rev;

#ifdef DROP_CACHE
  utils::drop_cache();
#endif


  //----------------------------------------------------------------------------
  // The computation continues only if the block under consideration is not the
  // last block of the text.
  //----------------------------------------------------------------------------
  if (last_block) {
    free(left_block_bwt);

    // INVARIANT:
    //   The gap of the left half-block wrt to the right half-block is
    //   the gap of the left half-block wrt to the whole tail and right_block_size > 0.
    // What we should do in this situation, is to write the gap to disk
    // and update the information about the gap array filename in info_left.
    info_left.gap_filename = gap_filename + ".gap." + utils::random_string_hash();
    left_block_gap->save_to_file(info_left.gap_filename);
    left_block_gap->erase_disk_excess();
    delete left_block_gap;

    hblock_info.push_back(info_left);
    hblock_info.push_back(info_right);
    return;
  }

#ifdef DROP_CACHE
  utils::drop_cache();
#endif

  //----------------------------------------------------------------------------
  // STEP 4: Compute the BWT for the block.
  //
  // RAM:
  // - handle to block_psa,
  // - left_block_sabwt containing the count array of the left_block_gap (which
  //   is the gap array of the left half-block wrt to the right half-block).
  //
  // DISK:
  // - if the block under consideration is not the last block of the text,
  //   at this point the partial BWT of the left and right half-block is
  //   stored in disk as a regular file and is accesible via filenames
  //   left_block_pbwt_fname and right_block_pbwt_fname. Otherwise, the
  //   files were not created.
  //
  // ADDITIONAL VALUES:
  // - when computing the BWT of the block, we also need to know i0 values
  //   for the BWTs of the left and right half-blocks. This is necessary to
  //     1) replace the occurrence of 0 in the BWT of the right half-block
  //        with left_block_last,
  //     2) identify position i0 for the BWT of the block.
  //
  // INVARIANT:
  // - at this point we have right_block_size > 0. The other case would not
  //   reach this point,
  // - sequential access of the left_block_gap is still initialized (i.e.,
  //   excess values are in RAM).
  //----------------------------------------------------------------------------
  fprintf(stderr, "  Compute block gap array:\n");

  // 4.a
  //
  // Convert the partial gap of the left half-block into bitvector.
  fprintf(stderr, "    Convert partial gap array of left half-block to bitvector: ");
  long double convert_to_bitvector_start = utils::wclock();
  bitvector *left_block_gap_bv = left_block_gap->convert_to_bitvector(max_threads);
  long double convert_to_bitvector_time = utils::wclock() - convert_to_bitvector_start;
  long double convert_to_bitvector_speed = (block_size / (1024.L * 1024)) / convert_to_bitvector_time;
  fprintf(stderr, "%.2Lfs (%.2LfMiB/s)\n", convert_to_bitvector_time, convert_to_bitvector_speed);

  left_block_gap->erase_disk_excess();
  delete left_block_gap;

#ifdef DROP_CACHE
  utils::drop_cache();
#endif

  // 4.b
  //
  // Read the BWT of the right half-block into RAM.
  fprintf(stderr, "    Read BWT of right half-block: ");
  long double right_block_bwt_read_start = utils::wclock();
  std::uint8_t *right_block_bwt = (std::uint8_t *)malloc(right_block_size);
  utils::read_from_file(right_block_bwt, right_block_size, right_block_pbwt_fname);
  long double right_block_bwt_read_time = utils::wclock() - right_block_bwt_read_start;
  long double right_block_bwt_read_io = (right_block_size / (1024.L * 1024)) / right_block_bwt_read_time;
  fprintf(stderr, "%.2Lfs (I/O: %.2LfMiB/s)\n", right_block_bwt_read_time, right_block_bwt_read_io);

  utils::file_delete(right_block_pbwt_fname);

  unsigned char *block_pbwt = (unsigned char *)malloc(block_size);
  long block_i0 = 0;

  // 4.c
  //
  // Merge BWTs of left and right half-block.
  fprintf(stderr, "    Merge BWTs of half-blocks: ");
  long double bwt_merge_start = utils::wclock();
  block_i0 =
    merge_bwt(left_block_bwt, right_block_bwt, left_block_gap_bv,
        left_block_size, right_block_size, left_block_i0,
        right_block_i0, left_block_last, block_pbwt);

  long double bwt_merge_time = utils::wclock() - bwt_merge_start;
  long double bwt_merge_speed = (block_size / (1024.L * 1024)) / bwt_merge_time;
  fprintf(stderr, "%.2Lfs (%.2LfMiB/s)\n", bwt_merge_time, bwt_merge_speed);

  free(left_block_bwt);
  free(right_block_bwt);

  // 4.d
  //
  // Write left_block_gap_bv to disk.
  fprintf(stderr, "    Write left half-block gap bitvector to disk: ");
  long double write_left_gap_bv_start = utils::wclock();
  std::string left_block_gap_bv_filename = gap_filename + ".left_block_gap_bv";
  left_block_gap_bv->save(left_block_gap_bv_filename);
  delete left_block_gap_bv;
  long double write_left_gap_bv_time = utils::wclock() - write_left_gap_bv_start;
  long double write_left_gap_bv_io = ((block_size / 8.L) / (1 << 20)) / write_left_gap_bv_time;
  fprintf(stderr, "%.2Lfs (I/O: %.2LfMiB/s)\n", write_left_gap_bv_time, write_left_gap_bv_io);

#ifdef DROP_CACHE
  utils::drop_cache();
#endif

  //----------------------------------------------------------------------------
  // STEP 5: Compute the gap array of the block.
  //
  // RAM:
  // - BWT of the block (block_size bytes)
  // - additional values necessary for streaming:
  //   * last symbol of the block (block_last),
  //   * block i0,
  //   * initial_ranks.
  //----------------------------------------------------------------------------

  // 5.a
  //
  // Construct the rank data structure over BWT of the block.
  fprintf(stderr, "    Construct rank: ");
  long double whole_block_rank_build_start = utils::wclock();
  rank4n<> *block_rank = new rank4n<>(block_pbwt, block_size, max_threads);
  free(block_pbwt);
  long double whole_block_rank_build_time = utils::wclock() - whole_block_rank_build_start;
  long double whole_block_rank_build_io = (block_size / (1024.L * 1024)) / whole_block_rank_build_time;
  fprintf(stderr, "%.2Lfs (%.2LfMiB/s)\n", whole_block_rank_build_time, whole_block_rank_build_io);

  buffered_gap_array *block_gap = new buffered_gap_array(block_size + 1, gap_filename);

  // 5.b
  //
  // Compute gap for the block. During this step we also compute gt_begin
  // for the new tail.
  // RAM: block_rank, block_gap_array, block_gap.
  compute_gap<block_offset_type>(block_rank, block_size, block_gap, block_tail_beg, block_tail_end, text_length,
      max_threads, block_i0, gap_buf_size, block_last_symbol, block_initial_ranks, text_filename,
      output_filename, tail_gt_begin_rev, newtail_gt_begin_rev);
  delete block_rank;

  block_gap->flush_excess_to_disk();

  // 5.c
  //
  // Read left_block_gap_bv from disk.
  fprintf(stderr, "    Read left half-block gap bitvector from disk: ");
  long double left_block_gap_bv_read_start = utils::wclock();
  left_block_gap_bv = new bitvector(left_block_gap_bv_filename);
  long double left_block_gap_bv_read_time = utils::wclock() - left_block_gap_bv_read_start;
  long double left_block_gap_bv_read_io = ((block_size / 8.L) / (1 << 20)) / left_block_gap_bv_read_time;
  fprintf(stderr, "%.2Lfs (I/O: %.2LfMiB/s)\n", left_block_gap_bv_read_time, left_block_gap_bv_read_io);
  utils::file_delete(left_block_gap_bv_filename);

  //----------------------------------------------------------------------------
  // STEP 6: Compute gap arrays of half-blocks.
  //
  // At this point we know that right_block_size > 0 and the current block
  // is not the last one in the text. The task now is to compute the gap
  // arrays for the left and right half-blocks from the pseudo gap array for
  // the left half-block and the gap array of the whole block. They are streamed
  // directly to disk and after we're done we update the information about the
  // location of gap arrays into info_left and info_right structures.
  //----------------------------------------------------------------------------
  info_left.gap_filename = gap_filename + ".gap." + utils::random_string_hash();
  info_right.gap_filename = gap_filename + ".gap." + utils::random_string_hash();

  gap_array_2n *block_gap_2n = new gap_array_2n(block_gap);
  delete block_gap;
  block_gap_2n->apply_excess_from_disk(std::max((1UL << 20), block_size), max_threads);

  long ram_budget = std::max(1L << 20, (long)(0.875L * block_size));
  compute_right_gap(left_block_size, right_block_size, block_gap_2n, left_block_gap_bv, info_right.gap_filename, max_threads, ram_budget);  
  compute_left_gap(left_block_size, right_block_size, block_gap_2n, left_block_gap_bv, info_left.gap_filename, max_threads, ram_budget);

  block_gap_2n->erase_disk_excess();

  delete block_gap_2n;
  delete left_block_gap_bv;
  
  hblock_info.push_back(info_left);
  hblock_info.push_back(info_right);

#ifdef DROP_CACHE
  utils::drop_cache();
#endif
}


//=============================================================================
// Compute partial SAs and gap arrays and write to disk.
// Return the array of handlers to distributed files as a result.
//=============================================================================
template<typename block_offset_type>
std::vector<half_block_info<block_offset_type> > partial_sufsort(std::string text_filename, std::string output_filename,
    std::string gap_filename, long text_length, long max_block_size, long ram_use, long max_threads, long gap_buf_size,
    bool verbose) {
  fprintf(stderr, "sizeof(block_offset_type) = %lu\n\n", sizeof(block_offset_type));

  long n_blocks = (text_length + max_block_size - 1) / max_block_size;
  multifile *tail_gt_begin_reversed = NULL;

  std::vector<half_block_info<block_offset_type> > hblock_info;
  for (long block_id = n_blocks - 1; block_id >= 0; --block_id) {
    long block_beg = max_block_size * block_id;
    long block_end = std::min(block_beg + max_block_size, text_length);
    fprintf(stderr, "Process block %ld/%ld [%ld..%ld):\n", n_blocks - block_id, n_blocks, block_beg, block_end);

    multifile *newtail_gt_begin_reversed = new multifile();
    process_block<block_offset_type>(block_beg, block_end, text_length, ram_use, max_threads, gap_buf_size,
        text_filename, output_filename, gap_filename, newtail_gt_begin_reversed, tail_gt_begin_reversed,
        hblock_info, verbose);

    delete tail_gt_begin_reversed;
    tail_gt_begin_reversed = newtail_gt_begin_reversed;
  }

  delete tail_gt_begin_reversed;
  return hblock_info;
}

}  // namespace psascan_private

#endif // __SRC_PSASCAN_SRC_PARTIAL_SUFSORT_HPP_INCLUDED
