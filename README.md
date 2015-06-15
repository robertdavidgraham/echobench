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
