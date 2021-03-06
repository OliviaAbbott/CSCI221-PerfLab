#include <stdio.h>
#include "cs1300bmp.h"
#include <iostream>
#include <stdint.h>
#include <fstream>
#include <omp.h>
#include "Filter.h"

using namespace std;

#include "rtdsc.h"

//
// Forward declare the functions
//
Filter * readFilter(string filename);
double applyFilter(Filter *filter, cs1300bmp *input, cs1300bmp *output);

  int
main(int argc, char **argv)
{

  if ( argc < 2) {
    fprintf(stderr,"Usage: %s filter inputfile1 inputfile2 .... \n", argv[0]);
  }

  //
  // Convert to C++ strings to simplify manipulation
  //
  string filtername = argv[1];

  //
  // remove any ".filter" in the filtername
  //
  string filterOutputName = filtername;
  string::size_type loc = filterOutputName.find(".filter");
  if (loc != string::npos) {
    //
    // Remove the ".filter" name, which should occur on all the provided filters
    //
    filterOutputName = filtername.substr(0, loc);
  }

  Filter *filter = readFilter(filtername);

  double sum = 0.0;
  int samples = 0;

  for (int inNum = 2; inNum < argc; inNum++) {
    string inputFilename = argv[inNum];
    string outputFilename = "filtered-" + filterOutputName + "-" + inputFilename;
    struct cs1300bmp *input = new struct cs1300bmp;
    struct cs1300bmp *output = new struct cs1300bmp;
    int ok = cs1300bmp_readfile( (char *) inputFilename.c_str(), input);

    if ( ok ) {
      double sample = applyFilter(filter, input, output);
      sum += sample;
      samples++;
      cs1300bmp_writefile((char *) outputFilename.c_str(), output);
    }
    delete input;
    delete output;
  }
  fprintf(stdout, "Average cycles per sample is %f\n", sum / samples);

}

  struct Filter *
readFilter(string filename)
{
  ifstream input(filename.c_str());

  if ( ! input.bad() ) {
    int size = 0;
    input >> size;
    Filter *filter = new Filter(size);
    int div;
    input >> div;
    filter -> setDivisor(div);
    for (int i=0; i < size; i++) {
      for (int j=0; j < size; j++) {
        int value;
        input >> value;
        filter -> set(i,j,value);
      }
    }
    return filter;
  }
}

#if defined(__arm__)
static inline unsigned int get_cyclecount (void)
{
  unsigned int value;
  // Read CCNT Register
  asm volatile ("MRC p15, 0, %0, c9, c13, 0\t\n": "=r"(value)); 
  return value;
}

static inline void init_perfcounters (int32_t do_reset, int32_t enable_divider)
{
  // in general enable all counters (including cycle counter)
  int32_t value = 1;

  // peform reset: 
  if (do_reset)
  {
    value |= 2;     // reset all counters to zero.
    value |= 4;     // reset cycle counter to zero.
  }

  if (enable_divider)
    value |= 8;     // enable "by 64" divider for CCNT.

  value |= 16;

  // program the performance-counter control-register:
  asm volatile ("MCR p15, 0, %0, c9, c12, 0\t\n" :: "r"(value)); 

  // enable all counters: 
  asm volatile ("MCR p15, 0, %0, c9, c12, 1\t\n" :: "r"(0x8000000f)); 

  // clear overflows:
  asm volatile ("MCR p15, 0, %0, c9, c12, 3\t\n" :: "r"(0x8000000f));
}



#endif



  double
applyFilter(struct Filter *filter, cs1300bmp *input, cs1300bmp *output)
{
#if defined(__arm__)
  init_perfcounters (1, 1);
#endif

  long long cycStart, cycStop;
  double start,stop;
#if defined(__arm__)
  cycStart = get_cyclecount();
#else
  cycStart = rdtscll();
#endif
  output -> width = input -> width;
  output -> height = input -> height;

  int divisor = filter -> getDivisor();
  int width = (input -> width) - 1;
  int height = (input -> height) - 1;
  int t = 0;
  
  #pragma omp parallel for reduction(+:val)
  //call input -> width and input -> height once
  for(int col = 1; col < width; col = col + 1) 
  {
    for(int row = 1; row < height; row = row + 1) 
    {
      for(int plane = 0; plane < 3; plane++) 
      {
        //move to outside loop
        //int t = 0;
        
        //memory aliasing
        //output -> color[plane][row][col] = 0;

        //chane get_size to actual size of filter, they should all be same
        //unroll for loops below?
        //for (int j = 0; j < 3; j++) 
        //{
          //for (int i = 0; i < 3; i++) 
          //{	
            {
            int j = 0;
            int i = 0;
            //int val = output -> color[plane][row][col];
            int val = 0;
            val += (input -> color[plane][row + i - 1][col + j - 1] 
                  * filter -> get(i, j) );
            val += (input -> color[plane][row + i][col + j - 1] 
                  * filter -> get(i + 1, j) );
            val += (input -> color[plane][row + i + 1][col + j - 1] 
                  * filter -> get(i + 2, j) );
            //}
            val += (input -> color[plane][row + i - 1][col + j] 
                  * filter -> get(i, j + 1) );
            val += (input -> color[plane][row + i][col + j] 
                  * filter -> get(i + 1, j + 1) );
            val += (input -> color[plane][row + i + 1][col + j] 
                  * filter -> get(i + 2, j + 1) );

            val += (input -> color[plane][row + i - 1][col + j + 1] 
                  * filter -> get(i, j + 2) );
            val += (input -> color[plane][row + i][col + j + 1] 
                  * filter -> get(i + 1, j + 2) );
            val += (input -> color[plane][row + i + 1][col + j + 1] 
                  * filter -> get(i + 2, j + 2) );

            output -> color[plane][row][col] = val;
            }
        //if divisor is one then dont do the division by the divisor
        //also move filter -> getDivisor out of loops
        if(divisor != 1)
        {
          output -> color[plane][row][col] = 	
            output -> color[plane][row][col] / divisor;
        }
        else
        {
          output -> color[plane][row][col] = 	
            output -> color[plane][row][col];
        }
        if ( output -> color[plane][row][col]  < 0 ) 
        {
          output -> color[plane][row][col] = 0;
        }
        //change to else if?
        else if ( output -> color[plane][row][col]  > 255 ) 
        { 
          output -> color[plane][row][col] = 255;
        }
      }
    }
  }
#if defined(__arm__)
  cycStop = get_cyclecount();
#else
  cycStop = rdtscll();
#endif

  double diff = cycStop-cycStart;
#if defined(__arm__)
  diff = diff * 64;
#endif
  double diffPerPixel = diff / (output -> width * output -> height);
  fprintf(stderr, "Took %f cycles to process, or %f cycles per pixel\n",
      diff, diff / (output -> width * output -> height));
  return diffPerPixel;
}
