// An OpenMP clause keyword: num_threads
//  used as a regular variable in a clause's variable list
//  Extracted from NPB 3.2 benchmarks
//
//  6/10/2010
#ifdef _OPENMP
#include <omp.h>
#endif

void c_print_results( )
{
  int num_threads, max_threads;


  max_threads = 1;
  num_threads = 1;

  /*   figure out number of threads used */
#ifdef _OPENMP
  max_threads = omp_get_max_threads();
#pragma omp parallel num_threads(6) shared(num_threads)
  {   
#pragma omp master
    num_threads = omp_get_num_threads();
  }
#endif


}
