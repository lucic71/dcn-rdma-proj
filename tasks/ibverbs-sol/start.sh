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

entries_per_cell=10
 
for a in $addrs
do
	ip=`echo $a | awk -F':' '{print $1}'`
	r=`echo $a | awk -F':' '{print $2}'`
	pipe_read="/tmp/pipe-$rank-$r"
	pipe_write="/tmp/pipe-$r-$rank"

	if [ $r -eq $rank ]
	then
		continue
	fi

	mkfifo $pipe_write
	mkfifo $pipe_read

	port_server=`echo $src | awk -F'.' '{print $4}'``echo $ip | awk -F'.' '{print $4}'`
	port_client=`echo $ip | awk -F'.' '{print $4}'``echo $src | awk -F'.' '{print $4}'`
	
	(./rdma --dev enp0s3rxe --src_ip $src --dst_ip $ip --port $port_server --server --pipe $pipe_read --datasize $((`cpp -dD /dev/null | grep __SIZEOF_INT__ | awk -F' ' '{print $3}'`*$entries_per_cell)) |& tee server.out &)
	(./rdma --dev enp0s3rxe --src_ip $src --dst_ip $ip --port $port_client --pipe $pipe_write --datasize $((`cpp -dD /dev/null | grep __SIZEOF_INT__ | awk -F' ' '{print $3}'`*$entries_per_cell)) |& tee client.out &)
	sleep 5
	(./bruck --rank $rank --num_procs $numprocs --entries_per_cell $entries_per_cell |& tee bruck.out &)
done
