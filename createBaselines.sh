#!/bin/sh

find ../fuzzing-benchmarks/ -maxdepth 1 -name "*Original" -exec bash -c "./afl-dyninst -i {} -o {}Dyninst" \;

