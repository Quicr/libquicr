 #!/bin/sh
NUM_SUBS=500
parallel -j ${NUM_SUBS}  "./qperf_sub -i {} --connect_uri moq://192.168.1.110:1234 > ~/logs/t_{}logs.txt 2>&1" ::: $(seq ${NUM_SUBS})
