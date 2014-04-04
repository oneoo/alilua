#!/bin/bash

while((1)); do
now=$(date +%s)
echo -e "$now $(top -n 1|head -1|awk '{print $12}') $(ps aux | grep -v grep | grep -v bash | grep "$1" | awk '{print $6}')" >> /tmp/cpu.infos
sleep 1
done