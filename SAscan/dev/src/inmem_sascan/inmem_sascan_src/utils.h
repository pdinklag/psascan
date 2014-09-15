#ifndef __INMEM_SASCAN_UTILS_H_INCLUDED
#define __INMEM_SASCAN_UTILS_H_INCLUDED

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sstream>

namespace inmem_sascan_private {

namespace utils {

#define STRX(x) #x
#define STR(x) STRX(x)

/****************************** MEASURING TIME ********************************/
long double wclock();

/**************************** FILE MANIPULATION *******************************/
// Basic routines.
FILE *open_file(std::string fname, std::string mode);
long file_size(std::string fname);
bool file_exists(std::string fname);
void file_delete(std::string fname);
std::string absolute_path(std::string fname);

template<typename T>
void write_objects_to_file(T *tab, long length, std::string fname) {
  std::FILE *f = open_file(fname, "w");
  size_t fwrite_ret = std::fwrite(tab, sizeof(T), length, f);
  if ((long)fwrite_ret != length) {
    fprintf(stderr, "Error: fwrite in line %s of %s returned %ld\n",
        STR(__LINE__), STR(__FILE__), fwrite_ret);
    std::exit(EXIT_FAILURE);
  }

  std::fclose(f);
}

template<typename T>
void add_objects_to_file(T *tab, long length, std::FILE *f) {
  size_t fwrite_ret = std::fwrite(tab, sizeof(T), length, f);
  if ((long)fwrite_ret != length) {
    fprintf(stderr, "Error: fwrite in line %s of %s returned %lu\n",
        STR(__LINE__), STR(__FILE__), fwrite_ret);
    std::exit(EXIT_FAILURE);
  }
}

template<typename T>
void add_objects_to_file(T *tab, long length, std::string fname) {
  std::FILE *f = utils::open_file(fname.c_str(), "a");
  add_objects_to_file<T>(tab, length, f);
  std::fclose(f);
}

void read_block(std::string fname, long beg, long length, unsigned char *b);
void read_block(std::FILE *f, long beg, long length, unsigned char *b);

template<typename T>
void read_objects_from_file(T* tab, long length, std::FILE *f) {
  size_t fread_ret = fread(tab, sizeof(T), length, f);
  if ((long)fread_ret != length) {
    fprintf(stderr, "Error: fread in line %s of %s returned %ld\n",
        STR(__LINE__), STR(__FILE__), fread_ret);
    std::exit(EXIT_FAILURE);
  }
}

template<typename T>
void read_objects_from_file(T* &tab, long &length, std::string fname) {
  std::FILE *f = open_file(fname, "r");  
  std::fseek(f, 0L, SEEK_END);
  length = (long)(std::ftell(f) / sizeof(T));
  std::rewind(f);
  
  tab = new T[length];
  read_objects_from_file(tab, length, f);
  
  std::fclose(f);
}

template<typename T>
void read_n_objects_from_file(T* tab, long length, std::string fname) {
  std::FILE *f = open_file(fname, "r");
  read_objects_from_file(tab, length, f);
  std::fclose(f);
}

/******************************* RANDOMNESS ***********************************/
int random_int(int p, int r);
long random_long(long p, long r);
void fill_random_string(unsigned char* &s, long length, int sigma);
void fill_random_letters(unsigned char* &s, long n, int sigma);
std::string random_string_hash();

/********************************* MATH ***************************************/
long log2ceil(long x);
long log2floor(long x);

/********************************* MISC ***************************************/
template<typename int_type>
std::string intToStr(int_type x) {
  std::stringstream ss;
  ss << x;
  return ss.str();
}

template<class T, class U>
struct is_same_type {
  enum { value = 0 };
};

template<class T>
struct is_same_type<T, T> {
  enum { value = 1 };
};

} // namespace utils

}  // namespace inmem_sascan

#endif // __UTILS_H_INCLUDED