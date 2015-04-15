#!/bin/bash
#
# Copyright (c) 2015 Cray Inc.  All rights reserved.
# Copyright (c) 2015 Los Alamos National Security, LLC. All rights reserved.
#
# This software is available to you under a choice of one of two
# licenses.  You may choose to be licensed under the terms of the GNU
# General Public License (GPL) Version 2, available from the file
# COPYING in the main directory of this source tree, or the
# BSD license below:
#
#     Redistribution and use in source and binary forms, with or
#     without modification, are permitted provided that the following
#     conditions are met:
#
#      - Redistributions of source code must retain the above
#        copyright notice, this list of conditions and the following
#        disclaimer.
#
#      - Redistributions in binary form must reproduce the above
#        copyright notice, this list of conditions and the following
#        disclaimer in the documentation and/or other materials
#        provided with the distribution.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AWV
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
# BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
# ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#

#
# globals:
#   provider to use for client/server "simple" tests
cs_provider="sockets"
script_path=$(dirname "$0")
run_client_server=$script_path/run_client_server.py
expected_failures=$script_path/cray_runall_expected_failures
intermittent_failures=$script_path/cray_runall_intermittent_failures

usage() {
    ec=$1
    echo "cray_runall.sh [-d <dir>]"
    echo "   -d location of libfabric tests"
    exit $ec
}

my_exit() {
    # check for expected or intermittent failures
    expected=`awk '{ if ($1!="#") print $0 }' < $expected_failures`
    intermittent=`awk '{ if ($1!="#") print $0 }' < $intermittent_failures`
    # replace control characters with spaces
    expected=${expected//[[:cntrl:]]/ }
    intermittent=${intermittent//[[:cntrl:]]/ }
    echo "==="
    echo "Total tests run: "$total_tests
    echo "Tests passed: "$tests_passed
    echo "Tests failed: "$tests_failed
    if test $tests_failed -ne 0 ; then
	echo "Failing tests are:"
	for ftest in ${failed_tests[@]}; do
	    expr="$ftest[[:blank:]]"
	    lexpr="$ftest$"
	    if [[ "$expected" =~ $expr ]]; then
		echo "* $ftest (EXPECTED)"
		junk=$((tests_failed--))
		expected=${expected//$expr/}
	    elif [[ "$expected" =~ $lexpr ]]; then
		echo "* $ftest (EXPECTED)"
		junk=$((tests_failed--))
		expected=${expected%%$ftest}
	    elif [[ "$intermittent" =~ $expr ]] || [[ "$intermittent" =~ $lexpr ]]; then
		echo "* $ftest (INTERMITTENT)"
		junk=$((tests_failed--))
	    else
		echo "* $ftest"
	    fi
	done
    fi

    unexpected=${expected//[[:blank:]]/}
    if [[ "$unexpected" != "" ]]; then
	echo "The following tests were expected to fail, but passed:"
	for t in $expected; do
	    echo "* $t"
	    junk=$((tests_failed++))
	done
    fi

    exit $tests_failed
}


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

#
# Check for srun or aprun
#
srun=`command -v srun`
if [ $? == 0 ]; then
    launcher="srun"
else
    aprun=`command -v aprun`
    if [ $? == 0 ]; then
        launcher="aprun"
    else
        echo "Cannot find a supported job launcher (srun, aprun).  Please load the appropriate module"
        exit -1
    fi
fi

tmp_outfile="test.$$.out"
echo Running fi_info
$run_client_server $testdir/fi_info -f gni --launcher $launcher --no-server 2>&1 > $tmp_outfile
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
         fi_av_test2 \
         fi_dom_test \
         fi_multi_dom_test )
num_stests=${#stests[@]}

total_tests=$((total_tests+$num_stests))

for test in ${stests[@]} ; do

  echo Running $test
  $run_client_server $testdir/$test -f gni --launcher $launcher --no-server
  if [ $? != 0 ] ; then
    junk=$((tests_failed++))
    failed_tests=("${failed_tests[@]}" $test)
  else
    junk=$((tests_passed++))
  fi
  sleep 1
done

# Is there a way to have configure get all thes names for us?
cs_tests=(fi_msg\
          fi_msg_pingpong \
          fi_msg_rma \
          fi_rdm \
          fi_rdm_rma_simple \
          fi_dgram \
          fi_dgram_waitset \
          fi_rdm_pingpong \
          fi_rdm_tagged_pingpong \
          fi_rdm_tagged_search \
          fi_rdm_cntr_pingpong \
          fi_rdm_rma \
          fi_rdm_atomic \
          fi_ud_pingpong \
          fi_cq_data \
          fi_poll \
          fi_rdm_inject_pingpong \
          fi_rdm_multi_recv \
          fi_scalable_ep \
          fi_rdm_shared_ctx )

num_cs_tests=${#cs_tests[@]}

total_tests=$((total_tests+$num_cs_tests))

for test in ${cs_tests[@]} ; do

  echo Running $test using provider $cs_provider
  $run_client_server $testdir/$test -f $cs_provider --launcher $launcher
  if [ $? != 0 ] ; then
    junk=$((tests_failed++))
    failed_tests=("${failed_tests[@]}" $test)
  else
    junk=$((tests_passed++))
  fi
  sleep 1

done

# handle special case of complex/fabtest

total_tests=$((total_tests+1))
echo Running test fabtest
$run_client_server $testdir/fabtest -f $cs_provider --launcher $launcher --server-args="-x" --client-args="-x" -t 120
if [ $? != 0 ] ; then
  junk=$((tests_failed++))
  failed_tests=("${failed_tests[@]}" "fabtest")
else
  junk=$((tests_passed++))
fi



my_exit
