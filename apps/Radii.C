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

const intE MAX_IDENTITY{-1};
// atomically do bitwise-OR of *a with b and store in location a
template <class ET>
inline void writeOr(ET *a, ET b)
{
  volatile ET newV, oldV;
  do
  {
    oldV = *a;
    newV = oldV | b;
  } while ((oldV != newV) && !CAS(a, oldV, newV));
}

struct Radii_F
{
  bool *nextBitmap;
  intE round;
  intE *radii;
  long *Visited, *NextVisited;
  Radii_F(bool *_nextBitmap, long *_Visited, long *_NextVisited, intE *_radii, intE _round) : nextBitmap(_nextBitmap), Visited(_Visited), NextVisited(_NextVisited), radii(_radii), round(_round)
  {
  }
  inline bool update(uintE s, uintE d)
  { // Update function does a bitwise-or
    long toWrite = Visited[d] | Visited[s];
    if (Visited[d] != toWrite)
    {
      NextVisited[d] |= toWrite;
      if (radii[d] != round)
      {
        radii[d] = round;
        return 1;
      }
    }
    return 0;
  }
  inline bool updateAtomic(uintE s, uintE d)
  { // atomic Update
    long toWrite = Visited[d] | Visited[s];
    if (Visited[d] != toWrite)
    {
      writeOr(&NextVisited[d], toWrite);
      bool r = false;
      intE oldRadii = radii[d];
      if (radii[d] != round)
        r = CAS(&radii[d], oldRadii, round);
      if (r == true)
        nextBitmap[d] = true;
    }
    return 0;
  }
  inline bool cond(uintE d) { return cond_true(d); }
};

// function passed to vertex map to sync NextVisited and Visited
struct Radii_Vertex_F
{
  long *Visited, *NextVisited;
  Radii_Vertex_F(long *_Visited, long *_NextVisited) : Visited(_Visited), NextVisited(_NextVisited) {}
  inline bool operator()(uintE i)
  {
    Visited[i] = NextVisited[i];
    return 1;
  }
};

void writeOutputToFile(intE *arr, long numElements, pvector<uintE> &new_ids)
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
  long n = GA.n;
  intE *radii = newA(intE, n);
#ifndef ALIGNED
  long *Visited = newA(long, n), *NextVisited = newA(long, n);
#else
  long *NextVisited = newA(long, n);
  long *Visited{nullptr};
  posix_memalign((void **)&Visited, 64, sizeof(long) * n);
  assert(Visited != nullptr && ((uintptr_t)Visited % 64 == 0) && "App Malloc Failure\n");
#endif
  {
    parallel_for(long i = 0; i < n; i++)
    {
      radii[i] = MAX_IDENTITY;
      Visited[i] = NextVisited[i] = 0;
    }
  }
  long sampleSize = min(n, (long)64);
  uintE *starts = newA(uintE, sampleSize);

  {
    parallel_for(ulong i = 0; i < sampleSize; i++)
    { // initial set of vertices
      uintE v = hashInt(i) % n;
      if (preprocessed)
        v = new_ids[v];
      radii[v] = 0;
      starts[i] = v;
      NextVisited[v] = (long)1 << i;
    }
  }

  vertexSubset Frontier(n, sampleSize, starts); // initial frontier of size 64

  std::string result_filename = events;
  replace(result_filename.begin(), result_filename.end(), ',', '-');
  result_filename = "result_Radii_" + inputFileName + "_" + result_filename;

  struct perf_struct *perf = init_perf(events);
  reset_counter(perf);
  start_counter(perf);

  intE round = 0;
  while (!Frontier.isEmpty())
  {
    round++;
    vertexMap(Frontier, Radii_Vertex_F(Visited, NextVisited));
    bool *nextBitmap = newA(bool, n);
    assert(nextBitmap != nullptr);
    vertexSubset output = edgeMap(GA, Frontier, nextBitmap, Radii_F(nextBitmap, Visited, NextVisited, radii, round), -1, dense_forward);
    Frontier.del();
    Frontier = output;
  }

  stop_counter(perf);
  read_counter(perf, NULL, result_filename);

  // std::cout << "Iterations until convergence = " << round << std::endl;
  // writeOutputToFile(radii, n, new_ids);
  free(Visited);
  free(NextVisited);
  Frontier.del(); /*free(radii)*/
  ;
  tm.Stop();
  tm.PrintTime("Run Time(sec) ", tm.Seconds());
  std::cout << "[OUTPUT] Iters until convergence = " << round << std::endl;
#if 0
  // computing graph radius
  intE maxRadius {MAX_IDENTITY};
#pragma omp parallel for reduction(max \
                                   : maxRadius)
  for (long v = 0; v < GA.n; ++v)
    maxRadius = std::max(maxRadius, radii[v]); 
  
  std::cout << "[OUTPUT] Graph Radius = " << maxRadius << std::endl;
#endif
  free(radii);
}
