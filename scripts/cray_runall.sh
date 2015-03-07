#!/bin/bash
#

usage() {
    ec=$1
    echo "cray_runall.sh [-d <dir>]"
    echo "   -d location of libfabric tests"
    exit $ec
}

my_exit() {
    echo "==="
    echo "Total tests run:"$total_tests
    echo "Tests passed:"$tests_passed
    echo "Tests failed:"$tests_failed
    if test $tests_failed -ne 0 ; then
	echo "Failing tests are:"
	for ftest in ${failed_tests[@]}; do
	    echo $ftest
	done
    fi
    exit $tests_failed
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

nprocs=1
total_tests=1

tmp_outfile="test.$$.out"
echo Running fi_info
$launcher -n $nprocs $one_proc_per_node $testdir/fi_info 2>&1 > $tmp_outfile
if [ $? != 0 ]; then
    echo "fi_info failed, aborting..."
    junk=$((tests_failed++))
    failed_tests=("${failed_tests[@]}" "fi_info")
else
  count=`grep gni $tmp_outfile | wc -l`
  if [ $count -lt 3 ] ; then
    echo "GNI doesn't appear in fi_info about, aborting..." 
    junk=$((tests_failed++))
    failed_tests=("${failed_tests[@]}" "fi_info")
  else
    junk=$((tests_passed++))
    cat $tmp_outfile
  fi
fi
rm $tmp_outfile
if test $tests_failed -ne 0 ; then
    # exit if fi_info fails, since we can't rely on anything else
    my_exit
fi
sleep 1

stests=( fi_ep_test \
         fi_dom_test )
num_stests=${#stests[@]}

total_tests=$((total_tests+$num_stests))

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

my_exit
