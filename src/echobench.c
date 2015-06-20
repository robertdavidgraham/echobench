#define _CRT_SECURE_NO_WARNINGS
#include "echobench.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "pixie-threads.h"
#include "pixie-timer.h"
#include "main-throttle.h"

#if defined(WIN32)
#include <WinSock2.h>
#include <WS2tcpip.h>
#define snprintf _snprintf
#define WSA(err) WSA##err
#if defined(_MSC_VER)
#pragma comment(lib, "ws2_32.lib")
#endif
typedef int socklen_t;
#else
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#define WSAGetLastError() (errno)
#define SOCKET int
#define WSA(err) (err)
#endif


/******************************************************************************
 ******************************************************************************/
static const char *
my_inet_ntop(struct sockaddr *sa, char *dst, size_t sizeof_dst)
{
#if defined(WIN32)
    /* WinXP doesn't have 'inet_ntop()', but it does have another WinSock
     * function that takes care of this for us */
    {
        DWORD len = (DWORD)sizeof_dst;
        WSAAddressToStringA(sa, sizeof(struct sockaddr_in6), NULL,
                            dst, &len);
    }
#else
    switch (sa->sa_family) {
    case AF_INET:
        inet_ntop(AF_INET, &(((struct sockaddr_in *)sa)->sin_addr),
                dst, sizeof_dst);
        break;
    case AF_INET6:
        inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)sa)->sin6_addr),
                dst, sizeof_dst);
        break;
    default:
        dst[0] = '\0';
    }
#endif
    return dst;
}


/******************************************************************************
 ******************************************************************************/
void
client_thread(void *v)
{
    struct ThreadData *t = (struct ThreadData *)v;
    struct addrinfo *target = t->ai;
    int fd;
    static const char test[] = "this is a test";
    struct Throttler throttler[1];

    memset(throttler, 0, sizeof(throttler[0]));
    throttler_start(throttler, t->rate);

    fd = socket(target->ai_family, SOCK_DGRAM, IPPROTO_UDP);
    if (fd <= 0) {
        fprintf(stderr, "FAIL: couldn't create socket %u\n", WSAGetLastError());
        exit(1);
    }

    if (target->ai_family == AF_INET) {
        struct sockaddr_in sin;
        int x;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        x = bind(fd, (struct sockaddr*)&sin, sizeof(sin));
        if (x)
            printf("bind: %u\n", WSAGetLastError());
    }
    
    for (;;) {
        uint64_t count;
        uint64_t i;

        count =  throttler_next_batch(throttler, t->total_packets);

        for (i=0; i<count; i++) {
            int x;

            x = sendto(fd, test, sizeof(test), 0, target->ai_addr, target->ai_addrlen);
            if (x == -1) {
                char hostname[256];
                int err = WSAGetLastError();

#ifdef WIN32
                /* Once we hit the max rate for sending packets, Windows starts
                 * returning this as error codes */
                if (err == WSAEINVAL)
                    continue;
#endif
                my_inet_ntop(target->ai_addr, hostname, sizeof(hostname));
                printf("%s:%u\n", hostname, err);
                //select(1, 0, writefs, 0, 0);
            } else
                t->total_packets++;
        }

    }
}


/******************************************************************************
 ******************************************************************************/
