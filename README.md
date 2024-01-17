# DCN RDMA Proj

## Summary

The standard implementation of MPI_Alltoall in MPI libraries (e.g., MPICH,
Open-MPI) uses a combination of techniques, such as the pairwise and Bruck
algorithms. The pairwise algorithm uses a linear number iterations, in process
count P, while the Bruck algorithm is logarithmic.

This project implements both algoritms using RDMA communication.

## Setup

For testing and collecting algorithm metrics I used the virtual machines
provided [here](https://drive.google.com/file/d/1eT5yU0NGJ8sP47VY_HkwW-JRu3LFbB3Z/view?usp=sharing).

They were started under VirtualBox and connected through the bridge interface.

## Architecture

The start.sh script should be run with the correct parameters on each VM. The
script does the following operations:
  * creates FIFOs for communicating between the algorithm process and the RDMA
    processes
  * computes the ports for exchanging the RDMA information
  * starts the RDMA processes
  * starts the algorithm process

To communicate with a remote VM, I use a server-rdma process that only receives
data from the algorithm process through a FIFO and sends it through RDMA to a
client connected on the remote machine and I use a client-rdma process that only
receives data through RDMA and sends it through a FIFO to the algorithm process.

For both algorithms, the communication is abstracted through the rread and
rwrite interface that calls read or write on the correct FIFO.

## Algorithms and implementation

Both algoritms are described
[here](https://tcpp.cs.gsu.edu/curriculum/?q=system/files/1786_A%20Visual%20Guide%20to%20MPI%20All-to-all.pdf)
in II.A. Bruck Algorithm and II.B. Spread-out Algorithm. In this paper the
pairwise algorithm is found as the spread-out algorithm.

The pairwise algorithm uses P rounds of communication to send data to all peers.
At each communication round it sends only a cell of data to a peer. A cell of
data is defined as a slice of the array that should be shared with other peers,
see Fig. 3 of the above cited paper, each slice is colored differently. However
each slices can contain multiple entries, as seen in the same figure. So at each
step of communication the pairwise alorithm sends entries_per_cell *
bytes_per_entry bytes.

The bruck algorithm uses log(P) rounds of communication. At each communication
round it sends multiple cells of data to its peer. The number of cells that it
sends is equal to P/2, the number of bytes that it sends is P/2 *
entries_per_cell * bytes_per_entry.

However, my implementation of RDMA server and client only knows to craft RDMA
messages of size entries_per_cell * bytes_per_entry. This means that for a
single round of communication pairwise will generate 1 RDMA message while bruck
will generate P/2 RDMA messages. I implemented the communication this way to
simlify the protocol.

## Measurements

In total, pairwise creates (P-1) messages while bruck creates P/2 * log(P)
messages.

Using `rdma statistic` I observed that for each RDMA message that is sent from a
VM, 2 RDMA packets are created, e.g. these are the statistics generated for a
full bruck with P=4, entries_per_cell=1, bytes_per_entry=4:
```
link enp0s3rxe/1 sent_pkts 8 rcvd_pkts 8 duplicate_request 0 out_of_seq_request 0 rcvd_rnr_err 0 send_rnr_err 0 rcvd_seq_err 0 ack_deferred 0 retry_exceeded_err 0 retry_rnr_exceeded_err 0 completer_retry_err 0 send_err 0 link_downed 0 rdma_sends 0 rdma_recvs 4
```

In this case, the total number of generated packets for brucks is P * log(P)
and for pairwise is 2 * (P - 1).

Because of this pairwise has a great advantage because it creates less packets
even though it takes more communicaiton rounds to finish.
