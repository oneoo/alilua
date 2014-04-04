#!/bin/bash
 
if ! [ -x "$(type -P ab)" ]; then
  echo "ERROR: script requires apache bench"
  echo "For Debian and friends get it with 'apt-get install apache2-utils'"
  echo "If you have it, perhaps you don't have permissions to run it, try 'sudo $(basename $0)'"
  exit 1
fi

runs=$1
site=$2

log=ab.$(echo $site | sed -r 's|https?://||;s|/$||;s|/|_|g;').log
 
if [ -f $log ]; then
  echo removing $log
  rm $log
fi

rm bench.csv
rm bench.csv.tmp

echo "Level Loads Mem qps T min men +- median max" > bench.csv.tmp

for((run=100;run<=$runs;run+=100)); do
  st=$(date +%s)
  ab -r -k -c $run -n 500000 $site >> $log
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
