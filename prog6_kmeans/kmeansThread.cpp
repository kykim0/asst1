#include <algorithm>
#include <math.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <thread>

#include "CycleTimer.h"

using namespace std;

typedef struct {
  // Control work assignments
  int start, end;

  // Shared by all functions
  double *data;
  double *clusterCentroids;
  int *clusterAssignments;
  double *currCost;
  int M, N, K;
  int threadId;
  int numThreads;
} WorkerArgs;


/**
 * Checks if the algorithm has converged.
 * 
 * @param prevCost Pointer to the K dimensional array containing cluster costs 
 *    from the previous iteration.
 * @param currCost Pointer to the K dimensional array containing cluster costs 
 *    from the current iteration.
 * @param epsilon Predefined hyperparameter which is used to determine when
 *    the algorithm has converged.
 * @param K The number of clusters.
 * 
 * NOTE: DO NOT MODIFY THIS FUNCTION!!!
 */
static bool stoppingConditionMet(double *prevCost, double *currCost,
                                 double epsilon, int K) {
  for (int k = 0; k < K; k++) {
    if (abs(prevCost[k] - currCost[k]) > epsilon)
      return false;
  }
  return true;
}

/**
 * Computes L2 distance between two points of dimension nDim.
 * 
 * @param x Pointer to the beginning of the array representing the first
 *     data point.
 * @param y Poitner to the beginning of the array representing the second
 *     data point.
 * @param nDim The dimensionality (number of elements) in each data point
 *     (must be the same for x and y).
 */
double dist(double *x, double *y, int nDim) {
  double accum = 0.0;
  for (int i = 0; i < nDim; i++) {
    accum += pow((x[i] - y[i]), 2);
  }
  return sqrt(accum);
}

/**
 * Assigns each data point to its "closest" cluster centroid.
 */
void workerThreadStart(WorkerArgs *const args) {
  int numPoints = args->end - args->start;
  // Initialization.
  double *minDist = new double[numPoints];
  for (int i = 0; i < numPoints; ++i) {
    minDist[i] = 1e30;
    int m = args->start + i;
    args->clusterAssignments[m] = -1;
  }
  
  // Assign datapoints to closest centroids
  for (int i = 0; i < numPoints; ++i) {
    for (int k = 0; k < args->K; k++) {
      int m = args->start + i;
      double d = dist(&args->data[m * args->N],
                      &args->clusterCentroids[k * args->N], args->N);
      if (d < minDist[i]) {
        minDist[i] = d;
        args->clusterAssignments[m] = k;
      }
    }
  }
}

void computeAssignments(WorkerArgs *const args, int numThreads) {
  std::thread workers_arr[numThreads];
  WorkerArgs args_arr[numThreads];
  int numPointsPerThread = args->M / numThreads;
  for (int i = 0; i < numThreads; ++i) {
    args_arr[i].data = args->data;
    args_arr[i].clusterCentroids = args->clusterCentroids;
    args_arr[i].clusterAssignments = args->clusterAssignments;
    args_arr[i].currCost = args->currCost;
    args_arr[i].M = args->M; args_arr[i].N = args->N; args_arr[i].K = args->K;
    args_arr[i].threadId = i;
    args_arr[i].numThreads = numThreads;
    args_arr[i].start = numPointsPerThread * i;
    args_arr[i].end = std::min(numPointsPerThread * (i + 1), args->M);
  }

  for (int i = 1; i < numThreads; ++i) {
    workers_arr[i] = std::thread(workerThreadStart, &args_arr[i]);
  }
  workerThreadStart(&args_arr[0]);
  for (int i = 1; i < numThreads; ++i) {
    workers_arr[i].join();
  }
}

/**
 * Given the cluster assignments, computes the new centroid locations for
 * each cluster.
 */
