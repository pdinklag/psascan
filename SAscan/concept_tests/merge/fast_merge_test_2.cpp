// For a given string, we generate the random partition into blocks. Then for
// each block we compute:
//   * the sparse suffix array (containing only suffixes starting inside)
//   * the gap array, as computed in the FGM
// Then we perform the fast merging (as suggested by Juha) and compare the
// result to the complete suffix array of the text.

#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <vector>
#include <string>

#include "sais.hxx"
#include "utils.h"


// Represents the output of a single FGM phase: gap and sparse SA.
struct fgm_phase_output {
  fgm_phase_output() :gap(NULL), sparse_sa(NULL) {}
  
  // The block is text[beg..end). SA is the suf array of text[0..length).
  void construct(int length, int *SA, int beg, int end) {
    block_length = end - beg;
    gap = new int[block_length + 1];
    sparse_sa = new int[block_length];
    
    std::fill(gap, gap + block_length + 1, 0);
    for (int i = 0, j = 0; i < length; ++i) {
      if (SA[i] < beg) continue;
      else if (SA[i] < end) sparse_sa[j++] = SA[i] - beg;
      else ++gap[j];
    }
  }

  ~fgm_phase_output() {
    if (gap) delete[] gap;
    if (sparse_sa) delete[] sparse_sa;
  }

  int block_length, text_length;
  int *gap, *sparse_sa;
};

void test(unsigned char *text, int length) {  
  int m = utils::random_int(1, length);
  int n_block = (length + m - 1) / m;

  // Compute the suffix array of the text.
  int *SA = new int[length];
  saisxx(text, SA, length);

  // Compute the FGM output.
  fgm_phase_output *output = new fgm_phase_output[n_block];
  for (int i = 0, beg = 0; i < n_block; ++i, beg += m) {
    int end = std::min(beg + m, length);
    output[i].construct(length, SA, beg, end);
  }

  // Testing the pseudo-code described in the ICABD paper:
  int *computed_sa = new int[length]; // output of merging
  int *i_k = new int[n_block]; // as in the paper
  for (int i = 0; i < n_block; ++i) i_k[i] = 0;
  for (int i = 0; i < length; ++i) {
    int k = 0;
    while (output[k].gap[i_k[k]] > 0) {
      --output[k].gap[i_k[k]];
      ++k;
    }
    computed_sa[i] = output[k].sparse_sa[i_k[k]] + k * m;
    ++i_k[k];
  }
  delete[] i_k;
  delete[] output;

  // Compare the computed suffix array to the correct suffix array.
  if (!std::equal(SA, SA + length, computed_sa)) {
    fprintf(stderr, "Error: SA and computed_sa are not equal!\n");
    if (length < 100) {
      fprintf(stderr, "  text = %s\n", text);
      fprintf(stderr, "  SA = ");
      for (int j = 0; j < length; ++j) fprintf(stderr, "%d ", SA[j]);
      fprintf(stderr, "\n");
      fprintf(stderr, "  computed sa = ");
      for (int j = 0; j < length; ++j) fprintf(stderr, "%d ", computed_sa[j]);
      fprintf(stderr, "\n");
    }
    std::exit(EXIT_FAILURE);
  }
  delete[] SA;
  delete[] computed_sa;
}

// Test many string chosen according to given paranters.
void test_random(int testcases, int max_length, int max_sigma) {
  fprintf(stderr,"TEST, testcases = %d, max_n = %d, max_sigma = %d\n",
      testcases, max_length, max_sigma);
  unsigned char *text = new unsigned char[max_length + 1];

  for (int tc = 0, dbg = 0; tc < testcases; ++tc, ++dbg) {
    // Print progress information.
    if (dbg == 100) {
      fprintf(stderr,"%d (%.2Lf%%)\r", tc, (tc * 100.L) / testcases);
      dbg = 0;
    }

    // Generate string.
    int length = utils::random_int(2, max_length);
    int sigma = utils::random_int(2, max_sigma);
    if (max_sigma <= 26) utils::fill_random_letters(text, length, sigma);
    else utils::fill_random_string(text, length, sigma);
    text[length] = 0;

    // Run the test on generated string.
    test(text, length);
  }

  // Clean up.
  delete[] text;
}

int main(int, char **) {
  srand(time(0) + getpid());

  // Run tests.
  fprintf(stderr, "Testing fast merging in FGM.\n");
  test_random(500000, 10,       5);
  test_random(500000, 10,     256);
  test_random(100000, 100,      5);
  test_random(100000, 100,    256);
  test_random(50000,  1000,     5);
  test_random(50000,  1000,   256);
  test_random(10000,  10000,    5);
  test_random(10000,  10000,  256);
  test_random(1000,   100000,   5);
  test_random(1000,   100000, 256);
  fprintf(stderr,"All tests passed.\n");

  return 0;
}
