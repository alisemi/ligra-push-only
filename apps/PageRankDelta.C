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
#include "math.h"
#include "chp_perf.h"

typedef double fType;

template <class vertex>
struct PR_F
{
  vertex *V;
  fType *Delta, *nghSum;
  PR_F(vertex *_V, fType *_Delta, fType *_nghSum) : V(_V), Delta(_Delta), nghSum(_nghSum) {}
  inline bool update(uintE s, uintE d)
  {
    fType oldVal = nghSum[d];
    nghSum[d] += Delta[s] / V[s].getOutDegree();
    return false;
  }
  inline bool updateAtomic(uintE s, uintE d)
  {
    volatile fType oldV, newV;
    do
    { // basically a fetch-and-add
      oldV = nghSum[d];
      newV = oldV + Delta[s] / V[s].getOutDegree();
    } while (!CAS(&nghSum[d], oldV, newV));
    return false;
  }
  inline bool cond(uintE d) { return cond_true(d); }
};

struct PR_Vertex_F_FirstRound
{
  fType damping, addedConstant, one_over_n, epsilon2;
  fType *p, *Delta, *nghSum;
  PR_Vertex_F_FirstRound(fType *_p, fType *_Delta, fType *_nghSum, fType _damping, fType _one_over_n, fType _epsilon2) : p(_p),
                                                                                                                         damping(_damping), Delta(_Delta), nghSum(_nghSum), one_over_n(_one_over_n),
                                                                                                                         addedConstant((1 - _damping) * _one_over_n),
                                                                                                                         epsilon2(_epsilon2) {}
  inline bool operator()(uintE i)
  {
    Delta[i] = damping * nghSum[i] + addedConstant;
    p[i] += Delta[i];
    Delta[i] -= one_over_n; // subtract off delta from initialization
    return (fabs(Delta[i]) > epsilon2 * p[i]);
  }
};

struct PR_Vertex_F
{
  fType damping, epsilon2;
  fType *p, *Delta, *nghSum;
  PR_Vertex_F(fType *_p, fType *_Delta, fType *_nghSum, fType _damping, fType _epsilon2) : p(_p),
                                                                                           damping(_damping), Delta(_Delta), nghSum(_nghSum),
                                                                                           epsilon2(_epsilon2) {}
  inline bool operator()(uintE i)
  {
    Delta[i] = nghSum[i] * damping;
    if (fabs(Delta[i]) > epsilon2 * p[i])
    {
      p[i] += Delta[i];
      return 1;
    }
    else
      return 0;
  }
};

struct PR_Vertex_Reset
{
  fType *nghSum;
  PR_Vertex_Reset(fType *_nghSum) : nghSum(_nghSum) {}
  inline bool operator()(uintE i)
  {
    nghSum[i] = 0.0;
    return 1;
  }
};

template <class vertex>
void Compute(graph<vertex> &GA, commandLine P, pvector<uintE> &new_ids)
{
  string events = P.getOptionValue("-e", "cycles:u");
  pair<char *, char *> filePairs = P.IOFileNames();
  string inputFileName = filesystem::path(filePairs.second).filename();

  Timer tm;
  tm.Start();
  long maxIters = P.getOptionLongValue("-maxiters", 100);
  const long n = GA.n;
  const fType damping = 0.85;
  const fType epsilon = 0.0000001;
  const fType epsilon2 = 0.01;

  fType one_over_n = 1 / (fType)n;
#ifndef ALIGNED
  fType *p = newA(fType, n), *Delta = newA(fType, n),
        *nghSum = newA(fType, n);
#else
  fType *p = newA(fType, n);
  fType *nghSum{nullptr};
  fType *Delta{nullptr};
  posix_memalign((void **)&Delta, 64, sizeof(fType) * n);
  posix_memalign((void **)&nghSum, 64, sizeof(fType) * n);
  assert(Delta != nullptr && ((uintptr_t)Delta % 64 == 0) && "App Malloc Failure\n");
  assert(nghSum != nullptr && ((uintptr_t)nghSum % 64 == 0) && "App Malloc Failure\n");
#endif
  bool *frontier = newA(bool, n);
  parallel_for(long i = 0; i < n; i++)
  {
    p[i] = 0.0;            // one_over_n;
    Delta[i] = one_over_n; // initial delta propagation from each vertex
    nghSum[i] = 0.0;
    frontier[i] = 1;
  }

  vertexSubset Frontier(n, n, frontier);
  bool *all = newA(bool, n);
  {
    parallel_for(long i = 0; i < n; i++) all[i] = 1;
  }
  vertexSubset All(n, n, all); // all vertices

  fType L1_norm{0.0};

  long round = 0;

  std::string result_filename = events;
  replace(result_filename.begin(), result_filename.end(), ',', '-');
  result_filename = "result_PageRankDelta_" + inputFileName + "_" + result_filename;

  struct perf_struct *perf = init_perf(events);
  reset_counter(perf);
  start_counter(perf);

  while (round++ < maxIters)
  {
    bool *nextBitmap{nullptr};
    edgeMap(GA, Frontier, nextBitmap, PR_F<vertex>(GA.V, Delta, nghSum), GA.m / 20, no_output | dense_forward);
    vertexSubset active = (round == 1) ? vertexFilter(All, PR_Vertex_F_FirstRound(p, Delta, nghSum, damping, one_over_n, epsilon2)) : vertexFilter(All, PR_Vertex_F(p, Delta, nghSum, damping, epsilon2));
    // compute L1-norm (use nghSum as temp array)
    {
      parallel_for(long i = 0; i < n; i++)
      {
        nghSum[i] = fabs(Delta[i]);
      }
    }
    L1_norm = sequence::plusReduce(nghSum, n);
    if (L1_norm < epsilon)
      break;
    // reset
    vertexMap(All, PR_Vertex_Reset(nghSum));
    Frontier.del();
    Frontier = active;
  }

  stop_counter(perf);
  read_counter(perf, NULL, result_filename);

  Frontier.del();
  free(p);
  All.del();
#ifndef ALIGNED
  free(Delta);
  free(nghSum);
#else
  free(Delta);
  free(nghSum);
#endif
  tm.Stop();
  tm.PrintTime("Run Time(sec) ", tm.Seconds());
  std::cout << "[OUTPUT] Num Iters = " << round << std::endl;
  std::cout << "[OUTPUT] L1_Norm   = " << L1_norm << std::endl;
}
