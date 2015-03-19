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

#
# srun specific function to run the client/server tests
# in simpledirectory.  This function assumes the client and
# server are run on the same node.
#

run_client_server_srun() {
    local prog_name=$1
    local fabric=$2
    echo Number of args to run_client_server_srun is $#
    if [ $# -gt 2 ]; then
        local server_args=$3
    fi
    if [ $# -gt 3 ]; then
        local client_args=$4
    fi
    local gni_ip_addr=`/sbin/ifconfig ipogif0 | grep 'inet addr:' | cut -d: -f2 | awk '{ print $1}'`
    if [ -f mpmd_conf_file ] ; then
        rm mpmd_conf_file
    fi

    if [ -z $server_args ]; then
        echo  "0  run_client_server.sh -t $prog_name -f $fabric">> mpmd_conf_file
    else
        echo server_args=$server_args
        echo  "0  run_client_server.sh -t $prog_name -f $fabric -a $server_args">> mpmd_conf_file
    fi

    if [ -z $client_args ]; then
        echo  "1  run_client_server.sh -t $prog_name -f $fabric -c ">> mpmd_conf_file
    else
        echo client_args=$client_args
        echo  "1  run_client_server.sh -t $prog_name -f $fabric -c -a $client_args">> mpmd_conf_file
    fi

#   cat mpmd_conf_file
#
# the exclusive option seems necessary for MPMD
#
    $launcher --multi-prog --exclusive -N 1 -t $timeout mpmd_conf_file
    return $?
}

#
# the aprun method assume one is running the script
# in a batch environment, so the first invocation of
# aprun will put a rank on the same node as for the
# second invocation.
#

run_client_server_aprun() {
    local prog_name=$1
    local gni_ip_addr=`aprun -n 1 /sbin/ifconfig ipogif0 | grep 'inet addr:' | cut -d: -f2 | awk '{ print $1}'`
    echo  "0: $prog_name" >> mpmd_conf_file
    echo  "1: $prog_name $gni_ip_addr" >> mpmd_conf_file
    $launcher -n 1 $prog_name : -n 1 $ prog_name $gni_ip_addr
    return $?
}

if [ -x /opt/slurm/default/bin/srun ]; then
#
# slurm uses a hh:mm:ss format, ask for 1 minute
#
using_alps=0
timeout=1:00
launcher="srun -t $timeout"
one_proc_per_node="--ntasks-per-node=1"
else
using_alps=1
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
         fi_av_test2 \
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
  run_client_server_srun $test $cs_provider
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
run_client_server_srun fabtest $cs_provider -x -x
if [ $? != 0 ] ; then
  junk=$((tests_failed++))
  failed_tests=("${failed_tests[@]}" "fabtest")
else
  junk=$((tests_passed++))
fi



my_exit
