#!/bin/bash
 
if ! [ -x "$(type -P ab)" ]; then
  echo "ERROR: script requires apache bench"
  exit 1
fi

runs=$1
step=$2
site=$3

log=ab.$(echo $site | sed -r 's|https?://||;s|/$||;s|/|_|g;').log
 
if [ -f $log ]; then
  echo removing $log
  rm $log
fi

rm bench.csv
rm bench.csv.tmp

echo "Level Loads Mem qps T min men +- median max" > bench.csv.tmp

for((run=$step;run<=$runs;run+=$step)); do
  st=$(date +%s)
  ab -r -k -c $run -n 50000 $site >> $log
  se=$(date +%s)

  rm /tmp/a.log;
  for((;st<se;st++)); do
    cat /tmp/cpu.infos | grep $st >> /tmp/a.log;
  done
  cpu=$(cat /tmp/a.log |awk '{a+=$2}END{print a/NR}')
  mem=$(cat /tmp/a.log |awk '{a+=$3}END{print a/NR}')

  echo -e "Concurrency Level: $run $cpu $mem $(grep "^Requests per second" $log | tail -1 | awk '{print$4}') reqs/sec\t $(grep "^Total:" $log | tail -1 | awk '{print$0}')"

  echo -e "$run    $cpu    $mem    $(grep "^Requests per second" $log | tail -1 | awk '{print$4}')    $(grep "^Total:" $log | tail -1 | awk '{print$0}')" >> bench.csv.tmp
  
  #sleep 60
done

cat bench.csv.tmp | sed 's/\s\+/ /g' > bench.csv
echo "see $log for details"
