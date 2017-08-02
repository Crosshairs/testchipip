// See LICENSE for license details.

#include "verilated.h"
#if VM_TRACE
#include "verilated_vcd_c.h"
#endif
//TODO: someone who likes macros fix this
#define __tether_header(x) #x
#define _tether_header(x) __tether_header(fesvr/x.h)
#define tether_header(x) _tether_header(x)
#include tether_header(TETHER_NAME)
#include <iostream>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>

//COLIN FIGURE OUT HOW TO SWITCH BETWEEN TSI AND DTM
extern TETHER_TYPE* TETHER_NAME;
static uint64_t trace_count = 0;
bool verbose;
bool done_reset;

void handle_sigterm(int sig)
{
  TETHER_NAME->stop();
}

double sc_time_stamp()
{
  return trace_count;
}

extern "C" int vpi_get_vlog_info(void* arg)
{
  return 0;
}

static void usage(const char * program_name) {
  printf("Usage: %s [OPTION]... BINARY [BINARY ARGS]\n", program_name);
  fputs("\
Run a BINARY on the Rocket Chip emulator.\n\
\n\
Mandatory arguments to long options are mandatory for short options too.\n\
  -c, --cycle-count          print the cycle count before exiting\n\
       +cycle-count\n\
  -h, --help                 display this help and exit\n\
  -m, --max-cycles=CYCLES    kill the emulation after CYCLES\n\
       +max-cycles=CYCLES\n\
  -s, --seed=SEED            use random number seed SEED\n\
  -V, --verbose              enable all Chisel printfs\n\
       +verbose\n\
", stdout);
#if VM_TRACE
  fputs("\
  -v, --vcd=FILE,            write vcd trace to FILE (or '-' for stdout)\n\
  -x, --dump-start=CYCLE     start VCD tracing at CYCLE\n\
      +dump-start\n\
", stdout);
#else
  fputs("\
VCD options (e.g., -v, +dump-start) require a debug-enabled emulator.\n\
Try `make debug`.\n\
", stdout);
#endif
}

int main(int argc, char** argv)
{
  unsigned random_seed = (unsigned)time(NULL) ^ (unsigned)getpid();
  uint64_t max_cycles = -1;
  int ret = 0;
  bool print_cycles = false;
#if VM_TRACE
  FILE * vcdfile = NULL;
  uint64_t start = 0;
#endif

  std::vector<std::string> to_tether;
  while (1) {
    static struct option long_options[] = {
      {"cycle-count", no_argument,       0, 'c' },
      {"help",        no_argument,       0, 'h' },
      {"max-cycles",  required_argument, 0, 'm' },
      {"seed",        required_argument, 0, 's' },
      {"verbose",     no_argument,       0, 'V' },
#if VM_TRACE
      {"vcd",         required_argument, 0, 'v' },
      {"dump-start",  required_argument, 0, 'x' },
#endif
      {0, 0, 0, 0}
    };
    int option_index = 0;
#if VM_TRACE
    int c = getopt_long(argc, argv, "-chm:s:v:Vx:", long_options, &option_index);
#else
    int c = getopt_long(argc, argv, "-chm:s:V", long_options, &option_index);
#endif
    if (c == -1) break;
    switch (c) {
      // Process "normal" options with '--' long options or '-' short options
      case '?': usage(argv[0]);             return 1;
      case 'c': print_cycles = true;        break;
      case 'h': usage(argv[0]);             return 0;
      case 'm': max_cycles = atoll(optarg); break;
      case 's': random_seed = atoi(optarg); break;
      case 'V': verbose = true;             break;
#if VM_TRACE
      case 'v': {
        vcdfile = strcmp(optarg, "-") == 0 ? stdout : fopen(optarg, "w");
        if (!vcdfile) {
          std::cerr << "Unable to open " << optarg << " for VCD write\n";
          return 1;
        }
        break;
      }
      case 'x': start = atoll(optarg);      break;
#endif
      // Processing of legacy '+' options and recognition of when
      // we've hit the binary. The binary is expected to be a
      // non-option and not start with '-' or '+'.
      case 1: {
        std::string arg = optarg;
        if (arg == "+verbose")
          verbose = true;
        else if (arg.substr(0, 12) == "+max-cycles=")
          max_cycles = atoll(optarg+12);
#if VM_TRACE
        else if (arg.substr(0, 12) == "+dump-start=")
          start = atoll(optarg+12);
#endif
        else if (arg.substr(0, 12) == "+cycle-count")
          print_cycles = true;
        else {
          to_tether.push_back(optarg);
          goto done_processing;
        }
        break;
      }
    }
  }

done_processing:
  if (optind < argc)
    while (optind < argc)
      to_tether.push_back(argv[optind++]);
  if (!to_tether.size()) {
    std::cerr << "No binary specified for emulator\n";
    usage(argv[0]);
    return 1;
  }

  if (verbose)
    fprintf(stderr, "using random seed %u\n", random_seed);

  srand(random_seed);
  srand48(random_seed);

  Verilated::randReset(2);
  Verilated::commandArgs(argc, argv);
  TEST_HARNESS *tile = new TEST_HARNESS;

#if VM_TRACE
  Verilated::traceEverOn(true); // Verilator must compute traced signals
  std::unique_ptr<VerilatedVcdFILE> vcdfd(new VerilatedVcdFILE(vcdfile));
  std::unique_ptr<VerilatedVcdC> tfp(new VerilatedVcdC(vcdfd.get()));
  if (vcdfile) {
    tile->trace(tfp.get(), 99);  // Trace 99 levels of hierarchy
    tfp->open("");
  }
#endif

  TETHER_NAME = new TETHER_TYPE(to_tether);

  signal(SIGTERM, handle_sigterm);

  // reset for several cycles to handle pipelined reset
  for (int i = 0; i < 50; i++) {
    tile->reset = 1;
    tile->clock = 0;
    tile->eval();
    tile->clock = 1;
    tile->eval();
    tile->reset = 0;
  }
  done_reset = true;

  while (!TETHER_NAME->done() && !tile->io_success && trace_count < max_cycles) {
    tile->clock = 0;
    tile->eval();
#if VM_TRACE
    bool dump = tfp && trace_count >= start;
    if (dump)
      tfp->dump(static_cast<vluint64_t>(trace_count * 2));
#endif

    tile->clock = 1;
    tile->eval();
#if VM_TRACE
    if (dump)
      tfp->dump(static_cast<vluint64_t>(trace_count * 2 + 1));
#endif
    trace_count++;
  }

#if VM_TRACE
  if (tfp)
    tfp->close();
  if (vcdfile)
    fclose(vcdfile);
#endif

  if (TETHER_NAME->exit_code())
  {
    fprintf(stderr, "*** FAILED *** (code = %d, seed %d) after %ld cycles\n", TETHER_NAME->exit_code(), random_seed, trace_count);
    ret = TETHER_NAME->exit_code();
  }
  else if (trace_count == max_cycles)
  {
    fprintf(stderr, "*** FAILED *** (timeout, seed %d) after %ld cycles\n", random_seed, trace_count);
    ret = 2;
  }
  else if (verbose || print_cycles)
  {
    fprintf(stderr, "Completed after %ld cycles\n", trace_count);
  }

  if (TETHER_NAME) delete TETHER_NAME;
  if (tile) delete tile;
  return ret;
}
