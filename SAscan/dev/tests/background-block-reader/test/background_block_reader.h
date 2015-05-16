#ifndef __BACKGROUND_BLOCK_READER_H_INCLUDED
#define __BACKGROUND_BLOCK_READER_H_INCLUDED

#include <cstdio>
#include <cstdlib>

#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>

#include <unistd.h>

#include "utils.h"


struct background_block_reader {
  public:
    unsigned char *m_data;
    long m_start;
    long m_size;

  private:
    long m_chunk_size;

    // These variables are protected by m_mutex.
    long m_fetched;
    bool m_signal_stop;
    bool m_joined;

    std::mutex m_mutex;

    // This condition variable is used by the I/O thread to notify
    // the waiting threads when the next chunk is read.
    std::condition_variable m_cv;

    std::thread *m_thread;
    std::FILE *m_file;

  private:
    static void io_thread_main(background_block_reader &reader) {
      while (true) {
        usleep(utils::random_int(0, 50));  /////

        std::unique_lock<std::mutex> lk(reader.m_mutex);
        long fetched = reader.m_fetched;
        bool signal_stop = reader.m_signal_stop;
        lk.unlock();


        usleep(utils::random_int(0, 50));  /////

        if (fetched == reader.m_size || signal_stop) break;

        usleep(utils::random_int(0, 50));  //////

        long toread = std::min(reader.m_size - fetched, reader.m_chunk_size);
        unsigned char *dest = reader.m_data + fetched;
        long fread_ret = std::fread(dest, sizeof(unsigned char), toread, reader.m_file);
        if (fread_ret != toread) {
          fprintf(stderr, "\nError: fread in backgroud_block_reader failed.\n");
          std::exit(EXIT_FAILURE);
        }

        usleep(utils::random_int(0, 50)); //////

        lk.lock();

        usleep(utils::random_int(0, 50));  /////


        reader.m_fetched += toread;
        lk.unlock();

        usleep(utils::random_int(0, 50));   //////


        reader.m_cv.notify_all();

        usleep(utils::random_int(0, 50));  //////

      }

      usleep(utils::random_int(0, 50)); //////

      
      // Close the file and exit.
      std::fclose(reader.m_file);
    }

  public:
    background_block_reader(std::string filename, long start, long size, long chunk_size = (1L << 20)) {
      m_chunk_size = chunk_size;
      m_start = start;
      m_size = size;
         
      // Initialize file and buffer.
      m_data = (unsigned char *)malloc(m_size);
      m_file = utils::open_file(filename, "r");
      std::fseek(m_file, m_start, SEEK_SET);
      m_fetched = 0;

      // Start the I/O thread.
      m_signal_stop = false;
      m_joined = false;
      m_thread = new std::thread(io_thread_main, std::ref(*this));
    }

    ~background_block_reader() {
      if (!m_joined) {
        fprintf(stderr, "\nError: the I/O thread is still not joined when "
          "destroying an object of backgroud_block_reader.\n");
        std::exit(EXIT_FAILURE);
      }
      
      // Note: m_file is already closed.
      delete m_thread;
      free(m_data);
    }

    inline void stop() {
      // Set the flag for the thread to stop.
      std::unique_lock<std::mutex> lk(m_mutex);
      m_signal_stop = true;
      lk.unlock();

      // Wait until the thread notices the flag and exits. Possibly the thread
      // is already not running, but in this case this call will do nothing.
      m_thread->join();
      
      // To detect (in the destructor) if stop() was called.
      lk.lock();
      m_joined = true;
      lk.unlock();
    }

    inline void wait(long target_fetched) {
      std::unique_lock<std::mutex> lk(m_mutex);
      while (m_fetched < target_fetched)
        m_cv.wait(lk);
      lk.unlock();
    }
};

#endif  // __BACKGROUND_BLOCK_READER_H_INCLUDED