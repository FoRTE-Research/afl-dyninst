## AFL-Dyninst

Usage
```
Analysis options:
	-m int  - minimum block size (default: 1)
	-n int  - number of initial blocks to skip (default: 0)
	-u      - do not skip over unrecognizable functions (recommended
	          only for closed-src or stripped binaries; default: false) 
Instrumentation options:
	-o file - output instrumented binary
	-f addr - custom forkserver address (required for stripped binaries)
	-b      - trace blocks
	-e      - trace edges
	-d      - attempt fixing Dyninst register bug
	-x      - optimization level 1 (graph optimizations)
	-xx     - optimization level 2 (Dyninst settings)
Additional options:
	-v      - verbose mode
```

Performance-related Bpatch options:
* `setDelayedParsing`   -- ?? (default ??)
* `setLivenessAnalysis` -- perf gain on true (default true)
* `setMergeTramp`       -- perf gain on true (default true)
* `setInstrStackFrames` -- perf loss on true (default false)
* `setSaveFPR`          -- perf gain on false (default true)
* `setTrampRecursive`   -- perf gain on true (default false)
