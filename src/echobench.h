#ifndef ECHOBENCH_H
#define ECHOBENCH_H
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

struct Configuration
{
    const char *target;
    unsigned port;
    unsigned thread_count;
    double rate;
    unsigned is_mmsg;
    unsigned is_reuseport; 
};

struct ThreadData
{
    //const char *target;
    size_t total_packets;
    ptrdiff_t fd;
    struct addrinfo *ai;
    double rate;
    unsigned port;
};


#endif

