echobench
=========

This is a simple server to demonstrate the FASTEST possible
performance for a server in userspace, using such things
as epoll, kqueue,and IOCompletionPorts. Its purpose is twofold.
The first is as a sample server, from which build new code,
demonstrating the "right" way to do things if scalability
and performance are required. This includes demonstrating such
concepts as IPv6. The second purpose is as a benchmark, to compare
platforms, or to compare with other servers in order to see how
far from the ideal they perform.

The only processing this server does is to echo back the
contents of incoming data, either over TCP or UDP, in
conformance with RFC 862. Because it does virtually nothing,
it is the "ideal" service.


-------------------
NUMA (multi-socket)
-------------------

Today's chips have multiple CPU cores, but in addition, most servers in data
centers support multiple chips (or "sockets") per computer, usually 2 sockets,
sometimes 4, and rarely 8. A common configuration is two sockets, each holding
6 core chips, for a total of 12 cores.

The reason the configuration is popular is that it's "glueless" -- the glue
known as "QPI" is built into all Intel CPUs, so manufacturers don't have to
add any extra chips. The cost for two socket motherboard is about the same
as the cost for a single socket motherboard.

However, the "QPI" links between sockets can become a bottleneck. When packets
arrive on one socket, but are processed on another socket, this will have
a noticeable performance impact.

The features described below can direct incoming traffic to specific sockets
(actually, specific CPUs within a socket). You can direct all traffic to
a single socket, or you can split traffic between sockets. If using multiple
sockets, however, you need NUMA-aware software, which is beyond the scope
of this document.


------------------------------------------------
Interupt mitigation/coalescing/throttling (NAPI)
------------------------------------------------

There is a tradeoff between *latency* and *throughput*. 

To reduce latency, packets will generate an *interrupt* when they arrive.
This causes the packet to be processed immediately, with no wait, potentially
getting below 1-microsecond of response time.

Interrupts are expensive, though. Under high traffic loads (high throutput), 
interrupts will overload
the system. Therefore, the system starts processing multiple packets per
interrupt, increasing latency (because, in effect, it means most packets wait
a little bit before being processed).

Linux has a system called NAPI that automatically switches from one to the
other. Under light loads, it generates an interrupt for each packet, but starts
throttling the interrupts under heavy loads.

The automatic throttling of NAPI is good, but has some problems. It doesn't
start throttling until CPUs are heavily overloaded. Thus, even though it can
receive the packets at high rate, there may not be enough CPU power left over
to process the packet, and it may have to be dropped.

You can monitor this case by looking at statistics. Adapter statistics show
how many packets the adapter drops because they can't be received. Network
stack statistics show how many packets are received, but then later dropped
because they could not be processed. Ideally, you tune the system so that 
the stack can handle all the packets received, and that any overload is dropped
immediately at the adapter.


On Intel adapters, you can use `ethtool` to set a fixed interrupt rate, as
shown below:

# ethtool -C eth1 rx-usecs 275

A 10gbps Ethernet can receive 14,880,000 packets/second. An Intel adapter has
4096 packet descriptors. That means the maximum amount of time the system can
wait between interrupts, and not lose packets, is 275 microseconds.



---------------------------------
Configure hardware receive queues
---------------------------------

This program blasts from a single IP address to a single IP
address. This can be a problem because a common optimization
for NICs is to have multiple received queues hashed by 
IP address. If all packets have the same IP addresses, then
they will go into the same queue, thus becoming single queue
performance.

This benchmark sends from multiple port numbers, so the
quickest fix is to change the hashing algorithm to use
both ports and IP addresses.

The first step is to query the NIC to see what the configuration
is. On my machine (Ubuntu 14.04 LTS) with an Intel NIC, the command 
will look like the following:

	# ethtool -n p2p2 rx-flow-hash udp4
	
	UDP over IPV4 flows use these fields for computing Hash flow key:
	IP SA
	IP DA

This tells me that it's only hashing the IPv4 source/destination addresses.
To change this, I run the command:

	# ethtool -N p2p2 rx-flow-hash udp4 sdfn

This works on Intel, but reportedly it won't work for other cards, such as 
the Solarflare 10gbps NIC.

Rerunning the original request to get configuration now looks like this:

	# ethtool -n p2p2 rx-flow-hash udp4
	
	UDP over IPV4 flows use these fields for computing Hash flow key:
	IP SA
	IP DA
	L4 bytes 0 & 1 [TCP/UDP src port]
	L4 bytes 2 & 3 [TCP/UDP dst port]


---------------------------------
Flow-control
---------------------------------

Get the number of buffers.

	ethtool -g p2p

Increase the number:

	ethtool -G p2p2 rx 4096
	

---------------------------------
Flow-control
---------------------------------

Adapters and switches have flow control. When the receive falls behind
IN THE DRIVER, it sends back messages telling the sender to slow down.
Two devices directly connected, or connected through a switch.

This is good in some respects, but bad in others. Sometimes it will shave
off about 10% to 20% of the performance the system would otherwise achieve.

One interesting effect is how it slows down the transmitter in these tests.
When packet loss is solely a function of the driver, the sender slows down
to the same rate as the receiver. When packet loss happens further up
the stack, such as the UDP layer, it doesn't slow down.

To see what your flow control looks like, use a command like the following:

	# ethtool -a p2p2
	Pause parameters for p2p2:
	Autonegotiate:  on
	RX:             on
	TX:             on

To disable it, run the following:

	# ethtool -A p2p2 autoneg off rx off tx off


---------------------------------
IRQ affinity
---------------------------------
	
	service irqbalance stop

	grep p2p2 /proc/interrupts
	echo 1 >/proc/irq/56/smp_affinity
	echo 2 >/proc/irq/57/smp_affinity
	echo 4 >/proc/irq/58/smp_affinity
	echo 8 >/proc/irq/59/smp_affinity
	echo 1 >/proc/irq/60/smp_affinity
	echo 2 >/proc/irq/61/smp_affinity
	echo 4 >/proc/irq/62/smp_affinity
	echo 8 >/proc/irq/63/smp_affinity


---------------------------------
CPU affinity
---------------------------------

	Use taskset -c <cpus> in order to configure which CPUs to use, preferably
	ones that aren't processing interupts.

	Or, in the code, manually assign CPUs

---------------------------------
Auto-Throttling
---------------------------------

Modern CPUs throttle performance based on changes in power usage and
temperature. Therefore, a slight difference in speed will be detected
as the system changes temperature.

----------
References
----------

	https://blog.cloudflare.com/how-to-receive-a-million-packets/



http://blog.serverfault.com/2011/03/23/performance-tuning-intel-nics/
http://www.intel.com/content/dam/doc/white-paper/improving-network-performance-in-multi-core-systems-paper.pdf