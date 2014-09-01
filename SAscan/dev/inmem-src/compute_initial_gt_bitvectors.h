#ifndef __COMPUTE_INITIAL_GT_BITVECTORS_H
#define __COMPUTE_INITIAL_GT_BITVECTORS_H

#include <cstring>
#include <algorithm>
#include <thread>

#include "bitvector.h"
#include "srank_aux.h"


//==============================================================================
// Compute bitvectors bv[0..ref_pos) and undecided[0..ref_pos), where:
//   undecided[i] == 0 iff lcp(text[0..txtlen), text[ref_pos..txtlen)) < max_lcp
//   gt[i] == 1 iff undecided[i] == 0 and text[0..txtlen) > text[ref_pos..txtlen)
//
// NOTE: we store the gt bitvector reversed, so that later we can overwrite
// it with gt_begin in place.
//==============================================================================
void compute_partial_gt_end(unsigned char *text, long text_length, long begin,
    long end, long max_lcp, bitvector *gt, bitvector *undecided, bool &all_decided) {
  long i = 0, el = 0, s = 0, p = 0, r = 0;
  long i_max = 0, el_max = 0, s_max = 0, p_max = 0, r_max = 0;

  unsigned char *txt = text + begin;
  unsigned char *pat = text + end;
  long range_size = end - begin;

  all_decided = true;
  while (i < range_size) {
    while (el < max_lcp && txt[i + el] == pat[el])
      next(pat, ++el, s, p, r);
     
    if (el < max_lcp) {
      if (txt[i + el] > pat[el]) gt->set(end - i - 1);
    } else if (end + el == text_length) {
      gt->set(end - i - 1);
    } else {
      undecided->set(begin + i);
      all_decided = false;
    }

    long j = i_max;
    if (el > el_max) {
      std::swap(el, el_max);
      std::swap(s, s_max);
      std::swap(p, p_max);
      std::swap(r, r_max);
      i_max = i;
    }

    if (el < 100) {
      ++i;
      el = 0;
    } else if (p > 0 && (p << 2) <= el && !memcmp(pat, pat + p, s)) {
      for (long k = 1; k < std::min(p, range_size - i); ++k) {
        if (undecided->get(begin + j + k)) undecided->set(begin + i + k);
        if (gt->get(end - (j + k) - 1)) gt->set(end - (i + k) - 1);
      }

      i += p;
      el -= p;
    } else {
      long h = (el >> 2) + 1;
      for (long k = 1; k < std::min(h, range_size - i); ++k) {
        if (undecided->get(begin + j + k)) undecided->set(begin + i + k);
        if (gt->get(end - (j + k) - 1)) gt->set(end - (i + k) - 1);
      }

      i += h;
      el = 0;
      s = 0;
      p = 0;
    }
  }
}


//==============================================================================
// Set all undecided bits inside the given microblock (that is, the range
// [mb_beg..mb_end)) of all gt bitvectors to their correct values.
//==============================================================================
void compute_final_gt(long length, long max_block_size,
    long mb_beg, long mb_end, bitvector* &gt, bitvector* &undecided,
    bool *all_decided) {

  // Go through blocks right to left.
  long n_blocks = (length + max_block_size - 1) / max_block_size;
  for (long i = n_blocks - 1, next_block_end = length; i >= 0; --i) {
    long block_beg = i * max_block_size;
    long block_end = std::min(block_beg + max_block_size, length);
    long this_block_size = block_end - block_beg;
    long this_mb_end = std::min(this_block_size, mb_end);

    if (!all_decided[i]) {
      // Scan the bits inside the microblock of block i.
      for (long j = mb_beg; j < this_mb_end; ++j) {
        if (undecided->get(block_beg + j)) {
          // j-th bit of gt[i] was undecided -> copy it from the right.
          if (gt->get(next_block_end - j - 1))
            gt->set(block_end - j - 1);
        }
      }
    }

    next_block_end = block_end;
  }
}


//==============================================================================
// Fully parallel computation of gt bitvectors.
//==============================================================================
void compute_initial_gt_bitvectors(unsigned char *text, long length,
    bitvector* &gt, long max_blocks, long max_threads) {
  long double start;
  long max_block_size = (length + max_blocks - 1) / max_blocks;
  while (max_block_size & 7) ++max_block_size;
  long n_blocks = (length + max_block_size - 1) / max_block_size;


  //----------------------------------------------------------------------------
  // STEP 1: compute gt bitvectors, some bits may still be undecided after this.
  //----------------------------------------------------------------------------

  // Allocate ane zero-initialize (in parallel) bitvectors.
  fprintf(stderr, "  Allocating: ");
  start = utils::wclock();
  gt = new bitvector(length, max_threads);
  bitvector *undecided = new bitvector(length, max_threads);
  fprintf(stderr, "%.2Lf\n", utils::wclock() - start);

  // all_decided[i] == true, if all bits inside block i were
  // decided in the first state. This can be used by threads in the
  // second stage to completely skip inspecting some blocks.
  bool *all_decided = new bool[n_blocks];

  // Process blocks right-to-left.
  fprintf(stderr, "  Computing decided bits: ");
  start = utils::wclock();
  std::thread **threads = new std::thread*[n_blocks];
  for (long i = n_blocks - 1, next_block_size = 0L; i >= 0; --i) {
    long beg = i * max_block_size;
    long end = std::min(beg + max_block_size, length);
    long this_block_size = end - beg;

    // Compute bitvectors 'gt' and 'undecided' for block i.
    threads[i] = new std::thread(compute_partial_gt_end,
        text, length, beg, end, next_block_size, gt,
        undecided, std::ref(all_decided[i]));

    next_block_size = this_block_size;
  }

  // Wait for the threads to finish and clean up.
  for (long i = 0; i < n_blocks; ++i) threads[i]->join();
  for (long i = 0; i < n_blocks; ++i) delete threads[i];
  delete[] threads;
  fprintf(stderr, "%.2Lf\n", utils::wclock() - start);

  //----------------------------------------------------------------------------
  // STEP 2: compute the undecided bits in the gt bitvectors.
  //----------------------------------------------------------------------------
  
  // The size of micro block has to be a multiple of 8, otherwise two
  // threads might try to update the same char inside bitvector.
  long max_microblock_size = (max_block_size + max_threads - 1) / max_threads;
  while (max_microblock_size & 7) ++max_microblock_size;
  long n_microblocks = (max_block_size + max_microblock_size - 1) / max_microblock_size;

  fprintf(stderr, "  Computing undecided bits: ");
  start = utils::wclock();
  threads = new std::thread*[n_microblocks];
  for (long i = 0; i < n_microblocks; ++i) {
    long mb_beg = i * max_microblock_size;
    long mb_end = (i + 1) * max_microblock_size;
    threads[i] = new std::thread(compute_final_gt, length,
        max_block_size, mb_beg, mb_end, std::ref(gt),
        std::ref(undecided), all_decided);
  }

  // Wait for the threads to finish and clean up.
  for (long i = 0; i < n_microblocks; ++i) threads[i]->join();
  for (long i = 0; i < n_microblocks; ++i) delete threads[i];
  fprintf(stderr, "%.2Lf\n", utils::wclock() - start);

  fprintf(stderr, "  Deallocating: ");
  start = utils::wclock();
  delete[] threads;
  delete undecided;
  delete[] all_decided;
  fprintf(stderr, "%.2Lf\n", utils::wclock() - start);
}



#endif  // __COMPUTE_INITIAL_GT_BITVECTORS_H