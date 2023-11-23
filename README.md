# DCN RDMA Lab

## Introduction

The guys from Mellanox had a dream:
what if an application can access another application's memory via the network, even without it knowing?
That's how the RDMA protocol was born.
At first, it was exclusive to Infiniband networks, provided by, you guessed, Mellanox.
But soon people got tired of Infiniband, and wanted something cheaper and easier to use.
`RoCE` and `iWARP` were born.

## RDMA Protocol Implementations

As you saw above, the RDMA protocol has many flavours, but all of them are essentially the Infiniband implementation, from which different headers are removed.
The main implementations are:
 * Infiniband
 * RoCE
 * iWARP

### Infiniband

The OG, **the** RDMA implementation.
Usable only on Infiniband networks, provided by Mellanox, now Nvidia.
It looks something like this:

![](/images/ib.avif)

People nowadays use `RoCE`, which is, essentially, `GHR`, `BTH`, and `ETH` slapped onto an Ethernet header.

This is the `GRH`:

![](/images/grh.avif)

This is the `BTH:`

![](/images/bth.jpeg)

This is the `ETH` specific to RDMA, called `RETH`:

![](/images/reth.png)

There is also `AETH`, the header used for ACKs.
You will be able to see it during this lab. 

### RoCE

How about we replace `LRH` (Local Routing Header) with Ethernet? We get RDMA over Ethernet.
Or, how the guys that had this thought first decided to call it, `RDMA over Converged Ethernet`.
Now we can do RDMA in Ethernet networks.
Hooray!
This version is called `RoCEv1`, and there is no reason why someone would use it today.
Think about it:
you have a MAC address and a `GID`.
But routers don't know about `GIDs`, they know about `IPs`, so you can only use `RoCEv1` in L2 networks.
Not good.
So another protocol had to be developed.
Enter RoCEv2.

#### RoCEv2

How about we take it further?
Let's replace `GRH` with `IP` and `UDP`.
We get IPs and ports, things that the routers can actually use to route our packets in the network.
Much better.
We will be ignoring the problems that RoCE has, like the utter chaos that happens when a packet is lost, and the fact that the protocol designers originally thought that `go-back-to-0` was a good ideea.
What is that?
If you lose one packet, you reset everything!
Doesn't matter that some packets reached their destination.
Bless the guys from Microsoft for pushing `go-back-N`.

### iWARP

Now, a protocol not so used, but that exists:
`iWARP`.
Replace `UDP` from `RoCEv2` with `TCP` and you have `iWARP`.
Is it a good idea?
Yes, no more losses that create chaos.
Do people use it?
No.

##### RXE (Soft-RoCE)

Now, people start asking:
"What if I don't want to buy an expensive NIC, that implements one of the protocols from above?".
Someone tought about it, and came up with `SoftRoCE`, which is basically a software implementation of RDMA in the kernel.
That's what we will use today.
But first, what can RDMA do?

## RDMA Operations

If TCP and UDP just carry a payload, that must be interpreted by the protocols above, RDMA specifies what operation is performed, in `BTH`.
There are 3 relevant operations:
 * `send`
 * `read`
 * `write`

There is also a 4th category, `atomics`.
We don't talk about it today.

### Send

The best analogy for a RDMA Send is a normal packet from TCP or UDP:
someone must send it, someone must receive it and intrerpret it.
Nothing else, no writing someone's memory without it knowing it.

### Read

The first interesting one:
the sender requests data from an address, and that data is sent asynchronously, without the receiving application knowing.
In order to do that, the sender must know a remote key, and that data must be at in special memory zone, registered beforehand as available for RDMA operations.
Now a question arises:
can you read as much as you want?
The answer is yes, you can request as much as you want.
The response will be split into multiple packets, depending on the MTU of the RDMA interface.
The packet corresponding to the returned data will be `Read Response First`.
The last will be `Read Response Last`.
Everything else will be `Read Response Middle`.
If the data fits in only one packet, we will have a `Read Response Only`.

#### RDMA MTU

There is a difference between the Ethernet MTU and the RDMA MTU.
If the Ethernet MTU specifies the maxcimum length of a packet that includes headers, the RDMA MTU specifies the maximum length of the payload. 

### Write

The second interesting one.
the sender sends data, that arrive at the receiver, and are written to a registered memory address, with or without the knowledge of the receiver.
Can you write as much as you want?
Yes.
Same thing as in the case of `read`.

## `ibverbs`

Theory is nice and all, but how do we do that?
Using what is commonly knows as `verbs`.
There is a library that allows an application to use RDMA, without knowing which RDMA protocol is implemented by the NIC:
`ibverbs`.
How do the operations from above translate to `verbs`?

### ibverbs Send

Let's start with the easier one to understand:
`send`.
In order for an application to send data, using the `send` operation, the following need to happen:
 * a RDMA device must be active and open
 * a Protection Domain (PD) must be allocated
 * a Queue Pair must be created;
 this queue pair contains 2 Completion Queues;
 one for sending packets, one for receiveing
 * a Memory Region (MR) must be allocated;
 that region can have multiple permissions:
 `local write` (the app that allocates it can write to it), `remote read` (a remote application ca read it) and `remote write`.
 `local read` is always there.
 * a Work Request (WR) must be created and posted;
 a Work Request can contain multiple Scatter-Gather Entries (SGE);
 each SGE specifies a local memory address, a length and a local access key.

The receiver must also have the device, PD, QP and MR allocated.
When a RDMA Send is received, a Work Completion (WC) structure will be added to the CQ of the receiver.
The receiver must poll and empty the CQ.
The WC specifies if the data was sent correctly, if there is any Immediate Data, among other things.

