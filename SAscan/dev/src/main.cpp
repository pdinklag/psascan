#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <getopt.h>
#include <unistd.h>
#include <omp.h>

#include "sascan.h"


char *program_name;
bool verbose;

void usage(int status) {
  printf(
"Usage: %s [OPTION]... FILE\n"
"Construct the suffix array for text stored in FILE.\n"
"\n"
"Mandatory arguments to long options are mandatory for short options too.\n"
"  -g, --gap=GAPFILE       specify gap array file (default: along with output)\n"
"  -h, --help              display this help and exit\n"
"  -m, --mem=LIMIT         limit RAM usage to LIMIT MiB (default: 3072)\n"
"  -o, --output=OUTFILE    specify output file (default: FILE.sa5)\n"
"  -v, --verbose           print detailed information during internal sufsort\n",
    program_name);

  std::exit(status);
}

int main(int argc, char **argv) {
  srand(time(0) + getpid());
  program_name = argv[0];
  verbose = false;

  static struct option long_options[] = {
    {"help",    no_argument,       NULL, 'h'},
    {"verbose", no_argument,       NULL, 'v'},
    {"mem",     required_argument, NULL, 'm'},
    {"output",  required_argument, NULL, 'o'},
    {"gap",     required_argument, NULL, 'g'},
    {NULL, 0, NULL, 0}
  };

  long ram_use = 3072L << 20;
  std::string out_fname("");
  std::string gap_fname("");

  // Parse command-line options.
  int c;
  while ((c = getopt_long(argc, argv, "hvm:o:g:", long_options, NULL)) != -1) {
    switch(c) {
      case 'm':
        ram_use = std::atol(optarg) << 20;
        if (ram_use <= 0L) {
          fprintf(stderr, "Error: invalid RAM limit (%ld)\n\n", ram_use);
          usage(EXIT_FAILURE);
        }
        break;
      case 'o':
        out_fname = std::string(optarg);
        break;
      case 'g':
        gap_fname = std::string(optarg);
        break;
      case 'v':
        verbose = true;
        break;
      case 'h':
        usage(EXIT_FAILURE);
      default:
        usage(EXIT_FAILURE);
    }
  }

  if (optind >= argc) {
    fprintf(stderr, "Error: FILE not provided\n\n");
    usage(EXIT_FAILURE);
  }

  // Parse the text filename.
  std::string text_fname = std::string(argv[optind++]);
  if (optind < argc) {
    fprintf(stderr, "Warning: multiple input files provided. "
    "Only the first will be processed.\n");
  }

  // Set default output filename (if not provided).
  if (out_fname.empty())
    out_fname = text_fname + ".sa5";

  // Set default gap filename (if not provided).
  if (gap_fname.empty())
    gap_fname = out_fname;

  // Check if input exists.
  if (!utils::file_exists(text_fname)) {
    fprintf(stderr, "Error: input file (%s) does not exist\n\n",
        text_fname.c_str());
    usage(EXIT_FAILURE);
  }

  if (utils::file_exists(out_fname)) {
    // Output file exists, should we proceed?
    char *line = NULL;
    size_t buflen = 0;
    long len = 0L;

    do {
      printf("Output file (%s) exists. Overwrite? [y/n]: ",
          out_fname.c_str());
      if ((len = getline(&line, &buflen, stdin)) == -1) {
        fprintf(stderr, "\nError: failed to read answer\n\n");
        usage(EXIT_FAILURE);
      }
    } while (len != 2 || (line[0] != 'y' && line[0] != 'n'));

    if (line[0] == 'n') {
      free(line);
      std::exit(EXIT_FAILURE);
    }
    free(line);
  }

  long max_threads = (long)omp_get_max_threads();
  SAscan(text_fname, out_fname, gap_fname, ram_use, max_threads);
}
