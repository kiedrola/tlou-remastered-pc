/* GOW3_TIMING_LOG=1: sample the guest's timing globals against host real time.
 * See runtime_timing_probe.c for the globals and offsets. No-op without the env var. */
#ifndef RUNTIME_TIMING_PROBE_H
#define RUNTIME_TIMING_PROBE_H
#include <stdint.h>
void runtime_timing_probe_start(unsigned char *image, uint64_t size);
#endif