Immediate Data?
What is that?
Some RDMA operations can add a new header, `ImmData`, to the packet, that contains raw data.
Any operation that has a `ImmData` header will generate a `WC` at the receiver, except `Send`, which will always generate a `WC`, and `Read`, which will never generate a `WC` at the receiver.
`Read` also doesn't accept `ImmData`.
So it is usefull only for the receiver knowing when a `Write` operation has finished.

### ibverbs Write

The sender must have everything needed to perform a `Send` operation, with a twist:
the WR structure must also specify the remote memory address and the access key of that address.

In the case of the receiver, a WC will be generated only if the `Write` has Immediate Data in it.
If not, the receiver won't know that a `Write` was performed, unless it is notified in another way.

### ibverbs Read

For the sender, it is the same as the `Write`.
Things are different for the receiver.
Unless it is is notified another way, the receiver won't be notified if a `Read` is performed on its memory.

## Tasks

### 1: Lab Setup

In this lab you will use 2 virtual machines, that will communicate with eachother.
A virtual mahine with all the needed packages is provided here [](https://drive.google.com/file/d/1eT5yU0NGJ8sP47VY_HkwW-JRu3LFbB3Z/view?usp=sharing).
Make sure that the virtual machines can ping eachother.

For the lab to work, the virtual machines must be on a Bridged Network.
If it doesn't work for you (looking at you, VMWare), try another hypervisor.

You can also do the lab on your native Linux, but you must find another person that wants to do the same thing, so you can speak RDMA to eachother.
Or, if you are a networking god, you can use only one VM an pair with another fellow divine.

### 2: Create a RXE Interface

Use the following command to create a SoftRoCE (RXE) interface, replacing <netdev> with your network device name.
```bash
sudo rdma link add <netdev>rxe type rxe netdev <netdev>
```

### 2: Inspect The Interface

There are a few commands to inspect a RDMA interface.
First, you can use `rdma link show` and `ibv_devices` to see if your interface is there.

Then, use `ibv_devinfo -v` to show details about the RDMA devices present on your system.
You will see a lot of output.
The important part is at the end: the description of the ports.
Your interface has only one port, so you should see something like this:

![](/images/ibv-devinfo.png)

Some things are important here: `state`, `active_mtu`, and the GID table.
In the image you have an interface that uses both RoCEv1 and RoCEv2, so it will have 2 GID entries for each protocol.
Generally, a RoCEv2 entry will corespond to an IP address assigned to the network interface to which the RDMA device is linked.

Use `ip a s` to display details about the network interfaces that your system uses.
Observe the connection between GID entries and IP addresses.
For RoCEv2, the GID entry will be either the IPv6 address, or `::ffff:<IPv4 address>`.
Remember the index of the GID entry for the IPv4 address;
you will need it later.

### 3: Do Some RDMA

Now, let's generate some RDMA traffic, using some standard tools: `ib_write_bw` and `ibv_rc_pingpong`

#### 3.1: `ibv_rc_pingpong`

`ibv_rc_pingpong` will do a simple ping back and forth, to test the connectivity.

On one system, run:
```bash
ibv_rc_pingpong -d <rxe_interface> -g <gid_index>
```
Notice you need a GID index.
Use the one for the IPv4 address.

On the second system, run:
```bash
ibv_rc_pingpong -d <rxe_interface> -g <gid_index> <ip_of_first_system>
```

#### 3.2: `ib_write_bw`

`ib_write_bw` will measure the bandwidth of a RDMA connection, for write operations.

On one system, run:
```bash
ib_write_bw -d <rxe_interface> -x <gid_index>
```

On the other, run:
```
ib_write_bw -d <rxe_interface> -x <gid_index> <ip_of_first_system>
```

There also other tools, like `ib_write_lat`, `ib_read_bw`, `ib_read_lat`, `ib_send_bw`, `ib_send_lat`.
The `_lat` tools measure the latency of one operation.

### 4: Dump Some RDMA Traffic

Normally, intercepting RDMA traffic is a pain.
But, because we use SoftRoCE, all the packets go through the Linux kernel, and `tcpdump` can see them.
Use `tcpdump` to dump the traffic, while you use one of the tools from above.
Use `Wireshark` to inspect the capture.

### 5: RDMA Interface Statistics

Sometimes stuff doesn't work, and no one knows why.
That's why there are hardware counters available, to shed some light.
Usually, you can find them in `/sys/class/infiniband<rdma_dev>/ports/1/hw_counters/`.
Some drivers also provide additional drivers in `/sys/class/infiniband<rdma_dev>/ports/1/counters/`, but that's not our case.
List those counters and try to find what they mean.

### 6: Write A RDMA Application

Ok, enough using other people's applications.
Time to get your hands dirty, and write an application that does RDMA.
To do that, you must use the `ibverbs` library.
The VMs already have it installed.

#### 6.1: Setup the Connection

In order for any 2 applications to speak RDMA to eachother, a few things must happen:
 * each application must open a RDMA device
 * each application must create one or more QPs (Queue Pairs)
 * each application must register the memory it's going to use for RDMA operations
 * the 2 applications must exchange at least the folloiwng things: the numbers of the used QPs, the GID, the addresses of the registered memory and the remote access keys of that memory

You have to do just that.
Follow the comments in `ibverbs/main.cc`.
If you get stuck anywhere, the reference implementation is in `ibverbs-sol`.
And google (especially rdmamojo) is your friend for this one.
Oh, and one more thing:
the RDMA drivers really hate it when you don't free the resources you use

#### 6.2: Do a Send

Now that all the structures are set up, you can do a RDMA Send.
As before, follow the comments.
If you feel adventurous, do a Send With Immediate.

#### 6.3: Do a Write

Now do a RDMA Write.
You know the drill.
