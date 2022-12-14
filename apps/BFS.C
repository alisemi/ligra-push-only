// This code is part of the project "Ligra: A Lightweight Graph Processing
// Framework for Shared Memory", presented at Principles and Practice of
// Parallel Programming, 2013.
// Copyright (c) 2013 Julian Shun and Guy Blelloch
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights (to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#include "ligra.h"
#include "chp_perf.h"

const uintE MIN_IDENTITY{UINT_E_MAX};

struct BFS_F
{
  bool *nextBitmap;
  uintE *Parents;
  BFS_F(bool *_nextBitmap, uintE *_Parents) : nextBitmap(_nextBitmap), Parents(_Parents) {}
  inline bool update(uintE s, uintE d)
  { // Update
    if (Parents[d] == UINT_E_MAX)
    {
      Parents[d] = s;
      return 1;
    }
    else
      return 0;
  }
  inline bool updateAtomic(uintE s, uintE d)
  { // atomic version of Update
    bool r{false};
    if (Parents[d] == UINT_E_MAX)
    {
      r = (CAS(&Parents[d], UINT_E_MAX, s));
      if (r == true)
        nextBitmap[d] = true;
    }
    return false;
  }
  // cond function checks if vertex has been visited yet
  // inline bool cond (uintE d) { return (Parents[d] == UINT_E_MAX); }
  inline bool cond(uintE d) { return cond_true(d); }
};

void writeOutputToFile(uintE *arr, long numElements, pvector<uintE> &new_ids)
{
  std::ofstream fout;
  if (new_ids[0] == new_ids[1])
  {
    // graph not preprocessed
    std::string prefixStr{"AppOutput-nopreprocess.out"};
    fout.open(prefixStr);
    for (long v = 0; v < numElements; ++v)
    {
      fout << arr[v] << "\n";
    }
  }
  else
  {
    std::string prefixStr{"AppOutput.out"};
    fout.open(prefixStr);
    for (long v = 0; v < numElements; ++v)
    {
      fout << arr[new_ids[v]] << "\n";
    }
  }
  fout.close();
  return;
}

template <class vertex>
void Compute(graph<vertex> &GA, commandLine P, pvector<uintE> &new_ids)
{
  string events = P.getOptionValue("-e", "cycles:u");
  pair<char *, char *> filePairs = P.IOFileNames();
  string inputFileName = filesystem::path(filePairs.second).filename();

  Timer tm;
  tm.Start();
  bool preprocessed = (new_ids[0] != new_ids[1]);
  long start = P.getOptionLongValue("-r", 0);
  if (preprocessed)
    start = new_ids[start];
  long n = GA.n;
// creates Parents array, initialized to all -1, except for start
#ifndef ALIGNED
  uintE *Parents = newA(uintE, n);
#else
  uintE *Parents{nullptr};
  posix_memalign((void **)&Parents, 64, sizeof(uintE) * n);
  assert(Parents != nullptr && ((uintptr_t)Parents % 64 == 0) && "App Malloc Failure\n");
#endif
  parallel_for(long i = 0; i < n; i++) Parents[i] = UINT_E_MAX;
  Parents[start] = start;
  vertexSubset Frontier(n, start); // creates initial frontier
  int iter{0};

  std::string result_filename = events;
  replace(result_filename.begin(), result_filename.end(), ',', '-');
  result_filename = "result_BFS_" + inputFileName + "_" + result_filename;

  struct perf_struct *perf = init_perf(events);
  reset_counter(perf);
  start_counter(perf);

  while (!Frontier.isEmpty())
  { // loop until frontier is empty
    bool *nextBitmap = newA(bool, n);
    assert(nextBitmap != nullptr);
    vertexSubset output = edgeMap(GA, Frontier, nextBitmap, BFS_F(nextBitmap, Parents), -1, dense_forward);
    Frontier.del();
    Frontier = output; // set new frontier
    ++iter;
  }

  stop_counter(perf);
  read_counter(perf, NULL, result_filename);

  Frontier.del();
  // writeOutputToFile(Parents, n, new_ids);
  // free(Parents);
  tm.Stop();
  tm.PrintTime("Run Time(sec) ", tm.Seconds());
  // computing BFS-Tree stats
  std::cout << "[OUTPUT] Num. Iters until convergence = " << iter << std::endl;
  long numNodes{0};
#pragma omp parallel for reduction(+ \
                                   : numNodes)
  for (long v = 0; v < GA.n; ++v)
  {
    if (Parents[v] != MIN_IDENTITY)
      ++numNodes;
  }
  std::cout << "[OUTPUT] No. of nodes in BFS-Tree = " << numNodes << std::endl;
  free(Parents);
}
