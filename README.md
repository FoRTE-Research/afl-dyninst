## AFL-Dyninst

Usage
```
Analysis options:
	-M: minimum block size (default: 1)
	-N: number of initial blocks to skip (default: 0)
	-X: input list of block addresses to skip
	-L: don't analyze binary, only libraries
	-l: linked libraries to analyze
	-r: runtime libraries to analyze
-A: output list for all blocks analyzed
Instrumentation options:
	-O: output instrumented binary
	-F: forkserver address (required for stripped binaries)
	-B: trace blocks
	-E: trace edges (experimental hashing)
	-e: trace edges (original AFL tracing)
	-D: attempt fixing Dyninst register bug
	-I: output list for all blocks instrumented
Additional options:
	-V: verbose mode\n";
```

Performance-related Bpatch options:
* `setDelayedParsing`   -- ?? (default ??)
* `setLivenessAnalysis` -- perf gain on true (default true)
* `setMergeTramp`       -- perf gain on true (default true)
* `setInstrStackFrames` -- perf loss on true (default false)
* `setSaveFPR`          -- perf gain on false (default true)
* `setTrampRecursive`   -- perf gain on true (default false)