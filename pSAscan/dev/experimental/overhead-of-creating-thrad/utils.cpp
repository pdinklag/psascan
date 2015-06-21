#include <cstdio>
#include <cstdlib>

#include <stdint.h>
#include <unistd.h>
#include <sys/time.h>

#include <string>
#include <fstream>
#include <algorithm>

#include "utils.h"

namespace utils {

/******************************* SYSTEM CALLS *********************************/
void execute(std::string cmd) {
  int system_ret = system(cmd.c_str());
  if (system_ret) {
    fprintf(stderr, "Error: executing command [%s] returned %d.\n",
        cmd.c_str(), system_ret);
    std::exit(EXIT_FAILURE);
  }
}

/****************************** MEASURING TIME ********************************/
long double wclock() {
  timeval tim;
  gettimeofday(&tim, NULL);

  return tim.tv_sec + (tim.tv_usec / 1000000.0L);
}


/**************************** FILE MANIPULATION *******************************/
std::FILE *open_file(std::string fname, std::string mode) {
  std::FILE *f = std::fopen(fname.c_str(), mode.c_str());
  if (f == NULL) {
    std::perror(fname.c_str());
    std::exit(EXIT_FAILURE);
  }

  return f;
}

long file_size(std::string fname) {
  std::FILE *f = open_file(fname, "rt");
  std::fseek(f, 0L, SEEK_END);
  long size = std::ftell(f);
  std::fclose(f);

  return size;
}

bool file_exists(std::string fname) {
  std::FILE *f = std::fopen(fname.c_str(), "r");
  bool ret = (f != NULL);
  if (f != NULL)
    std::fclose(f);

  return ret;
}

void file_delete(std::string fname) {
  execute("rm " + fname);
  if (file_exists(fname)) {
    fprintf(stderr, "Error: Cannot delete %s.\n", fname.c_str());
    std::exit(EXIT_FAILURE);
  }
}

void read_block(std::FILE *f, long beg, long length, unsigned char *b) {
  std::fseek(f, beg, SEEK_SET);
  read_objects_from_file<unsigned char>(b, length, f);
}

void read_block(std::string fname, long beg, long length, unsigned char *b) {
  std::FILE *f = open_file(fname.c_str(), "r");
  read_block(f, beg, length, b);
  std::fclose(f);
}

/******************************* RANDOMNESS ***********************************/
int random_int(int p, int r) {
  return p + rand() % (r - p + 1);
}

long random_long(long p, long r) {
  long x = random_int(0, 1000000000);
  long y = random_int(0, 1000000000);
  long z = x * 1000000000L + y;
  return p + z % (r - p + 1);
}

void fill_random_string(unsigned char* &s, long length, int sigma) {
  for (long i = 0; i < length; ++i)
    s[i] = random_int(0, sigma - 1);
}

void fill_random_letters(unsigned char* &s, long n, int sigma) {
  fill_random_string(s, n, sigma);
  for (long i = 0; i < n; ++i) s[i] += 'a';
}

std::string random_string_hash() {
  uint64_t hash = (uint64_t)rand() * RAND_MAX + rand();
  std::stringstream ss;
  ss << hash;
  return ss.str();
}

/********************************* MATH ***************************************/
long log2ceil(long x) {
  long pow2 = 1, w = 0;
  while (pow2 < x) { pow2 <<= 1; ++w; }
  return w;
}

} // namespace utils