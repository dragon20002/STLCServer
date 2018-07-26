#include "cuda.h"

extern void run_detector(int argc, char **argv);

int main(int argc, char **argv) {
    gpu_index = find_int_arg(argc, argv, "-i", 0);
    if (find_arg(argc, argv, "-nogpu"))
        gpu_index = -1;

#ifndef GPU
    gpu_index = -1;
#else
    if (gpu_index >= 0)
        cuda_set_device(gpu_index);
#endif
    // detector
    run_detector(argc, argv);

    return 0;
}
