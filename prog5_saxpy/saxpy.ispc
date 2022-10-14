
export void saxpy_ispc(uniform int N,
                       uniform float scale,
                       uniform float X[],
                       uniform float Y[],
                       uniform float result[])
{
    foreach (i = 0 ... N) {           
        result[i] = scale * X[i] + Y[i];
    }
}

task void saxpy_ispc_task(uniform int N,
                          uniform int span,
                          uniform float scale,
                          uniform float X[], 
                          uniform float Y[],
                          uniform float result[])
{
    uniform int indexStart = taskIndex * span;
    uniform int indexEnd = min(N, indexStart + span);
    foreach (i = indexStart ... indexEnd) {
        result[i] = scale * X[i] + Y[i];
    }
}

task void saxpy_ispc_task2(uniform int N,
                           uniform float scale,
                           uniform float X[], 
                           uniform float Y[],
                           uniform float result[])
{
    for (uniform int i = 0; i < N; i += programCount) {
      // int idx = i + programIndex;
      int idx = min(N, i + programIndex);
      result[idx] = scale * X[idx] + Y[idx];
    }
}

export void saxpy_ispc_withtasks(uniform int N,
                                 uniform float scale,
                                 uniform float X[],
                                 uniform float Y[],
                                 uniform float result[])
{
    uniform int span = N / 64;  // 64 tasks
    launch[N/span] saxpy_ispc_task(N, span, scale, X, Y, result);
    // launch[N/span] saxpy_ispc_task2(N, scale, X, Y, result);

    // 2 gave 1.08x, 4 gave 1.06x.
    // uniform int tasks = 8;
    // uniform int span = N / tasks;
    // launch[tasks] saxpy_ispc_task(N, span, scale, X, Y, result);
}
