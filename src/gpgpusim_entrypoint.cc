/*
 * gpgpusim_entrypoint.c
 *
 * Copyright © 2009 by Tor M. Aamodt, Wilson W. L. Fung, Ali Bakhoda, 
 * George L. Yuan and the University of British Columbia, Vancouver, 
 * BC V6T 1Z4, All Rights Reserved.
 * 
 * THIS IS A LEGAL DOCUMENT BY DOWNLOADING GPGPU-SIM, YOU ARE AGREEING TO THESE
 * TERMS AND CONDITIONS.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNERS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 * NOTE: The files libcuda/cuda_runtime_api.c and src/cuda-sim/cuda-math.h
 * are derived from the CUDA Toolset available from http://www.nvidia.com/cuda
 * (property of NVIDIA).  The files benchmarks/BlackScholes/ and 
 * benchmarks/template/ are derived from the CUDA SDK available from 
 * http://www.nvidia.com/cuda (also property of NVIDIA).  The files from 
 * src/intersim/ are derived from Booksim (a simulator provided with the 
 * textbook "Principles and Practices of Interconnection Networks" available 
 * from http://cva.stanford.edu/books/ppin/). As such, those files are bound by 
 * the corresponding legal terms and conditions set forth separately (original 
 * copyright notices are left in files from these sources and where we have 
 * modified a file our copyright notice appears before the original copyright 
 * notice).  
 * 
 * Using this version of GPGPU-Sim requires a complete installation of CUDA 
 * which is distributed seperately by NVIDIA under separate terms and 
 * conditions.  To use this version of GPGPU-Sim with OpenCL requires a
 * recent version of NVIDIA's drivers which support OpenCL.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * 3. Neither the name of the University of British Columbia nor the names of
 * its contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * 
 * 4. This version of GPGPU-SIM is distributed freely for non-commercial use only.  
 *  
 * 5. No nonprofit user may place any restrictions on the use of this software,
 * including as modified by the user, by any other authorized user.
 * 
 * 6. GPGPU-SIM was developed primarily by Tor M. Aamodt, Wilson W. L. Fung, 
 * Ali Bakhoda, George L. Yuan, at the University of British Columbia, 
 * Vancouver, BC V6T 1Z4
 */

#include "gpgpusim_entrypoint.h"
#include <stdio.h>

#include "option_parser.h"
#include "cuda-sim/cuda-sim.h"
#include "cuda-sim/ptx_ir.h"
#include "cuda-sim/ptx_parser.h"
#include "gpgpu-sim/gpu-sim.h"
#include "gpgpu-sim/icnt_wrapper.h"

#include <pthread.h>
#include <semaphore.h>

#define MAX(a,b) (((a)>(b))?(a):(b))

static int sg_argc = 3;
static const char *sg_argv[] = {"", "-config","gpgpusim.config"};

struct gpgpu_ptx_sim_arg *grid_params;

sem_t g_sim_signal_start;
sem_t g_sim_signal_finish;
time_t g_simulation_starttime;
pthread_t g_simulation_thread;

gpgpu_sim g_the_gpu;

static void print_simulation_time();

void *gpgpu_sim_thread(void*)
{
   do {
      sem_wait(&g_sim_signal_start);
      unsigned grid;
      class function_info *entry;
      g_the_gpu.next_grid(grid,entry);
      g_the_gpu.run_gpu_sim();
      print_simulation_time();
      sem_post(&g_sim_signal_finish);
   } while(1);
   return NULL;
}

gpgpu_sim *gpgpu_ptx_sim_init_perf()
{
   print_splash();
   read_sim_environment_variables();
   read_parser_environment_variables();
   option_parser_t opp = option_parser_create();
   icnt_reg_options(opp);
   g_the_gpu.reg_options(opp); // register GPU microrachitecture options
   ptx_reg_options(opp);
   option_parser_cmdline(opp, sg_argc, sg_argv); // parse configuration options

   srand(1);

   // Open instructions debug output file for writing
   if(g_ptx_inst_debug_to_file != 0) {
      ptx_inst_debug_file = fopen(g_ptx_inst_debug_file, "w");
   }

   fprintf(stdout, "GPGPU-Sim: Configuration options:\n\n");
   option_parser_print(opp, stdout);

   g_the_gpu.init_gpu();

   g_simulation_starttime = time((time_t *)NULL);

   sem_init(&g_sim_signal_start,0,0);
   sem_init(&g_sim_signal_finish,0,0);
   pthread_create(&g_simulation_thread,NULL,gpgpu_sim_thread,NULL);

   return &g_the_gpu;
}

void print_simulation_time()
{
   time_t current_time, difference, d, h, m, s;
   current_time = time((time_t *)NULL);
   difference = MAX(current_time - g_simulation_starttime, 1);

   d = difference/(3600*24);
   h = difference/3600 - 24*d;
   m = difference/60 - 60*(h + 24*d);
   s = difference - 60*(m + 60*(h + 24*d));

   fflush(stderr);
   printf("\n\ngpgpu_simulation_time = %u days, %u hrs, %u min, %u sec (%u sec)\n",
          (unsigned)d, (unsigned)h, (unsigned)m, (unsigned)s, (unsigned)difference );
   printf("gpgpu_simulation_rate = %u (inst/sec)\n", (unsigned)(gpu_tot_sim_insn / difference) );
   printf("gpgpu_simulation_rate = %u (cycle/sec)\n", (unsigned)(gpu_tot_sim_cycle / difference) );
   fflush(stdout);
}

int gpgpu_cuda_ptx_sim_main_perf( kernel_info_t grid,
                                  struct dim3 gridDim, 
                                  struct dim3 blockDim, 
                                  gpgpu_ptx_sim_arg_list_t grid_params )
{
   g_the_gpu.launch(grid);
   sem_post(&g_sim_signal_start);
   sem_wait(&g_sim_signal_finish);
   return 0;
}

int gpgpu_opencl_ptx_sim_main_perf( class function_info *entry, 
                                  struct dim3 gridDim, 
                                  struct dim3 blockDim, 
                                  gpgpu_ptx_sim_arg_list_t grid_params )
{
   kernel_info_t grid = gpgpu_opencl_ptx_sim_init_grid(entry,grid_params,gridDim,blockDim);
   g_the_gpu.launch(grid);
   sem_post(&g_sim_signal_start);
   sem_wait(&g_sim_signal_finish);
   return 0;
}

int gpgpu_opencl_ptx_sim_main_func( class function_info *entry, 
                                  struct dim3 gridDim, 
                                  struct dim3 blockDim, 
                                  gpgpu_ptx_sim_arg_list_t grid_params )
{
   printf("GPGPU-Sim PTX API: OpenCL functional-only simulation not yet implemented (use performance simulation)\n");
   exit(1);
}
