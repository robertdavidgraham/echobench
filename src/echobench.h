#ifndef ECHOBENCH_H
#define ECHOBENCH_H
#include <stdio.h>

struct Configuration
{
    const char *target;
    unsigned port;
    unsigned thread_count;
    double rate;
    int is_mmsg;
};

struct ThreadData
{
    //const char *target;
    size_t total_packets;
    int fd;
    struct addrinfo *ai;
    double rate;
    unsigned port;
};


#endif

