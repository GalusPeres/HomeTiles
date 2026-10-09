#pragma once

// Network measurement build (camera full screen #65): TCP sinks that read and
// discard what a PC sends and log the Mbit/s, to tell the panel's raw receive
// rate from the camera protocol's. Guition V2 only; a no-op elsewhere.
//   5001: plain receive, normal priority
//   5002: plain receive, idle priority (the camera task's)
//   5003: 4-byte acknowledgement per 8 KB, idle priority (the camera protocol)
void net_bench_start();
