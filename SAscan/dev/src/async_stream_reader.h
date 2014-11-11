#ifndef __ASYNC_STREAM_READER_H_INCLUDED
#define __ASYNC_STREAM_READER_H_INCLUDED

#include <cstdio>
#include <thread>
#include <future>
#include <mutex>
#include <condition_variable>
#include <algorithm>

#include "utils.h"

template<typename value_type>
struct async_stream_reader {
  template<typename T>
  static void io_thread_code(async_stream_reader<T> *reader) {
    while (true) {
      // Wait until the passive buffer is available.
      std::unique_lock<std::mutex> lk(reader->m_mutex);
      while (!(reader->m_avail) && !(reader->m_finished))
        reader->m_cv.wait(lk);

      if (!(reader->m_avail) && (reader->m_finished)) {
        // We're done, terminate the thread.
        lk.unlock();
        return;
      }
      lk.unlock();

      // Safely read the data to disk.
      reader->m_passive_buf_filled = fread(reader->m_passive_buf,
          sizeof(T), reader->m_buf_size, reader->m_file);

      // Let the caller know what the I/O thread finished reading.
      std::unique_lock<std::mutex> lk2(reader->m_mutex);
      reader->m_avail = false;
      lk2.unlock();
      reader->m_cv.notify_one();
    }
  }

  async_stream_reader(std::string filename, long bufsize = (4 << 20)) {
    m_file = utils::open_file(filename.c_str(), "r");

    // Initialize buffers.
    long elems = std::max(2UL, (bufsize + sizeof(value_type) - 1) / sizeof(value_type));
    m_buf_size = elems / 2;

    m_active_buf_filled = 0L;
    m_passive_buf_filled = 0L;
    m_active_buf_pos = 0L;
    m_active_buf = (value_type *)malloc(m_buf_size * sizeof(value_type));
    m_passive_buf = (value_type *)malloc(m_buf_size * sizeof(value_type));

    m_finished = false;
    
    // Start the I/O thread and immediatelly start reading.
    m_avail = true;
    m_thread = new std::thread(io_thread_code<value_type>, this);
  }
  
  ~async_stream_reader() {
    // Let the I/O thread know that we're done.
    std::unique_lock<std::mutex> lk(m_mutex);
    m_finished = true;
    lk.unlock();
    m_cv.notify_one();

    // Wait for the thread to actually finish.
    m_thread->join();
    
    // Clean up.
    delete m_thread;
    free(m_active_buf);
    free(m_passive_buf);
    std::fclose(m_file);
  }

  // This function checks if the reading thread has already
  // prefetched the next buffer (the request should have been
  // done before), and waits if the prefetching was not
  // completed yet.
  void receive_new_buffer() {
    // Wait until the I/O thread finishes reading the previous
    // buffer. Most of the time this step is instantaneous.
    std::unique_lock<std::mutex> lk(m_mutex);
    while (m_avail == true)
      m_cv.wait(lk);

    // Set the new active buffer.
    std::swap(m_active_buf, m_passive_buf);
    m_active_buf_filled = m_passive_buf_filled;
    m_active_buf_pos = 0L;

    // Let the I/O thread know that it can now prefetch
    // another buffer.
    m_avail = true;
    lk.unlock();
    m_cv.notify_one();
  }

  inline value_type read() {
    if (m_active_buf_pos == m_active_buf_filled) {
      // The active buffer run out of data.
      // At this point we need to swap it with the passive
      // buffer. The request to read that passive buffer should
      // have been scheduled long time ago, so hopefully the
      // buffer is now available. We check for that, but we
      // also might wait a little, if the reading has not yet
      // been finished. At this point we also already schedule
      // the next read.
      receive_new_buffer();
    }

    return m_active_buf[m_active_buf_pos++];
  }

private:
  value_type *m_active_buf;
  value_type *m_passive_buf;

  long m_buf_size;
  long m_active_buf_pos;
  long m_active_buf_filled;
  long m_passive_buf_filled;

  // Used for synchronization with the I/O thread.
  std::mutex m_mutex;
  std::condition_variable m_cv;
  bool m_avail;
  bool m_finished;

  std::FILE *m_file;
  std::thread *m_thread;
};

#endif  // __ASYNC_STREAM_READER_H_INCLUDED
