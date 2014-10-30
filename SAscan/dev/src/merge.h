#ifndef __MERGE_H_INCLUDED
#define __MERGE_H_INCLUDED

#include <string>
#include <algorithm>

#include "utils.h"
#include "io_streamer.h"
#include "uint40.h"
#include "distributed_file.h"

// Merge partial suffix arrays into final suffix array (stored in normal file).
// INVARIANT: 5.2 * length <= ram_use.
template<typename block_offset_type>
void merge(std::string output_filename,
           long length,
           long max_block_size,
           long ram_use,
           distributed_file<block_offset_type> **sparseSA) {

  long n_block = (length + max_block_size - 1) / max_block_size;
  long pieces = (1 + sizeof(block_offset_type)) * n_block - 1 + sizeof(uint40);
  long buffer_size = (ram_use + pieces - 1) / pieces;

  fprintf(stderr, "\nBuffer size for merging: %ld\n", buffer_size);
  fprintf(stderr, "sizeof(output_type) = %ld\n", sizeof(uint40));

  stream_writer<uint40> *output = new stream_writer<uint40>(output_filename, sizeof(uint40) * buffer_size);
  vbyte_stream_reader **gap = new vbyte_stream_reader*[n_block - 1];
  for (long i = 0; i < n_block; ++i) {
    sparseSA[i]->initialize_reading(sizeof(block_offset_type) * buffer_size);
    if (i + 1 != n_block)
      gap[i] = new vbyte_stream_reader(output_filename + ".gap." + utils::intToStr(i), buffer_size);
  }

  long *gap_head = new long[n_block];
  for (long i = 0; i + 1 < n_block; ++i)
    gap_head[i] = gap[i]->read();
  gap_head[n_block - 1] = 0;

  fprintf(stderr, "Merging:\r");
  long double merge_start = utils::wclock();
  for (long i = 0, dbg = 0; i < length; ++i, ++dbg) {
    if (dbg == (1 << 23)) {
      long double elapsed = utils::wclock() - merge_start;
      long double scanned_m = i / (1024.L * 1024);
      long inp_vol = (1L + sizeof(uint40)) * i;
      long out_vol = sizeof(uint40) * i;
      long tot_vol = inp_vol + out_vol;
      long double tot_vol_m = tot_vol / (1024.L * 1024);
      long double io_speed = tot_vol_m / elapsed;
      fprintf(stderr, "Merging: %.1Lf%%, time = %.2Lfs (%.3Lfs/MiB), io = %2.LfMiB/s\r",
          (100.L * i) / length, elapsed, elapsed / scanned_m, io_speed);
      dbg = 0;
    }

    long k = 0;
    while (gap_head[k]) --gap_head[k++];
    if (k != n_block - 1) gap_head[k] = gap[k]->read();
    long SA_i = sparseSA[k]->read() + k * max_block_size;
    output->write(SA_i);
  }
  long double merge_time = utils::wclock() - merge_start;
  fprintf(stderr, "Merging: 100.0%%. Time: %.2Lfs\n", merge_time);

  // Clean up.
  delete output;
  for (long i = 0; i < n_block; ++i) {
    sparseSA[i]->finish_reading();
    delete sparseSA[i];
    if (i + 1 != n_block)
      delete gap[i];
  }

  delete[] sparseSA;
  delete[] gap;
  delete[] gap_head;
  
  for (int i = 0; i < n_block; ++i)
    if (i + 1 != n_block)
      utils::file_delete(output_filename + ".gap." + utils::intToStr(i));
}


#endif // __MERGE_H_INCLUDED

