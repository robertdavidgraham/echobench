#define _GNU_SOURCE
#include "echobench.h"
#include "pixie-threads.h"
#include "pixie-timer.h"

#include <stdlib.h>
#include <errno.h>
#include <string.h>

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
SOCKET
create_server_socket(unsigned port, unsigned is_reuseport)
{
    int err;
    SOCKET fd;
    struct sockaddr_in6 sin;
    
    
    /*
     * Create a socket for incoming UDP packets. By specifying IPv6, we are
     * actually going to allow both IPv4 and IPv6.
     */
    fd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (fd <= 0) {
        fprintf(stderr, "FAIL: couldn't create socket %u\n", WSAGetLastError());
        exit(1);
    }
    

    /*
     * Set the 'reuse' feature of the socket, otherwise restarting the process
     * requires a wait before binding back to the same port number
     */
#ifdef SO_REUSEADDR
    {
        int on = 1;
        err = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&on,sizeof(on));
        if (err < 0) {
            perror("setsockopt(SO_REUSEADDR) failed");
            exit(1); 
        }
    }
#endif
    
#ifdef SO_REUSEPORT
    if (is_reuseport) {
        int on = 1;
        err = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (char *)&on,sizeof(on));
        if (err < 0) {
            perror("setsockopt(SO_REUSEPORT) failed");
            exit(1); 
        }
    }
#endif
    
    /*
     * Enable both IPv4 and IPv6 to be used on the same sockets. This appears to
     * be needed for Windows, but not needed for Mac OS X.
     */
#ifdef IPV6_V6ONLY
    {
        int on = 0;
        err = setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&on, sizeof(on)); 
        if (err < 0) {
            perror("setsockopt(IPV6_V6ONLY) failed");
            exit(1); 
        }
    }
#endif
    



    /*
     * Listen on any IPv4 or IPv6 address in the system
     */
    memset(&sin, 0, sizeof(sin));
    sin.sin6_family = AF_INET6;
    sin.sin6_addr = in6addr_any;
    sin.sin6_port = htons(port);
    err = bind(fd, (struct sockaddr*)&sin, sizeof(sin));
    if (err) {
        switch (WSAGetLastError()) {
            case WSA(EACCES):
                fprintf(stderr, "FAIL: couldn't bind to port %u: %s\n", port, 
                    "access denied");
                if (port <= 1024)
                    fprintf(stderr, "  hint... need to be root for ports below 1024\n");
                break;
            case WSA(EADDRINUSE):
                fprintf(stderr, "FAIL: couldn't bind to port %u: %s\n", port, 
                    "address in use");
                fprintf(stderr, "  hint... some other server is running on that port\n");
                break;
            default:
                fprintf(stderr, "FAIL: couldn't bind to port %u: %u\n", port,
                    WSAGetLastError());
        }
        exit(1);
    } else {
        fprintf(stderr, "UDP port: %u\n", port);
    }
    
    return fd;
}

/******************************************************************************
 ******************************************************************************/
void
server_thread(void *v)
{
    struct ThreadData *thread = (struct ThreadData *)v;
    SOCKET fd = thread->fd;
    
    /*
     * Sit in loop processing incoming UDP packets
     */
    for (;;) {
        struct sockaddr_in6 sin;
        char buf[2048];
        int bytes_received;
        socklen_t sizeof_sin = sizeof(sin);
        
        bytes_received = recvfrom(fd, 
                                  buf, sizeof(buf),
                                  0, 
                                  (struct sockaddr*)&sin, &sizeof_sin);
        if (bytes_received <= 0)
            continue;
        
        thread->total_packets++;

        //sendto(fd, buf, bytes_received, 0, (struct sockaddr*)&sin, sizeof_sin);
    }

    fprintf(stderr, "end thread\n");
}

#define VLEN 256
#define BUFSIZE 2048

/******************************************************************************
 * Include these instructions here for documentation purposes AND so that we
 * can debug this code-path on systems that don't support this feature
 ******************************************************************************/
#ifndef  MSG_WAITFORONE
struct msghdr
{
    void         *msg_name;         // optional address
    socklen_t     msg_namelen;      // size of address
    struct iovec *msg_iov;          // scatter/gather array
    int           msg_iovlen;       // members in msg_iov
    void         *msg_control;      // ancillary data, see below
    socklen_t     msg_controllen;   // ancillary data buffer len
    int           msg_flags;        // flags on received message
};
struct mmsghdr {
    struct msghdr msg_hdr;  /* Message header */
    unsigned int  msg_len;  /* Number of received bytes for header */
};
struct iovec
{
    void *iov_base;     /* Pointer to data.  */
    size_t iov_len;     /* Length of data.  */
};
int recvmmsg(int sockfd, struct mmsghdr *msgvec, unsigned int vlen,
                    unsigned int flags, struct timespec *timeout)
{
    return 0;
}
struct timespec
{
        unsigned tv_sec;
        unsigned tv_nsec;
};
#endif

/******************************************************************************
 ******************************************************************************/
void
server_thread_mmsg(void *v)
{
    struct ThreadData *thread = (struct ThreadData *)v;
    SOCKET fd = thread->fd;
    size_t i;

    struct mmsghdr msgs[VLEN];
	struct iovec iovecs[VLEN];
	char bufs[VLEN][BUFSIZE+1];
    struct timespec timeout;

    timeout.tv_sec = 1;
    timeout.tv_nsec = 0;

    /*
     * Init multi-message uffers
     */
    memset(msgs, 0, sizeof(msgs[0]) * VLEN);
    for (i = 0; i < VLEN; i++) {
        iovecs[i].iov_base         = bufs[i];
        iovecs[i].iov_len          = BUFSIZE;
        msgs[i].msg_hdr.msg_iov    = &iovecs[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
    }

    /*
     * Sit in loop processing incoming UDP packets
     */
    for (;;) {
        struct sockaddr_in6 sin;
        int msgs_received = 0;
        socklen_t sizeof_sin = sizeof(sin);
        
#ifdef  MSG_WAITFORONE
        msgs_received = recvmmsg(  fd,
                                    msgs,
                                    VLEN,
                                    MSG_WAITFORONE,
                                    &timeout);
#endif
        if (msgs_received <= 0)
            continue;
        
        thread->total_packets += msgs_received;

        for (i = 0; i < (size_t)msgs_received; i++) {
            //sendto(fd, buf, bytes_received, 0, (struct sockaddr*)&sin, sizeof_sin);         
        }
    }

    fprintf(stderr, "end thread\n");
}

/******************************************************************************
 ******************************************************************************/
void
bench_server(struct Configuration *cfg)
{
    SOCKET fd;
    unsigned i;
    struct ThreadData threaddata[64];
    
    memset(&threaddata, 0, sizeof(threaddata));


    fprintf(stderr, "creating server socket on port %u\n", cfg->port);
    fd = create_server_socket(cfg->port, cfg->is_reuseport);
    if (fd <= 0)
        return;

    /*
     * Create all threads
     */
    for (i=0; i<cfg->thread_count; i++) {
        struct ThreadData *t = &threaddata[i];
        
        t->fd = fd;
        t->port = cfg->port;
        
        if (cfg->is_reuseport && i+1 < cfg->thread_count) {
            fd = create_server_socket(cfg->port, cfg->is_reuseport);
            if (fd <= 0)
                return;
        }

        if (cfg->is_mmsg)
            pixie_begin_thread(server_thread_mmsg, 0, t);
        else
            pixie_begin_thread(server_thread, 0, t);
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
