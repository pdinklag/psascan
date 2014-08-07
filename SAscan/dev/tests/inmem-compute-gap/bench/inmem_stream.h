// Parallel backward search.
#ifndef __STREAM_H_INCLUDED
#define __STREAM_H_INCLUDED

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <iostream>
#include <queue>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <algorithm>

#include "bitvector.h"
#include "utils.h"
#include "rank.h"
#include "buffer.h"
#include "inmem_update.h"
#include "stream_info.h"

template<typename block_offset_type>
void inmem_parallel_stream(
    unsigned char *text,
    long stream_block_beg,
    long stream_block_end,
    unsigned char last,
    long *count,
    buffer_poll<block_offset_type> *full_buffers,
    buffer_poll<block_offset_type> *empty_buffers,
    block_offset_type i,
    block_offset_type i0,
    rank4n<> *rank,
    long gap_range_size,
    long stream_buf_size,
    long n_increasers,
    bitvector *gt,
    long gt_origin,
    stream_info *info,
    int thread_id) {

  //----------------------------------------------------------------------------
  // STEP 1: initialize structures necessary to do the buffer partitions.
  //----------------------------------------------------------------------------
  static const int max_buckets = 4092;
  int *block_id_to_sblock_id = new int[max_buckets];

  long bucket_size = 1;
  long bucket_size_bits = 0;
  while ((gap_range_size + bucket_size - 1) / bucket_size > max_buckets)
    bucket_size <<= 1, ++bucket_size_bits;
  long n_buckets = (gap_range_size + bucket_size - 1) / bucket_size;
  int *block_count = new int[n_buckets];

  long max_buffer_elems = stream_buf_size / sizeof(block_offset_type);
  block_offset_type *temp = new block_offset_type[max_buffer_elems];
  int *oracle = new int[max_buffer_elems];

  static const long buffer_sample_size = 512;
  std::vector<block_offset_type> samples(buffer_sample_size);
  long *ptr = new long[n_increasers];
  block_offset_type *bucket_lbound = new block_offset_type[n_increasers + 1];

  //----------------------------------------------------------------------------
  // STEP 2: perform the actual streaming.
  //----------------------------------------------------------------------------
  long j = stream_block_end, dbg = 0L;
  while (j > stream_block_beg) {
    if (dbg > (1 << 26)) {
      info->m_mutex.lock();
      info->m_streamed[thread_id] = stream_block_end - j;
      info->m_update_count += 1;
      if (info->m_update_count == info->m_thread_count) {
        info->m_update_count = 0L;
        long double elapsed = utils::wclock() - info->m_timestamp;
        long total_streamed = 0L;

        for (long t = 0; t < info->m_thread_count; ++t)
          total_streamed += info->m_streamed[t];
        long double speed = (total_streamed / (1024.L * 1024)) / elapsed;

        fprintf(stderr, "\r  [PARALLEL]Stream: %.2Lf%%. Time: %.2Lf. Threads: %ld. "
            "Speed: %.2LfMiB/s (avg), %.2LfMiB/s (total)",
            (total_streamed * 100.L) / info->m_tostream, elapsed,
            info->m_thread_count, speed / info->m_thread_count, speed);
      }
      info->m_mutex.unlock();
      dbg = 0L;
    }


    //--------------------------------------------------------------------------
    // Get a buffer from the poll of empty buffers.
    //--------------------------------------------------------------------------
    std::unique_lock<std::mutex> lk(empty_buffers->m_mutex);
    while (!empty_buffers->available()) empty_buffers->m_cv.wait(lk);
    buffer<block_offset_type> *b = empty_buffers->get();
    lk.unlock();
    empty_buffers->m_cv.notify_one();

    //--------------------------------------------------------------------------
    // Process buffer, i.e., fill with gap values.
    //--------------------------------------------------------------------------
    long left = j - stream_block_beg;
    b->m_filled = std::min(left, b->m_size);
    dbg += b->m_filled;
    std::fill(block_count, block_count + n_buckets, 0);
    for (long t = 0; t < b->m_filled; ++t, --j) {
      bool gt_bit = gt->get(j - gt_origin);
      unsigned char c = text[j - 1];
      i = (block_offset_type)(count[c] + rank->rank((long)(i - (i > i0)), c));
      if (c == last && gt_bit) ++i;
      temp[t] = i;
      block_count[i >> bucket_size_bits]++;
    }

    //--------------------------------------------------------------------------
    // Partition the buffer into equal n_increasers parts.
    //--------------------------------------------------------------------------

    // Compute super-buckets.
    long ideal_sblock_size = (b->m_filled + n_increasers - 1) / n_increasers;
    long max_sbucket_size = 0;
    long bucket_id_beg = 0;
    for (long t = 0; t < n_increasers; ++t) {
      long bucket_id_end = bucket_id_beg, size = 0L;
      while (bucket_id_end < n_buckets && size < ideal_sblock_size)
        size += block_count[bucket_id_end++];
      b->sblock_size[t] = size;
      max_sbucket_size = std::min(max_sbucket_size, size);
      for (long id = bucket_id_beg; id < bucket_id_end; ++id)
        block_id_to_sblock_id[id] = t;
      bucket_id_beg = bucket_id_end;
    }

    if (max_sbucket_size < 4L * ideal_sblock_size) {
      for (long t = 0, curbeg = 0; t < n_increasers; curbeg += b->sblock_size[t++])
        b->sblock_beg[t] = ptr[t] = curbeg;

      // Permute the elements of the buffer.
      for (long t = 0; t < b->m_filled; ++t) {
        long id = (temp[t] >> bucket_size_bits);
        long sblock_id = block_id_to_sblock_id[id];
        oracle[t] = ptr[sblock_id]++;
      }

      for (long t = 0; t < b->m_filled; ++t) {
        long addr = oracle[t];
        b->m_content[addr] = temp[t];
      }
    } else {
      // Repeat the partition into sbuckets, this time using random sample.
      // This is a fallback mechanism in case the quick partition failed.
      // It is not suppose to happen to often.

      // Compute random sample of elements in the buffer.
      for (long t = 0; t < buffer_sample_size; ++t)
        samples[t] = temp[utils::random_long(0L, b->m_filled - 1)];
      std::sort(samples.begin(), samples.end());
      samples.erase(std::unique(samples.begin(), samples.end()), samples.end());

      // Compute bucket boundaries (lower bound is enough).
      std::fill(bucket_lbound, bucket_lbound + n_increasers + 1, gap_range_size);

      long step = (samples.size() + n_increasers - 1) / n_increasers;
      for (size_t t = 1, p = step; p < samples.size(); ++t, p += step)
        bucket_lbound[t] = (samples[p - 1] + samples[p] + 1) / 2;
      bucket_lbound[0] = 0;

      // Compute bucket sizes and sblock id into oracle array.
      std::fill(b->sblock_size, b->sblock_size + n_increasers, 0L);
      for (long t = 0; t < b->m_filled; ++t) {
        block_offset_type x = temp[t];
        int id = n_increasers;
        while (bucket_lbound[id] > x) --id;
        oracle[t] = id;
        b->sblock_size[id]++;
      }

      // Permute elements into their own buckets using oracle.
      for (long t = 0, curbeg = 0; t < n_increasers; curbeg += b->sblock_size[t++])
        b->sblock_beg[t] = ptr[t] = curbeg;

      for (long t = 0; t < b->m_filled; ++t) {
        long sblock_id = oracle[t];
        oracle[t] = ptr[sblock_id]++;
      }

      for (long t = 0; t < b->m_filled; ++t) {
        long addr = oracle[t];
        b->m_content[addr] = temp[t];
      }
    }

    //--------------------------------------------------------------------------
    // Add the buffer to the poll of full buffers and notify waiting thread.
    //--------------------------------------------------------------------------
    std::unique_lock<std::mutex> lk2(full_buffers->m_mutex);
    full_buffers->add(b);
    lk2.unlock();
    full_buffers->m_cv.notify_one();
  }

  // Report that another worker thread has finished.
  std::unique_lock<std::mutex> lk(full_buffers->m_mutex);
  full_buffers->increment_finished_workers();
  lk.unlock();

  // Notify waiting update threads in case no more buffers
  // are going to be produces by worker threads.
  full_buffers->m_cv.notify_one();

  delete[] block_count;
  delete[] block_id_to_sblock_id;
  delete[] temp;
  delete[] oracle;
  delete[] ptr;
  delete[] bucket_lbound;
}


#endif  // __STREAM_H_INCLUDED