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
# Helper script to assist in running the fabtest
# client/server tests in the slurm environment.
# The script is intended to be used with both client
# and server running on the same node.
#

usage() {
    ec=$1
    echo "cray_run_client_server.sh -t <test> [-f <fabric>] [-c] [-a <app args>]"
    echo "   -t name_of_client_server_test"
    echo "   -f fabric"
    echo "   -c run test as client if set"
    exit $ec
}

if [ $# -lt 2 ] ; then
usage 1
fi

run_client=0
fabric="sockets"
if [ $# -gt 0 ] ; then
  while getopts "t:f:ca:" option; do
    case $option in
      t) testprog=$OPTARG;;
      f) fabric=$OPTARG;;
      c) run_client=1;;
      a) app_args=$OPTARG;;
      *) usage 1;;
    esac
  done
  shift $(($OPTIND-1))
fi

gni_ip_addr=`/sbin/ifconfig ipogif0 | grep 'inet addr:' | cut -d: -f2 | awk '{ print $1}'`
if [ $run_client -eq 1 ] ; then
$testprog -f $fabric $app_args $gni_ip_addr
else
$testprog -f $fabric $app_args
fi



