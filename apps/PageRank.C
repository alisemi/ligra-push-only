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
struct PR_F {
  fType* p_curr, *p_next;
  vertex* V;
  PR_F(fType* _p_curr, fType* _p_next, vertex* _V) : 
    p_curr(_p_curr), p_next(_p_next), V(_V) {}
  inline bool update(uintE s, uintE d){ //update function applies PageRank equation
    p_next[d] += p_curr[s]/V[s].getOutDegree();
    return 0;
  }
  inline bool updateAtomic (uintE s, uintE d) { //atomic Update
    writeAdd(&p_next[d],p_curr[s]/V[s].getOutDegree());
    return 0;
  }
  inline bool cond (intT d) { return cond_true(d); }};

//vertex map function to update its p value according to PageRank equation
struct PR_Vertex_F {
  fType damping;
  fType addedConstant;
  fType* p_curr;
  fType* p_next;
  PR_Vertex_F(fType* _p_curr, fType* _p_next, fType _damping, intE n) :
    p_curr(_p_curr), p_next(_p_next), 
    damping(_damping), addedConstant((1-_damping)*(1/(fType)n)){}
  inline bool operator () (uintE i) {
    p_next[i] = damping*p_next[i] + addedConstant;
    return 1;
  }
};

//resets p
struct PR_Vertex_Reset {
  fType* p_curr;
  PR_Vertex_Reset(fType* _p_curr) :
    p_curr(_p_curr) {}
  inline bool operator () (uintE i) {
    p_curr[i] = 0.0;
    return 1;
  }
};

void writeOutputToFile(fType* arr, long numElements, pvector<uintE> &new_ids) {
    std::ofstream fout;
    std::string prefixStr {"AppOutput.out"};
    fout.open(prefixStr);
    if (new_ids[0] == new_ids[1]) {
        // graph not preprocessed
        for (long v = 0; v < numElements; ++v) {
            fout << arr[v] << "\n";
        }
    }
    else {
        for (long v = 0; v < numElements; ++v) {
            fout << arr[new_ids[v]] << "\n";
        }
    }
    fout.close();
    return;
}


template <class vertex>
void Compute(graph<vertex>& GA, commandLine P, pvector<uintE> &new_ids) {
  string events = P.getOptionValue("-e","0x53003c");
  pair<char*,char*> filePairs = P.IOFileNames();
  string inputFileName = filesystem::path( filePairs.second  ).filename();

  Timer tm;
  tm.Start();
  long maxIters = P.getOptionLongValue("-maxiters",100);
  const intE n = GA.n;
  const fType damping = 0.85, epsilon = 0.0000001;
  //const fType damping = 0.85, epsilon = 0.0001;

  
  fType one_over_n = 1/(fType)n;
  #ifndef ALIGNED
  fType* p_curr = newA(fType,n);
  #else
  fType* p_curr {nullptr};
  posix_memalign((void**) &p_curr, 64, sizeof(fType) * n);
  assert(p_curr != nullptr && ((uintptr_t)p_curr % 64 == 0) && "App Malloc Failure\n");
  #endif
  {parallel_for(long i=0;i<n;i++) p_curr[i] = one_over_n;}
  fType* p_next = newA(fType,n);
  {parallel_for(long i=0;i<n;i++) p_next[i] = 0;} //0 if unchanged
  bool* frontier = newA(bool,n);
  {parallel_for(long i=0;i<n;i++) frontier[i] = 1;}

  vertexSubset Frontier(n,n,frontier);
  
  long iter = 0;
  fType L1_norm {0.0};

  std::string result_filename = events;
  replace(result_filename.begin(), result_filename.end(), ',', '-');
  result_filename = "result_PageRank_" + inputFileName + "_" + result_filename;

  struct perf_struct *perf = init_perf(events);
  reset_counter(perf);
  start_counter(perf);

  while(iter++ < maxIters) {
    bool* nextBitmap {nullptr};
    edgeMap(GA,Frontier,nextBitmap,PR_F<vertex>(p_curr,p_next,GA.V),-1, no_output | dense_forward);
    vertexMap(Frontier,PR_Vertex_F(p_curr,p_next,damping,n));
    //compute L1-norm between p_curr and p_next
    {parallel_for(long i=0;i<n;i++) {
      p_curr[i] = fabs(p_curr[i]-p_next[i]);
      }}
    L1_norm = sequence::plusReduce(p_curr,n);
    if(L1_norm < epsilon) break;
    //reset p_curr
    vertexMap(Frontier,PR_Vertex_Reset(p_curr));
    swap(p_curr,p_next);
  }

  stop_counter(perf);
  read_counter(perf, NULL, result_filename);

  //std::cout << "Num Iters until convergence = " << iter << std::endl;
  //writeOutputToFile(p_next, n, new_ids);
  Frontier.del(); free(p_curr); free(p_next); 
  tm.Stop();
  tm.PrintTime("Run Time(sec) ", tm.Seconds()); 
  std::cout << "[OUTPUT] Num Iters = " << iter << std::endl;
  std::cout << "[OUTPUT] L1_Norm   = " << L1_norm << std::endl;
}
