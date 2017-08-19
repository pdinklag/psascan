/**
 * @file    src/psascan_src/io/background_chunk_reader.hpp
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

#ifndef __SRC_PSASCAN_SRC_IO_BACKGROUND_CHUNK_READER_HPP_INCLUDED
#define __SRC_PSASCAN_SRC_IO_BACKGROUND_CHUNK_READER_HPP_INCLUDED

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "../utils.hpp"


namespace psascan_private {

class background_chunk_reader {
  private:
    std::FILE *m_file;
    std::uint64_t m_chunk_length;
    std::uint64_t m_end;
    
    std::condition_variable m_cv;
    std::mutex m_mutex;
    std::thread *m_thread;
    
    bool m_signal_read_next_chunk;
    bool m_signal_stop;

    std::uint64_t m_cur;
    std::uint8_t *m_passive_chunk;

  public:
    std::uint8_t *m_chunk;

  private:
    static void async_io_code(background_chunk_reader &r) {
      while (true) {
        std::unique_lock<std::mutex> lk(r.m_mutex);
        while (!r.m_signal_read_next_chunk && !r.m_signal_stop)
          r.m_cv.wait(lk);
          
        bool sig_stop = r.m_signal_stop;
        r.m_signal_read_next_chunk = false;
        lk.unlock();
        
        if (sig_stop) break;
        
        std::uint64_t next_chunk_length = std::min(r.m_chunk_length, r.m_end - r.m_cur);
        utils::read_from_file(r.m_passive_chunk, next_chunk_length, r.m_file);
        
        lk.lock();
        r.m_cur += next_chunk_length;
        lk.unlock();
        r.m_cv.notify_all();
      }
    }

  public:
    background_chunk_reader(std::string filename, std::uint64_t beg,
        std::uint64_t end, std::uint64_t chunk_length = (1UL << 20)) {
      if (beg > end) {
        fprintf(stderr, "Error: beg > end in background_chunk_reader.\n");
        std::exit(EXIT_FAILURE);
      }

      if (beg == end) return;

      m_cur = beg;
      m_end = end;
      m_chunk_length = chunk_length;
      m_chunk = (std::uint8_t *)malloc(m_chunk_length);
      m_passive_chunk = (std::uint8_t *)malloc(m_chunk_length);
      
      m_file = utils::file_open(filename, "r");
      std::fseek(m_file, m_cur, SEEK_SET);

      m_signal_stop = false;
      m_signal_read_next_chunk = true;
      m_thread = new std::thread(async_io_code, std::ref(*this));
    }

    inline void wait(std::uint64_t end) {
      if (end > m_end) {
        fprintf(stderr, "Error: end > m_end in background_chunk_reader.\n");
        std::exit(EXIT_FAILURE);
      }
      
      std::unique_lock<std::mutex> lk(m_mutex);
      while (m_cur != end)
        m_cv.wait(lk);
        
      if (m_signal_read_next_chunk) {
        fprintf(stderr, "Error: m_signal_read_next_chunk in the wrong state.\n");
        std::exit(EXIT_FAILURE);
      }

      std::swap(m_chunk, m_passive_chunk);
      m_signal_read_next_chunk = true;

      lk.unlock();
      m_cv.notify_all();
    }
    
    ~background_chunk_reader() {
      std::unique_lock<std::mutex> lk(m_mutex);
      m_signal_stop = true;
      lk.unlock();
      m_cv.notify_all();

      // Wait until the thread notices the flag and exits. Possibly the thread
      // is already not running, but in this case this call will do nothing.
      m_thread->join();

      std::fclose(m_file);

      // Clean up.  
      delete m_thread;
      free(m_chunk);
      free(m_passive_chunk);
    }

    inline std::uint64_t get_chunk_size() const {
      return m_chunk_length;
    }
};

}  // namespace psascan_private

#endif  // __SRC_PSASCAN_SRC_IO_BACKGROUND_CHUNK_READER_HPP_INCLUDED
