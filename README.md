# Cache Simulator (C)

## Overview
This project is a configurable cache simulator written in C that models the behavior of CPU caches. It supports direct-mapped, set-associative, and fully associative cache organizations with FIFO and LRU replacement policies, as well as optional next-block prefetching.

The simulator reports detailed cache performance statistics, including hits, misses, memory reads, and writes.

---

## Features
- Direct-mapped, N-way set associative, and fully associative caches
- FIFO and LRU replacement policies
- Optional next-block prefetching
- Tracks cache hits, misses, memory reads, and memory writes

---

## Design Highlights
- Bit-level address manipulation for block offset, set index, and tag extraction
- Modular cache design supporting multiple configurations
- Replacement logic implemented using per-line aging metadata
- Separate simulation paths for prefetching and non-prefetching execution

---

## Output 
The simulator prints cache statistics for: 
- Execution without prefetching
- Execution with next block prefetching enabled
Reported metrics include:
- Memory reads
- Memory writes
- Cache hits
- Cache misses

--- 

## Usage 
After compiling, run the simulator with the following arguments: 
```./cachesim <cache_size> <associativity> <policy> <block_size> <trace_file>```
```./cachesim 128 assoc:2 lru 16 trace.txt```

Compile the simulator using the provided Makefile:

```bash
make


