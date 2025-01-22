#!/bin/sh

NUM_SUBS=${1:-100} # Arg 1

echo "Running $NUM_SUBS subscriber clients"

parallel -j ${NUM_SUBS}  "./qperf_sub -i {} --connect_uri moq://localhost:33435 > t_{}logs.txt 2>&1" ::: $(seq ${NUM_SUBS})