// Common routines. (c) Dominik Kempa 2013.
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

std::string get_absolute_dir(std::string fname) {
  char filepath[1 << 18];
  if (!realpath(fname.c_str(), filepath)) {
    fprintf(stderr, "Error: Cannot obtain realpath of %s\n", fname.c_str());
    std::exit(EXIT_FAILURE);
  }
  std::string dirpath = filepath;

  return dirpath.substr(0, dirpath.find_last_of("/") + 1);
}

std::string get_absolute_path(std::string fname) {
  char path[1 << 18];
  if (!realpath(fname.c_str(), path)) {
    fprintf(stderr, "Error: Cannot obtain realpath of %s\n", fname.c_str());
    std::exit(EXIT_FAILURE);
  }
  
  return std::string(path);
}

/****************************** MEASURING TIME ********************************/

long double wclock() {
  timeval tim;
  gettimeofday(&tim, NULL);

  return tim.tv_sec + (tim.tv_usec / 1000000.0L);
}

long double elapsed(clock_t &ts) {
  return (long double)(clock() - ts) / CLOCKS_PER_SEC;
}

/**************************** FILE MANIPULATION *******************************/

// Basic routines.
FILE *open_file(std::string fname, std::string mode) {
  FILE *f = fopen(fname.c_str(), mode.c_str());
  if (!f) {
    perror(fname.c_str());
    std::exit(EXIT_FAILURE);
  }

  return f;
}

long file_size(std::string fname) {
  FILE *f = open_file(fname, "rt");
  fseek(f, 0, SEEK_END);
  long size = (long)ftell(f);
  fclose(f);

  return size;
}

bool file_exists(std::string fname) {
  FILE *f = fopen(fname.c_str(), "r");
  bool ret = (f != NULL);
  if (f) fclose(f);

  return ret;
}

void file_delete(std::string fname) {
  execute("rm " + fname);
  if (file_exists(fname)) {
    fprintf(stderr, "Error: Cannot delete %s.\n", fname.c_str());
    std::exit(EXIT_FAILURE);
  }
}

// Writing sequences.
void write_text_to_file(unsigned char *text, long length, std::string fname) {
  FILE *f = open_file(fname, "w");
  long fwrite_ret = fwrite(text, sizeof(unsigned char), length, f);
  if (fwrite_ret != length) {
    fprintf(stderr, "Error: fwrite in line %s of %s returned %ld\n",
        STR(__LINE__), STR(__FILE__), fwrite_ret);
    std::exit(EXIT_FAILURE);
  }
  fclose(f);
}

void write_ints_to_file(int *tab, long length, std::string fname) {
  FILE *f = open_file(fname, "w");
  long fwrite_ret = fwrite(tab, sizeof(int), length, f);
  if (fwrite_ret != length) {
    fprintf(stderr, "Error: fwrite in line %s of %s returned %ld\n",
        STR(__LINE__), STR(__FILE__), fwrite_ret);
    std::exit(EXIT_FAILURE);
  }
  fclose(f);
}

void add_ints_to_file(int *tab, long length, FILE *f) {
  long fwrite_ret = fwrite(tab, sizeof(int), length, f);
  if (fwrite_ret != length) {
    fprintf(stderr, "Error: fwrite in line %s of %s returned %ld\n",
        STR(__LINE__), STR(__FILE__), fwrite_ret);
    std::exit(EXIT_FAILURE);
  }
}

// Reading sequences.
void read_text_from_file(unsigned char* &text, long length, std::string fname) {
  FILE *f = open_file(fname, "r");
  text = new unsigned char[length + 10];
  if (!text) {
    fprintf(stderr, "Error: cannot allocate text.\n");
    std::exit(EXIT_FAILURE);
  }
  long fread_ret = fread(text, sizeof(unsigned char), length, f);
  if (fread_ret != length) {
    fprintf(stderr, "Error: fread in line %s of %s returned %ld\n",
        STR(__LINE__), STR(__FILE__), fread_ret);
    std::exit(EXIT_FAILURE);
  }
  fclose(f);
}

void read_ints_from_file(int* &tab, long length, std::string fname) {
  FILE *f = open_file(fname, "r");
  tab = new int[length + 5];
  if (!tab) {
    fprintf(stderr, "Error: cannot allocate tab.\n");
    std::exit(EXIT_FAILURE);
  }
  long fread_ret = fread(tab, sizeof(int), length, f);
  if (fread_ret != length) {
    fprintf(stderr, "Error: fread in line %s of %s returned %ld\n",
        STR(__LINE__), STR(__FILE__), fread_ret);
    std::exit(EXIT_FAILURE);
  }
  fclose(f);
}

void read_file(unsigned char* &text, long &length, std::string fname) {
  FILE *f = open_file(fname, "r");
  fseek(f, 0, SEEK_END);
  length = (long)ftell(f); // No length was given.
  rewind(f);
  text = new unsigned char[length + 10];
  if (!text) {
    fprintf(stderr, "Error: cannot allocate text.\n");
    std::exit(EXIT_FAILURE);
  }
  long fread_ret = (long)fread(text, sizeof(unsigned char), length, f);
  if (fread_ret != length) {
    fprintf(stderr, "Error: fread in line %s of %s returned %ld\n",
        STR(__LINE__), STR(__FILE__), fread_ret);
    std::exit(EXIT_FAILURE);
  }
  fclose(f);
}

void read_block(std::string fname, long beg, long length, unsigned char *b) {
  std::FILE *f = open_file(fname.c_str(), "r");
  std::fseek(f, beg, SEEK_SET);
  read_objects_from_file<unsigned char>(b, length, f);
  std::fclose(f);
}

// Reading single objects from fie.
long double read_ld_from_file(std::string fname) {
  std::fstream f(fname.c_str(), std::ios_base::in);
  if (f.fail()) {
    fprintf(stderr, "Error: cannot open file %s.\n", fname.c_str());
    std::exit(EXIT_FAILURE);
  }
  long double ret;
  f >> ret;
  f.close();

  return ret;
}

int read_int_from_file(std::string fname) {
  std::fstream f(fname.c_str(), std::ios_base::in);
  if (f.fail()) {
    fprintf(stderr, "Error: cannot open file %s.\n", fname.c_str());
    std::exit(EXIT_FAILURE);
  }
  int ret;
  f >> ret;
  f.close();

  return ret;
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

double random_double(double p, double r) {
  double f = (double)random_int(0, 1000000000) / 1000000000.0;
  return p + f * (r - p);
}

/********************************* MATH ***************************************/

long log2ceil(long x) {
  long pow2 = 1, w = 0;
  while (pow2 < x) { pow2 <<= 1; ++w; }
  return w;
}

/********************************* MISC ***************************************/

void find_stxxl_config() {
  if (file_exists("./.stxxl")) {
    fprintf(stderr, "STXXL config file detected.\n");
    return;
  } else if (file_exists(std::string(getenv("HOME")) + "/.stxxl")) {
    fprintf(stderr, "Cannot find STXXL config file. Using $HOME/.stxxl\n");
    execute("cp " + std::string(getenv("HOME")) + "/.stxxl ./");
    return;
  } else {  
    fprintf(stderr, "Error: failed to find/copy STXXL config file!\n");
    std::exit(EXIT_FAILURE);
  }
}

} // namespace utils