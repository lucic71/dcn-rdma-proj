#!/bin/bash -ex
 
while getopts ":r:a:n:s:" option; do
  case $option in
    r)
      rank="$OPTARG"
      ;;
    n)
      numprocs="$OPTARG"
      ;;
    a)
      addrs="$OPTARG"
      ;;
    s)
      src="$OPTARG"
      ;;
    *)
      echo "Usage: $0 [-r rank] [-n num_procs] [-a addresses] [-s source addr]"
      exit 1
      ;;
  esac
done

if [ $rank -ge $numprocs ]
then
	echo "rank must be in interval [0, $numprocs]"
	exit -1
fi
 
for a in $addrs
do
	touch "/tmp/pipe-$src-$a"
done

for a in $addrs
do
	ip=`echo $a | awk -F':' '{print $1}'`
	r=`echo $a | awk -F':' '{print $2}'`
	pipe_write="/tmp/pipe-$rank-$r"
	pipe_read="/tmp/pipe-$r-$rank"

	if [ $r -eq $rank ]
	then
		continue
	fi

	touch $pipe_write
	touch $pipe_read

	port_server=`echo $src | awk -F'.' '{print $4}'``echo $ip | awk -F'.' '{print $4}'`
	port_client=`echo $ip | awk -F'.' '{print $4}'``echo $src | awk -F'.' '{print $4}'`
	
	(./rdma --dev enp0s3rxe --src_ip $src --dst_ip $ip --port $port_server --server --pipe $pipe_write --datasize `cpp -dD /dev/null | grep __SIZEOF_INT__ | awk -F' ' '{print $3}'` |& tee server.out &)
	(./rdma --dev enp0s3rxe --src_ip $src --dst_ip $ip --port $port_client --pipe $pipe_read --datasize `cpp -dD /dev/null | grep __SIZEOF_INT__ | awk -F' ' '{print $3}'` |& tee client.out &)
	sleep 5
	(./bruck --rank $rank --num_procs $numprocs |& tee bruck.out &)
done