void computeCentroids(WorkerArgs *const args) {
  int *counts = new int[args->K];

  // Zero things out
  for (int k = 0; k < args->K; k++) {
    counts[k] = 0;
    for (int n = 0; n < args->N; n++) {
      args->clusterCentroids[k * args->N + n] = 0.0;
    }
  }


  // Sum up contributions from assigned examples
  for (int m = 0; m < args->M; m++) {
    int k = args->clusterAssignments[m];
    for (int n = 0; n < args->N; n++) {
      args->clusterCentroids[k * args->N + n] +=
          args->data[m * args->N + n];
    }
    counts[k]++;
  }

  // Compute means
  for (int k = 0; k < args->K; k++) {
    counts[k] = max(counts[k], 1); // prevent divide by 0
    for (int n = 0; n < args->N; n++) {
      args->clusterCentroids[k * args->N + n] /= counts[k];
    }
  }

  free(counts);
}

/**
 * Computes the per-cluster cost. Used to check if the algorithm has converged.
 */
void computeCost(WorkerArgs *const args) {
  double *accum = new double[args->K];

  // Zero things out
  for (int k = 0; k < args->K; k++) {
    accum[k] = 0.0;
  }

  // Sum cost for all data points assigned to centroid
  for (int m = 0; m < args->M; m++) {
    int k = args->clusterAssignments[m];
    accum[k] += dist(&args->data[m * args->N],
                     &args->clusterCentroids[k * args->N], args->N);
  }

  // Update costs
  for (int k = 0; k < args->K; k++) {
    args->currCost[k] = accum[k];
  }

  free(accum);
}

/**
 * Computes the K-Means algorithm, using std::thread to parallelize the work.
 *
 * @param data Pointer to an array of length M*N representing the M different N 
 *     dimensional data points clustered. The data is layed out in a "data point
 *     major" format, so that data[i*N] is the start of the i'th data point in 
 *     the array. The N values of the i'th datapoint are the N values in the 
 *     range data[i*N] to data[(i+1) * N].
 * @param clusterCentroids Pointer to an array of length K*N representing the K 
 *     different N dimensional cluster centroids. The data is laid out in
 *     the same way as explained above for data.
 * @param clusterAssignments Pointer to an array of length M representing the
 *     cluster assignments of each data point, where clusterAssignments[i] = j
 *     indicates that data point i is closest to cluster centroid j.
 * @param M The number of data points to cluster.
 * @param N The dimensionality of the data points.
 * @param K The number of cluster centroids.
 * @param epsilon The algorithm is said to have converged when
 *     |currCost[i] - prevCost[i]| < epsilon for all i where i = 0, 1, ..., K-1
 */
void kMeansThread(double *data, double *clusterCentroids, int *clusterAssignments,
                  int M, int N, int K, double epsilon) {

  // Used to track convergence
  double *prevCost = new double[K];
  double *currCost = new double[K];

  // The WorkerArgs array is used to pass inputs to and return output from
  // functions.
  WorkerArgs args;
  args.data = data;
  args.clusterCentroids = clusterCentroids;
  args.clusterAssignments = clusterAssignments;
  args.currCost = currCost;
  args.M = M;
  args.N = N;
  args.K = K;

  // Initialize arrays to track cost
  for (int k = 0; k < K; k++) {
    prevCost[k] = 1e30;
    currCost[k] = 0.0;
  }

  /* Main K-Means Algorithm Loop */
  int numThreads = 16;
  int iter = 0;
  while (!stoppingConditionMet(prevCost, currCost, epsilon, K)) {
    // Update cost arrays (for checking convergence criteria)
    for (int k = 0; k < K; k++) {
      prevCost[k] = currCost[k];
    }

    // double startTime = CycleTimer::currentSeconds();
    computeAssignments(&args, numThreads);
    // double endTime = CycleTimer::currentSeconds();
    // double assignmentTime = (endTime - startTime) * 1000;
    // startTime = CycleTimer::currentSeconds();
    computeCentroids(&args);
    // endTime = CycleTimer::currentSeconds();
    // double centroidTime = (endTime - startTime) * 1000;
    // startTime = CycleTimer::currentSeconds();
    computeCost(&args);
    // endTime = CycleTimer::currentSeconds();
    // double costTime = (endTime - startTime) * 1000;

    // if (iter % 10 == 0) {
    //   printf("[Assignment Time]: %.3f ms\n", assignmentTime);
    //   printf("[Centroid Time]: %.3f ms\n", centroidTime);
    //   printf("[Cost Time]: %.3f ms\n", costTime);
    // }

    iter++;
  }

  free(currCost);
  free(prevCost);
}
