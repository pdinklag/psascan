#ifndef __GAP_ARRAY_H_INCLUDED
#define __GAP_ARRAY_H_INCLUDED

#include <cstdio>
#include <cstdlib>
#include <vector>
#include <mutex>
#include <algorithm>

#include "utils.h"
#include "stream.h"

std::mutex gap_writing;

struct buffered_gap_array {
  buffered_gap_array(long n, long sections) {
    if (n <= 0L) {
      fprintf(stderr, "Error: attempting to construct empty gap array.\n");
      std::exit(EXIT_FAILURE);
    }

    m_length = n;
    m_sections = sections;
    m_count = new unsigned char[m_length];
    std::fill(m_count, m_count + m_length, 0);

    m_excess        = new long*[m_sections];
    m_excess_filled = new long[m_sections];
    m_total_excess  = new long[m_sections];
    for (long i = 0L; i < m_sections; ++i) m_excess[i] = new long[k_excess_limit];
    for (long i = 0L; i < m_sections; ++i) m_excess_filled[i] = 0L;
    for (long i = 0L; i < m_sections; ++i) m_total_excess[i] = 0L;

    // File used to store excess values.
    storage_filename = "excess." + utils::random_string_hash();
  }

  inline void increment(long *buf, long elems, unsigned char which) {
    long *excess = m_excess[which];
    long &excess_filled = m_excess_filled[which];

    int section_size = m_length / m_sections;
    long start = which * section_size;
    long end = (which == m_sections - 1) ? m_length : start + section_size;
    long old_filled = excess_filled;

    for (long i = 0; i < elems; ++i) {
      if (start <= buf[i] && buf[i] < end) {
        long pos = buf[i];
        ++m_count[pos];
        if (!m_count[pos]) {
          excess[excess_filled++] = pos;
          if (excess_filled == k_excess_limit) {
            gap_writing.lock();
            utils::add_objects_to_file(excess, excess_filled, storage_filename);
            excess_filled = 0L;
            gap_writing.unlock();
          }
        }
      }
    }

    m_total_excess[which] += m_excess_filled[which] - old_filled;
  }

  ~buffered_gap_array() {
    delete[] m_count;
    for (int i = 0; i < m_sections; ++i)
      delete[] m_excess[i];
    delete[] m_excess;
    delete[] m_excess_filled;
    delete[] m_total_excess;

    if (utils::file_exists(storage_filename))
      utils::file_delete(storage_filename);
  }
  
  // Store to file using v-byte encoding.
  void save_to_file(std::string fname) {
    long excess_filled = 0L, total_excess = 0L;
    for (long i = 0L; i < m_sections; ++i) {
      excess_filled += m_excess_filled[i];
      total_excess += m_total_excess[i];
    }
    fprintf(stderr, "  Gap excess (inmem): %ld\n", excess_filled);
    fprintf(stderr, "  Gap excess (disk): %ld\n", total_excess - excess_filled);
    fprintf(stderr, "  Saving gap to file: ");
    long double gap_save_start = utils::wclock();

    // Gather all excess values together,
    // including ones stored on disk (if any).
    long *sorted_excess = new long[total_excess];
    long filled = 0L;
    for (long i = 0L; i < m_sections; ++i) {
      std::copy(m_excess[i], m_excess[i] + m_excess_filled[i], sorted_excess + filled);
      filled += m_excess_filled[i];
    }
    if (total_excess != excess_filled) {
      long *dest = sorted_excess + excess_filled;
      long toread = total_excess - excess_filled;
      utils::read_n_objects_from_file(dest, toread, storage_filename.c_str());
    }

    // Sort the excess values.
    std::sort(sorted_excess, sorted_excess + total_excess);

    // Write gap values to file.
    stream_writer<unsigned char> *writer = new stream_writer<unsigned char>(fname);
    for (long j = 0, pos = 0; j < m_length; ++j) {
      long c = 0;
      while (pos < total_excess && sorted_excess[pos] == j)
        ++pos, ++c;

      long gap_j = m_count[j] + (c << 8);
      while (gap_j > 127) {
        writer->write((gap_j & 0x7f) | 0x80);
        gap_j >>= 7;
      }

      writer->write(gap_j);
    }
    delete writer;
    delete[] sorted_excess;
    fprintf(stderr, "%.2Lf\n", utils::wclock() - gap_save_start);
  }


  unsigned char *m_count;

  long m_length;
  long m_sections;

  static const int k_excess_limit = (1 << 17);
  long **m_excess, *m_excess_filled, *m_total_excess;

  std::string storage_filename;
};

#endif // __GAP_ARRAY_H_INCLUDED

