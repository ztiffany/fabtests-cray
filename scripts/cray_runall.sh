#!/bin/bash
#

usage() {
    ec=$1
    echo "cray_runall.sh [-d <dir>]"
    echo "   -d location of libfabric tests"
    exit $ec
}

if [ -x /opt/slurm/default/bin/srun ]; then
#
# slurm uses a hh:mm:ss format, ask for 1 minute
#
timeout=1:00
launcher="srun -t $timeout"
one_proc_per_node="--ntasks-per-node=1"
else
timeout=60
launcher="aprun -t $timeout"
one_proc_per_node="-N 1"
fi
tests_failed=0
tests_passed=0
testdir=${PWD}
declare -a failed_tests=()

if [ $# -gt 0 ] ; then
  while getopts "d:h" option; do
    case $option in
      d) testdir=$OPTARG;;
      h) usage 0 ;;
      *) usage 1;;
    esac
  done
  shift $(($OPTIND-1))
fi

#
# first run single process tests, fi _info is a special test
#

stests=( fi_ep_test \
         fi_dom_test )

nprocs=1


echo Running fi_info
$launcher -n $nprocs $one_proc_per_node $testdir/fi_info 2>&1 | tee test.output
if [ $? != 0 ] ; then
  $tests_failed++
else
  count=`grep gni test.output | wc -l`
  if [ $count -lt 3 ] ; then
    echo "GNI doesn't appear in fi_info about, aborting..." 
    junk=$((tests_failed++))
    failed_tests=("${failed_tests[@]}" "fi_info")
  else
    junk=$((tests_passed++))
  fi
  rm ./test.output
fi
sleep 1

for test in ${stests[@]} ; do

  echo Running $test
  $launcher -n $nprocs $one_proc_per_node $testdir/$test -f gni
  if [ $? != 0 ] ; then
    junk=$((tests_failed++))
    failed_tests=("${failed_tests[@]}" $test)
  else
    junk=$((tests_passed++))
  fi
  sleep 1
done

echo "Tests passed:"$tests_passed
echo "Tests failed:"$tests_failed
if test $tests_failed -ne 0 ; then
echo "Failing tests are:"
for ftest in ${failed_tests[@]}; do
  echo $ftest
done
fi
