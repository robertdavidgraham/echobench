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

	# ethtool -N eth2 rx-flow-hash udp4 sdfn

This works on Intel, but reportedly it won't work for other cards, such as 
the Solarflare 10gbps NIC.

Rerunning the original request to get configuration now looks like this:

	# ethtool -n p2p2 rx-flow-hash udp4
	
	UDP over IPV4 flows use these fields for computing Hash flow key:
	IP SA
	IP DA
	L4 bytes 0 & 1 [TCP/UDP src port]
	L4 bytes 2 & 3 [TCP/UDP dst port]



----------
References
----------

	https://blog.cloudflare.com/how-to-receive-a-million-packets/


