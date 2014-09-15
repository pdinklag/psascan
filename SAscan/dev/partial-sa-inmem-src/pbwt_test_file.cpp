#include <cstdio>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <thread>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "divsufsort.h"
#include "divsufsort64.h"
#include "bitvector.h"
#include "multifile_bitvector.h"
#include "utils.h"
#include "io_streamer.h"
#include "inmem_sascan.h"
#include "divsufsort_template.h"


void compute_gt_begin_reversed(unsigned char *text, long text_length, bitvector *gt_begin_reversed) {
  long i = 1, el = 0;
  while (i < text_length) {
    while (i + el < text_length && text[i + el] == text[el]) ++el;
    if (i + el < text_length && text[i + el] > text[el])
      gt_begin_reversed->set(text_length - i);

    el = 0;
    ++i;
  }
}

template<typename saidx_t, unsigned pagesize_log>
void test(std::string supertext_filename, long text_length, long max_threads) {
  long double start;

  fprintf(stderr, "Input filename: %s\n", supertext_filename.c_str());
  fprintf(stderr, "Reading text: ");
  long supertext_length;
  unsigned char *supertext;
  utils::read_objects_from_file(supertext, supertext_length, supertext_filename);
  fprintf(stderr, "DONE\n");


  std::string sa_filename = supertext_filename + ".sa" + utils::intToStr(sizeof(long));
  if (!utils::file_exists(sa_filename)) {
    fprintf(stderr, "Running divsufsort\n");
    start = utils::wclock();
    long *correct_sa = new long[supertext_length];
    run_divsufsort(supertext, correct_sa, supertext_length);
    utils::write_objects_to_file(correct_sa, supertext_length, sa_filename);
    delete[] correct_sa;
    fprintf(stderr, "Total time: %.2Lf\n", utils::wclock() - start);
  }


  text_length = std::min(text_length, supertext_length);
  long text_beg = utils::random_long(0L, supertext_length - text_length);
  long text_end = text_beg + text_length;


  // Compute tail_gt_begin_reversed.
  unsigned char *tail = supertext + text_end;
  long tail_length = supertext_length - text_end;
  bitvector *tail_gt_begin_reversed_bv = new bitvector(tail_length, max_threads);
  compute_gt_begin_reversed(tail, tail_length, tail_gt_begin_reversed_bv);

  // Store tail_gt_begin_reversed on disk as a multifile bitvector.
  multifile *tail_gt_begin_reversed_multifile = new multifile();
  long ptr = 0;
  while (ptr < tail_length) {
    long left = tail_length - ptr;
    long chunk = utils::random_long(1L, left);
   
    // Store bits [ptr..ptr+chunk) from tail_gt_begin_reversed_bv into one file.
    std::string chunk_filename = "gt_begin_reversed_bv" + utils::random_string_hash();
    bit_stream_writer *writer = new bit_stream_writer(chunk_filename);
    for (long j = ptr; j < ptr + chunk; ++j)
      writer->write(tail_gt_begin_reversed_bv->get(j));
    delete writer;

    // Add this file to tail_gt_begin_reversed_multifile.
    tail_gt_begin_reversed_multifile->add_file(ptr, ptr + chunk, chunk_filename);
    
    ptr += chunk;
  }
  delete tail_gt_begin_reversed_bv;


  unsigned char *text = (unsigned char *)malloc(text_length);
  std::copy(supertext + text_beg, supertext + text_end, text);
  delete[] supertext;



  // Run the tested algorithm.
  fprintf(stderr, "Running inmem sascan\n\n");
  start = utils::wclock();
  unsigned char *bwtsa = (unsigned char *)malloc(text_length * (1 + sizeof(saidx_t)));
  saidx_t *computed_sa = (saidx_t *)bwtsa;
  unsigned char *computed_bwt = (unsigned char *)(computed_sa + text_length);
  long computed_i0;
  inmem_sascan<saidx_t, pagesize_log>(text, text_length, bwtsa, max_threads, true,
      false, NULL, -1, text_beg, text_end, supertext_length, supertext_filename,
      tail_gt_begin_reversed_multifile, &computed_i0);
  long double total_time = utils::wclock() - start;
  fprintf(stderr, "\nTotal time:\n");
  fprintf(stderr, "\tabsolute: %.2Lf\n", total_time);
  fprintf(stderr, "\trelative: %.4Lfs/MiB\n", total_time / ((long double)text_length / (1 << 20)));
  fprintf(stderr, "Speed: %.2LfMiB/s\n", ((long double)text_length / (1 << 20)) / total_time);

  ptr = 0;
  fprintf(stderr, "\nComparing:\n");
  stream_reader<long> *sa_reader = new stream_reader<long>(sa_filename);
  bool eq = true;
  long compared = 0;
  long correct_i0 = -1;
  for (long i = 0, dbg = 0; i < supertext_length; ++i) {
    ++dbg;
    ++compared;
    if (dbg == 10000000) {
      fprintf(stderr, "progress: %.3Lf%%\r", (100.L * i) / supertext_length);
      dbg = 0;
    }

    long next_correct_sa = sa_reader->read();
    if (text_beg <= next_correct_sa && next_correct_sa < text_end) {
      next_correct_sa -= text_beg;
      unsigned char next_correct_bwt = ((next_correct_sa == 0) ? 0 : text[next_correct_sa - 1]);
      if (next_correct_sa == 0) correct_i0 = ptr;
      if (next_correct_bwt != computed_bwt[ptr++]) {
        eq = false;
        break;
      }
    }
  }
  if (correct_i0 != computed_i0) eq = false;
  fprintf(stderr, "Compared %ld values", compared);
  fprintf(stderr, "\nResult: %s\n", eq ? "OK" : "FAIL");

  free(bwtsa);
  free(text);
  delete sa_reader;
  delete tail_gt_begin_reversed_multifile; // also deletes files
}


int main(int argc, char **argv) {
  std::srand(std::time(0) + getpid());
  if (argc == 1) {
    fprintf(stderr, "%s <file> <min-text-length-in-MiB>\n",
        argv[0]);
    std::exit(EXIT_FAILURE);
  }

  long min_text_length = atol(argv[2]) << 20;
  test<uint40/*int*/, 12>(argv[1], min_text_length, 24);
}

