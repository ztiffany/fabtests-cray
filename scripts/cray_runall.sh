#!/bin/sh
alps_present=`which alps`
if [ $? == 0 ]; then
launcher="aprun -n 1"
else
launcher="srun -n 1"
fi
echo Running fi_info
$launcher ./fi_info 2>&1 | tee test.output
if [ $? != 0 ] ; then
exit -1
fi
count=`grep gni test.output | wc -l`
if [ $count != 3 ] ; then
echo "GNI doesn't appear in fi_info about, aborting..." 
exit -1
fi
rm ./test.output
sleep 1
echo Running fi_ep_test
$launcher ./fi_ep_test -f gni
if [ $? != 0 ] ; then
exit -1
fi
sleep 1
echo Running fi_dom_test
$launcher ./fi_dom_test -f gni -n 4
if [ $? != 0 ] ; then
exit -1
fi
sleep 1
# fi_av_test doesn't work yet
#echo Running fi_av_test
#$launcher ./fi_av_test 
#if [ $? != 0 ] ; then
#exit -1
#fi
echo ...done 