void
bench_client(struct Configuration *cfg)
{
    unsigned i;
    struct ThreadData threaddata[64];
    int x;
    char szport[16];
    struct addrinfo *targets = 0;
    struct addrinfo hints;
    
    /* 'cause getaddrinfo() needs string */
    snprintf(szport, sizeof(szport), "%u", cfg->port);
    
    memset(&threaddata, 0, sizeof(threaddata));
    
    /*
     * Resolve target name
     */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    hints.ai_flags = AI_ADDRCONFIG;
    x = getaddrinfo(cfg->target, szport, 0, &targets);
    if (x != 0 || targets == 0) {
        fprintf(stderr, "%s: %s\n", cfg->target, gai_strerror(x));
        exit(1);
    }
    
    /* log the actual IP address we are using */
    {
        char hostname[256];
        const char *result;
        struct sockaddr_in *sin = (struct sockaddr_in*)targets->ai_addr;
        const char *type = "(unknown)";
        
        
        result = my_inet_ntop(targets->ai_addr, hostname, sizeof(hostname));
        
        printf("target: %s\n", result);

        
    }
    
    /*
     * Create all threads
     */
    for (i=0; i<cfg->thread_count; i++) {
        struct ThreadData *t = &threaddata[i];
        t->ai = targets;        
        t->rate = cfg->rate/(1.0 * cfg->thread_count);
        pixie_begin_thread(client_thread, 0, t);
    }
    
    {
        uint64_t last_count = 0;
        uint64_t last_time = pixie_nanotime();
        double last_rates[8] = {0};
        unsigned index = 0;
        int has_started = 0;

        for (;;) {
            unsigned i;
            uint64_t current_count = 0;
            uint64_t current_time;
            double current_rate;
            double rate;            

            pixie_mssleep(1000);

            for (i=0; i<cfg->thread_count; i++) {
                current_count += threaddata[i].total_packets;
            }

            current_time = pixie_nanotime();
            if (current_time == last_time)
                continue;


            current_rate = 1000000000.0*(current_count - last_count)/(current_time - last_time);
            last_rates[index] = current_rate;
            index = (index + 1) % 8;

            if (!has_started) {
                for (i=0; i<8; i++)
                    last_rates[i] = current_rate;
                has_started = 1;
            }

            rate = 0;
            for (i=0; i<8; i++) {
                rate += last_rates[i];
            }
            rate /= 8;
            
            printf("%10.1f pps\n", rate);
            
            last_count = current_count;
            last_time = current_time;
        }
    }
    
}

extern void bench_server(struct Configuration *cfg);

/******************************************************************************
 ******************************************************************************/
int main(int argc, char *argv[])
{
    struct Configuration cfg[1];
    int i;
    
    /*
     * Legacy Windows is legacy.
     */
#if defined(WIN32)
    {WSADATA x; WSAStartup(0x201, &x);}
#endif

    memset(cfg, 0, sizeof(cfg[0]));
    
    cfg->port = 12345;
    cfg->thread_count = pixie_cpu_get_count();
    cfg->rate = 100.0;


    if (argc <= 1) {
        fprintf(stderr, "--- echo benchmark ---\n" "usage:\n");
        fprintf(stderr, " echobench server [-n #cpus] [-p port]\n");
        fprintf(stderr, " echobench client <ip-addr> [-p port]\n");
        return -1;
    }
    
    for (i=2; i<argc; i++) {
        if (argv[i][0] == '-') {
            switch (argv[i][1]) {
                case 'n':
                    if (argv[i][2])
                        cfg->thread_count = strtoul(argv[i]+2,0,0);
                    else if (i+1>argc)
                        fprintf(stderr, "expected number after '%s'\n", argv[i]);
                    else
                        cfg->thread_count = strtoul(argv[++i],0,0);
                    break;
                case 'p':
                    if (argv[i][2])
                        cfg->port = strtoul(argv[i]+2,0,0);
                    else if (i+1>argc)
                        fprintf(stderr, "expected number after '%s'\n", argv[i]);
                    else
                        cfg->port = strtoul(argv[++i],0,0);
                    break;
                case 'r':
                    if (argv[i][2])
                        cfg->rate = strtoul(argv[i]+2,0,0);
                    else if (i+1>argc)
                        fprintf(stderr, "expected number after '%s'\n", argv[i]);
                    else
                        cfg->rate = strtoul(argv[++i],0,0);
                    break;
                case 'R':
                    cfg->is_reuseport = 1;
                    break;
                case 'M':
                    cfg->is_mmsg = 1;
                    break;
                default:
                    fprintf(stderr, "unknown option: '%s'\n", argv[i]);
            }
        } else {
            cfg->target = argv[i];
        }
    }
    if (cfg->thread_count > 64)
        cfg->thread_count = 64;
    
    
    if (strcmp(argv[1], "server") == 0 || memcmp(argv[1], "receiv", 5) == 0) {
        bench_server(cfg);
    } else if (strcmp(argv[1], "client") == 0 || memcmp(argv[1], "transm", 6) == 0) {
        if (cfg->target == NULL) {
            fprintf(stderr, "must specify target ip\n");
            return -1;
        }
        bench_client(cfg);
    } else {
        fprintf(stderr, "first parm should be 'client' or 'server'\n");
        fprintf(stderr, "type -h for help \n");
        exit(1);
    }
    return 0;
}






