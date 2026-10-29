#!/bin/bash
# usage: ./watch.sh <pid> - prints the process state every second
pid=$1
while [ -d /proc/$pid ]; do
    printf "%s  state=%s  voluntary=%s  involuntary=%s\n" "$(date +%T)" \
        "$(awk '{print $3}' /proc/$pid/stat)" \
        "$(awk '/^voluntary/{print $2}' /proc/$pid/status)" \
        "$(awk '/^nonvoluntary/{print $2}' /proc/$pid/status)"
    sleep 1
done
echo "process $pid is gone"
