/*********************************************************************
 *
 * $Id: ytcp.c 65727 2025-04-10 15:52:52Z mvuilleu $
 *
 * Implementation of a client TCP stack
 *
 * - - - - - - - - - License information: - - - - - - - - -
 *
 *  Copyright (C) 2011 and beyond by Yoctopuce Sarl, Switzerland.
 *
 *  Yoctopuce Sarl (hereafter Licensor) grants to you a perpetual
 *  non-exclusive license to use, modify, copy and integrate this
 *  file into your software for the sole purpose of interfacing
 *  with Yoctopuce products.
 *
 *  You may reproduce and distribute copies of this file in
 *  source or object form, as long as the sole purpose of this
 *  code is to interface with Yoctopuce products. You must retain
 *  this notice in the distributed source file.
 *
 *  You should refer to Yoctopuce General Terms and Conditions
 *  for additional information regarding your rights and
 *  obligations.
 *
 *  THE SOFTWARE AND DOCUMENTATION ARE PROVIDED "AS IS" WITHOUT
 *  WARRANTY OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING
 *  WITHOUT LIMITATION, ANY WARRANTY OF MERCHANTABILITY, FITNESS
 *  FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO
 *  EVENT SHALL LICENSOR BE LIABLE FOR ANY INCIDENTAL, SPECIAL,
 *  INDIRECT OR CONSEQUENTIAL DAMAGES, LOST PROFITS OR LOST DATA,
 *  COST OF PROCUREMENT OF SUBSTITUTE GOODS, TECHNOLOGY OR
 *  SERVICES, ANY CLAIMS BY THIRD PARTIES (INCLUDING BUT NOT
 *  LIMITED TO ANY DEFENSE THEREOF), ANY CLAIMS FOR INDEMNITY OR
 *  CONTRIBUTION, OR OTHER SIMILAR COSTS, WHETHER ASSERTED ON THE
 *  BASIS OF CONTRACT, TORT (INCLUDING NEGLIGENCE), BREACH OF
 *  WARRANTY, OR OTHERWISE.
 *
 *********************************************************************/

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "ydef_private.h"
#define __FILE_ID__     MK_FILEID('T','C','P')
#define __FILENAME__   "ytcp"

#if defined(WINDOWS_API) && !defined(_MSC_VER)
#define _WIN32_WINNT 0x501
#endif
#ifdef WINDOWS_API
typedef int socklen_t;
#if defined(__BORLANDC__)
#pragma warn -8004
#pragma warn -8019
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma warn +8004
#pragma warn +8019
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
#endif
#include "ytcp.h"
#include "yproto.h"
#ifdef YAPI_IN_YDEVICE
#include "privhash.h"
#else
#include "yhash.h"
#endif
#include "yssl.h"

#ifdef WIN32
#ifndef WINCE
#include <iphlpapi.h>
#if defined(_MSC_VER) || defined (__BORLANDC__)
#pragma comment(lib, "Ws2_32.lib")
#endif
#else
        #pragma comment(lib, "Ws2.lib")
#endif
#else
    #include <unistd.h>
    #include <fcntl.h>
    #include <netdb.h>
#endif
#if defined(OSX_API) && !defined(select)
#include <sys/_select.h>
#endif
//todo: delare elseware
static int ws_sendFrame(HubSt *hub, int stream, int tcpchan, const u8 *data, int datalen, char *errmsg);

//#define DEBUG_SLOW_TCP
//#define TRACE_TCP_REQ
//#define PERF_TCP_FUNCTIONS
#ifdef PERF_TCP_FUNCTIONS


typedef struct {
    yPerfMon  TCPOpen_socket;
    yPerfMon  TCPOpen_connect;
    yPerfMon  TCPOpen_setsockopt_noblock;
    yPerfMon  TCPOpen_setsockopt_nodelay;
    yPerfMon  TCPOpenReq_wait;
    yPerfMon  TCPOpenReq;
    yPerfMon  tmp1;
    yPerfMon  tmp2;
    yPerfMon  tmp3;
    yPerfMon  tmp4;
} yTcpPerfMonSt;

yTcpPerfMonSt yTcpPerf;


#define YPERF_TCP_ENTER(NAME) {yTcpPerf.NAME.count++;yTcpPerf.NAME.tmp=yapiGetTickCount();}
#define YPERF_TCP_LEAVE(NAME) {yTcpPerf.NAME.leave++;yTcpPerf.NAME.totaltime += yapiGetTickCount()- yTcpPerf.NAME.tmp;}


void dumpYTcpPerf(void)
{
    dumpYPerfEntry(&yTcpPerf.TCPOpen_socket,"TCPOpen:socket");
    dumpYPerfEntry(&yTcpPerf.TCPOpen_connect,"TCPOpen:connect");
    dumpYPerfEntry(&yTcpPerf.TCPOpen_setsockopt_noblock,"TCPOpen:sockopt_noblock");
    dumpYPerfEntry(&yTcpPerf.TCPOpen_setsockopt_nodelay,"TCPOpen:sockopt_nodelay");
    dumpYPerfEntry(&yTcpPerf.TCPOpenReq_wait,"TCPOpenReq:wait");
    dumpYPerfEntry(&yTcpPerf.TCPOpenReq,"TCPOpenReq");
    dumpYPerfEntry(&yTcpPerf.tmp1,"TCP:tmp1");
    dumpYPerfEntry(&yTcpPerf.tmp2,"TCP:tmp2");
    dumpYPerfEntry(&yTcpPerf.tmp3,"TCP:tmp3");
    dumpYPerfEntry(&yTcpPerf.tmp4,"TCP:tmp4");
}
#else
#define YPERF_TCP_ENTER(NAME)
#define YPERF_TCP_LEAVE(NAME)
#endif

#ifdef DUMP_YSOCKET_MULTI_TRAFFIC
//#define TRACE_ON_MEMORY
#ifdef WINDOWS_API
#include "Windows.h"
#include <direct.h>
#else
#include <sys/time.h>
#include <unistd.h>
#endif

#ifndef WINDOWS_API
#include <time.h>
int ylocaltime(struct tm *_out,const time_t *time)
{
    struct tm *tmp = localtime(time);
    memcpy(_out,tmp,sizeof(struct tm));
    return 0;
}
#endif


#ifdef TRACE_ON_MEMORY

yFifoBuf trace_fifo;
int trace_fifo_initialized = 0;

int write_onfile(const char* filename, char* mode, const char* bin, int binsize)
{

    if (trace_fifo_initialized==0) {
        int size = 1024 * 15;
        u8 *ptr = yMalloc(size);
        yFifoInitEx(&trace_fifo, ptr, size);
        trace_fifo_initialized = 1;
    }

    yPushFifoEx(&trace_fifo, bin, binsize);
    return 0;
}
#else
int write_onfile(const char * filename, char *mode, const char * bin, int binsize)
{
    FILE *f;
    int retry_count = 1;
    // write on file
retry:
    if (YFOPEN(&f, filename, mode) != 0) {
        if (retry_count--){
            //_mkdir("sock_trace");
            goto retry;
        }
        return -1;
    }
    fwrite(bin, 1, binsize, f);
    fclose(f);
    return 0;
}
#endif

void TRACE_SOCK_APPEND(const char *filename, int clear_log, const char *msg, const u8 * bin, int binsize, u64 start_tm)
{
    char buffer[2048], buffer2[64];
    struct tm timeinfo;
    time_t rawtime;
    int threadIdx, first_len;
    // compute time string
    time(&rawtime);
    ylocaltime(&timeinfo, &rawtime);
    strftime(buffer2, sizeof(buffer2), "%Y-%m-%d %H:%M:%S", &timeinfo);
    //format first info line
    threadIdx = yThreadIndex();
    first_len = YSPRINTF(buffer, 2048, "[%s/%"FMTu64"](%d): %s\n",buffer2, yapiGetTickCount() - start_tm, threadIdx, msg);
    // write on file
    write_onfile(filename, clear_log ? "wb" : "ab", buffer, first_len);
    dbglog("-----%s %d bytes (%"FMTu64")\n", msg, binsize, yapiGetTickCount() -start_tm);
    int pos, j;
    YSPRINTF(buffer, 2048, "   dump %d bytes\n", binsize);
    write_onfile(filename, "ab", buffer, ystrlen(buffer));
    for(pos = 0; pos < binsize; pos += 16) {
        memset(buffer2, '.', 16);

        for(j = 0; j < 16 && pos + j < binsize; j++){
            if(bin[pos+j] >= ' '){
                buffer2[j] = bin[pos+j];
            }
        }
        buffer2[j] = 0;
        YSPRINTF(buffer, 2048, "   %02x.%02x.%02x.%02x %02x.%02x.%02x.%02x %02x.%02x.%02x.%02x %02x.%02x.%02x.%02x    %s\n",
                   bin[pos+0], bin[pos+1], bin[pos+2], bin[pos+3],
            bin[pos+4], bin[pos+5], bin[pos+6], bin[pos+7],
            bin[pos+8], bin[pos+9], bin[pos+10], bin[pos+11],
            bin[pos+12], bin[pos+13], bin[pos+14], bin[pos+15],
                   (char*)buffer2);
        write_onfile(filename, "ab", buffer, ystrlen(buffer));
    }
}

void dump_socket(YSOCKET_MULTI newskt, int clear_log, const char* msg, const u8* bin, int binsize )
{
    char filename[256];
    u64 creation_tm = 0;
    if (newskt) {
        creation_tm = newskt->creation_tm;
    }
    YSPRINTF(filename, 256, "sock_%"FMTu64"_%p.txt", creation_tm, newskt);
    TRACE_SOCK_APPEND(filename, clear_log, msg, bin, binsize, creation_tm);
}
#define TRACE_WS_SOCK_APPEND(filename, clear_log, msg, bin, binsize, start_tm) TRACE_SOCK_APPEND(filename, clear_log, msg, bin, binsize, start_tm)

#else
#define TRACE_WS_SOCK_APPEND(filename, clear_log, msg, bin, binsize, start_tm)

#endif


void yDupSet(char **storage, const char *val)
{
    int len = (val ? (int)strlen(val) + 1 : 1);

    if (*storage)
        yFree(*storage);
    *storage = (char*)yMalloc(len);
    if (val) {
        memcpy(*storage, val, len);
    } else {
        **storage = 0;
    }
}

int yNetSetErrEx(const char *fileid, u32 line, unsigned err, char *errmsg)
{
    int len;
    if (errmsg == NULL)
        return YAPI_IO_ERROR;
    YSPRINTF(errmsg,YOCTO_ERRMSG_LEN, "%s:%d:tcp(%d):", fileid, line, err);
    //dbglog("yNetSetErrEx -> %s:%d:tcp(%d)\n",fileid,line,err);

#if defined(WINDOWS_API) && !defined(WINCE)
    len = (int)strlen(errmsg);
    FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)(errmsg + len),
        YOCTO_ERRMSG_LEN - len, NULL);
    // revove \r\n at the end of errmsg
    len = (int)strlen(errmsg);
    while (len > 0 && (errmsg[len - 1] == '\n' || errmsg[len - 1] == '\r')) {
        errmsg[len - 1] = 0;
        len--;
    }
#else
    len=YSTRLEN(errmsg);
    strcpy(errmsg+len, strerror((int)err));
#endif
    return YAPI_IO_ERROR;
}
#if 1
#define yNetLogErr()  yNetLogErrEx(__LINE__,SOCK_ERR)

static int yNetLogErrEx(u32 line, unsigned err)
{
    int retval;
    char errmsg[YOCTO_ERRMSG_LEN];
    retval = yNetSetErrEx(__FILENAME__, line, err, errmsg);
    dbglog("%s", errmsg);
    return retval;
}
#endif

//#define DEBUG_SOCKET_USAGE
#ifdef DEBUG_SOCKET_USAGE


static void dump_udp_packet(char *str, const u8 *buffer, int size, IPvX_ADDR *remote_ip, u16 port, YSOCKET skt)
{
    int pos, j;
    char buff[IPSTR_SIZE];
    iptoa(remote_ip, buff);
    dbglog("UDP: %s %d bytes addr=%s port=%d (sock=%d)\n", str, size, buff, port, skt);
#if 1
    for (pos = 0; pos < size; pos += 16) {
        char buffer2[32];
        memset(buffer2, '.', 16);

        for (j = 0; j < 16 && pos + j < size; j++) {
            if (buffer[pos + j] >= ' ') {
                buffer2[j] = buffer[pos + j];
            }
        }
        buffer2[j] = 0;
        dbglog("UDP: %02x.%02x.%02x.%02x %02x.%02x.%02x.%02x %02x.%02x.%02x.%02x %02x.%02x.%02x.%02x    %s\n",
               buffer[pos+0], buffer[pos+1], buffer[pos+2], buffer[pos+3],
               buffer[pos+4], buffer[pos+5], buffer[pos+6], buffer[pos+7],
               buffer[pos+8], buffer[pos+9], buffer[pos+10], buffer[pos+11],
               buffer[pos+12], buffer[pos+13], buffer[pos+14], buffer[pos+15],
               (char*)buffer2);
    }
#endif
}


#define yclosesocket(skt) yclosesocket_ex(__FILENAME__, __LINE__, skt)

void yclosesocket_ex(const char *file, int line, YSOCKET skt)
{
    dbglogf(file, line, "close socket %x\n", skt);
    closesocket(skt);
}


#define ysocket(domain, type, protocol) ysocket_ex(__FILENAME__, __LINE__, domain, type, protocol)

YSOCKET ysocket_ex(const char *file, int line, int domain, int type, int protocol)
{
    YSOCKET skt = socket(domain, type, protocol);
    dbglogf(file, line, "open socket %d (%x,%x,%x)\n", skt, domain, type, protocol);
    return skt;
}

#define ybind(skt, addr, addrlen) ybind_ex(__FILENAME__, __LINE__, skt, addr, addrlen)

int ybind_ex(const char *file, int line, YSOCKET skt, struct sockaddr *addr, socklen_t addrlen)
{
    int res = bind(skt, addr, addrlen);
    if (res < 0) {
        char errmsg[YOCTO_ERRMSG_LEN];
        int err = SOCK_ERR;
        yNetSetErrEx(__FILENAME__, __LINE__, err, errmsg);
        dbglogf(file, line, "bind failed with code %d: %s (skt=%d, addr=%p len=%d)\n", err, errmsg, skt, addr, addrlen);
    }
    if (addrlen == sizeof(struct sockaddr_in6)) {
        struct sockaddr_in6 *p = (struct sockaddr_in6*)addr;
        u8 *d = (u8*)&p->sin6_addr;
        dbglogf(file, line, "bind ipv6 skt=%d, ptr=%p len=%d\n", skt, addr, addrlen);
        dbglogf(file, line, "family=%d sin6_port=%x(=%d) scope_id=%d flowinfo=%d\n", p->sin6_family, p->sin6_port, ntohs(p->sin6_port), p->sin6_scope_id, p->sin6_flowinfo);
        dbglogf(file, line, "addr=%x:%x:%x:%x:%x:%x:%x:%x:%x:%x:%x:%x:%x:%x:%x:%x\n",
                d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7], d[8], d[9], d[10], d[11], d[12], d[13], d[14], d[15]);
    } else if (addrlen == sizeof(struct sockaddr_in)) {
        struct sockaddr_in *p = (struct sockaddr_in*)addr;
        u8 *d = (u8*)&p->sin_addr;
        dbglogf(file, line, "bind ipv4 skt=%d, ptr=%p len=%d\n", skt, addr, addrlen);
        dbglogf(file, line, "family=%d sin6_port=%x(=%d)\n", p->sin_family, p->sin_port, ntohs(p->sin_port));
        dbglogf(file, line, "addr=%d.%d.%d.%d\n", d[0], d[1], d[2], d[3]);
    } else {
        dbglogf(file, line, "bind skt=%d, addr=%p len=%d\n", skt, addr, addrlen);
    }
    return res;
}


#define ysend(skt, buf, len, flags) ysend_ex(__FILENAME__, __LINE__, skt, buf, len, flags)

int ysend_ex(const char *file, int line, YSOCKET skt, const char *buffer, int tosend, int flags)
{
    int res = (int)send(skt, buffer, tosend, flags);
    //dbglogf(file, line, "send socket %x (%d,%x -> %d)\n", skt, tosend, flags, res);
    return res;
}

#define ysendto(skt, buf, len, flags, dst, addrlen) ysendto_ex(__FILENAME__, __LINE__, skt, buf, len, flags, dst, addrlen)

int ysendto_ex(const char *file, int line, YSOCKET skt, const char *buffer, int len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen)

{
    IPvX_ADDR remote_ip;
    u16 remote_port;
    int res = (int)sendto(skt, buffer, len, flags, dest_addr, addrlen);
    dbglogf(file, line, "sendto socket %d (%d,%x -> %d)\n", skt, len, flags, res);
    if (addrlen == sizeof(struct sockaddr_in6)) {
        struct sockaddr_in6 *addr_ipv6 = (struct sockaddr_in6*)dest_addr;
        memcpy(&remote_ip.v6.addr, &addr_ipv6->sin6_addr, sizeof(addr_ipv6->sin6_addr));
        remote_port = ntohs(addr_ipv6->sin6_port);
    } else {
        struct sockaddr_in *addr_ipv4 = (struct sockaddr_in*)dest_addr;
        setIPv4Val(&remote_ip, addr_ipv4->sin_addr.s_addr);
        remote_port = ntohs(addr_ipv4->sin_port);
    }
    dump_udp_packet("write", (const u8*)buffer, res, &remote_ip, remote_port, skt);
    return res;
}

#define yrecv(skt, buf, len, flags) yrecv_ex(__FILENAME__, __LINE__, skt, buf, len, flags)

int yrecv_ex(const char *file, int line, YSOCKET skt, char *buf, int len, int flags)
{
    int res = recv(skt, buf, len, flags);
    //dbglogf(file, line, "read socket %x (%d,%x -> %d)\n", skt, len, flags, res);
    return res;
}

#define  yrecvfrom(skt, buf, len, flags, src, src_len) yrecvfrom_ex(__FILENAME__, __LINE__, skt, buf, len, flags, src, src_len)

int yrecvfrom_ex(const char *file, int line, YSOCKET skt, char *buf, int len, int flags, struct sockaddr *src_addr, socklen_t *addrlen)
{
    IPvX_ADDR remote_ip;
    u16 remote_port;
    int res = (int)recvfrom(skt, buf, len, flags, src_addr, addrlen);
    if (*addrlen == sizeof(struct sockaddr_in6)) {
        struct sockaddr_in6 *addr_ipv6 = (struct sockaddr_in6*)src_addr;
        memcpy(&remote_ip.v6.addr, &addr_ipv6->sin6_addr, sizeof(addr_ipv6->sin6_addr));
        remote_port = ntohs(addr_ipv6->sin6_port);
    } else {
        struct sockaddr_in *addr_ipv4 = (struct sockaddr_in*)src_addr;
        setIPv4Val(&remote_ip, addr_ipv4->sin_addr.s_addr);
        remote_port = ntohs(addr_ipv4->sin_port);
    }
    if (res >= 0) {
        dbglogf(file, line, "read from socket %d (%d,%x -> %d)\n", skt, len, flags, res);
        dump_udp_packet("read", (const u8*)buf, res, &remote_ip, remote_port, skt);
    }
    return res;
}


#else
#define yclosesocket(skt) closesocket(skt)
#define ysocket(domain, type, protocol) socket(domain, type, protocol)
#define ybind(socket, addr, len) bind(socket, addr, len)
#define ysend(skt, buf, len, flags) send(skt, buf, len, flags)
#define ysendto(skt, buf, len, flags, addr, addrlen) sendto(skt, buf, len, flags, addr, addrlen)
#define yrecv(skt, buf, len, flags) recv(skt, buf, len, flags)
#define yrecvfrom(skt, buf, len, flags, addr, addrlen) recvfrom(skt, buf, len, flags, addr, addrlen)
#endif

void yInitWakeUpSocket(WakeUpSocket *wuce)
{
    wuce->listensock = INVALID_SOCKET;
    wuce->signalsock = INVALID_SOCKET;
}


int yStartWakeUpSocket(WakeUpSocket *wuce, char *errmsg)
{
    u32 optval;
    socklen_t localh_size;
    struct sockaddr_in localh;

    if (wuce->listensock != INVALID_SOCKET || wuce->signalsock != INVALID_SOCKET) {
        return YERRMSG(YAPI_INVALID_ARGUMENT, "WakeUpSocket already Started");
    }
    //create socket
    wuce->listensock = ysocket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (wuce->listensock == INVALID_SOCKET) {
        return yNetSetErr();
    }
    optval = 1;
    setsockopt(wuce->listensock,SOL_SOCKET,SO_REUSEADDR, (char*)&optval, sizeof(optval));

    localh_size = sizeof(localh);
    // set port to 0 since we accept any port
    memset(&localh, 0, localh_size);
    localh.sin_family = AF_INET;
    localh.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (ybind(wuce->listensock, (struct sockaddr*)&localh, localh_size) < 0) {
        return yNetSetErr();
    }
    if (getsockname(wuce->listensock, (struct sockaddr*)&localh, &localh_size) < 0) {
        return yNetSetErr();
    }
    wuce->signalsock = ysocket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (wuce->signalsock == INVALID_SOCKET) {
        return yNetSetErr();
    }
    if (connect(wuce->signalsock, (struct sockaddr*)&localh, localh_size) < 0) {
        return yNetSetErr();
    }
    return YAPI_SUCCESS;
}

int yDringWakeUpSocket(WakeUpSocket *wuce, u8 signal, char *errmsg)
{
    if (ysend(wuce->signalsock, (char*)&signal, 1, SEND_NOSIGPIPE) < 0) {
        return yNetSetErr();
    }
    return YAPI_SUCCESS;
}

int yConsumeWakeUpSocket(WakeUpSocket *wuce, char *errmsg)
{
    u8 signal = 0;
    if (yrecv(wuce->listensock, (char*)&signal, 1, 0) < 0) {
        return yNetSetErr();
    }
    return signal;
}

void yFreeWakeUpSocket(WakeUpSocket *wuce)
{
    if (wuce->listensock != INVALID_SOCKET) {
        yclosesocket(wuce->listensock);
        wuce->listensock = INVALID_SOCKET;
    }
    if (wuce->signalsock != INVALID_SOCKET) {
        yclosesocket(wuce->signalsock);
        wuce->signalsock = INVALID_SOCKET;
    }
}


int yResolveDNS(const char *name, IPvX_ADDR *ip, char *errmsg)
{
    struct addrinfo *infos, *p;
    struct addrinfo hint;
    IPvX_ADDR ipv6_res;
    int has_ipv6 = 0;
    int res = -1;
#ifdef LOG_GETADDRINFO
    char buffer[512];
#endif

    memset(&hint, 0, sizeof(hint));
    memset(&ipv6_res, 0, sizeof(ipv6_res));
    hint.ai_family = AF_UNSPEC; // AF_INET6;

    if (getaddrinfo(name, NULL, &hint, &infos) != 0) {
        if (errmsg) {
            YSPRINTF(errmsg, YOCTO_ERRMSG_LEN, "Unable to resolve host %s (%s:%d/errno=%d)", name, __FILENAME__, __LINE__, SOCK_ERR);
        }
        return res;
    }

    // Retrieve each address and print out the hex bytes
    for (p = infos; p != NULL; p = p->ai_next) {
        if (p->ai_family == AF_INET6) {
            int addrlen = sizeof(IPvX_ADDR);
            struct sockaddr_in6 *addr_v6 = ((struct sockaddr_in6*)p->ai_addr);
#ifdef LOG_GETADDRINFO
            inet_ntop(AF_INET6, &(addr_v6->sin6_addr), buffer, 512);
            dbglog("ipv6:%s\n", buffer);
#endif
            // if we have an ipv6 result, use is as backup if we did not find a valid ipv4 res
            memcpy(&ipv6_res, &addr_v6->sin6_addr, addrlen);
            has_ipv6 = 1;
        } else if (p->ai_family == AF_INET) {
            u32 ipv4 = ((struct sockaddr_in*)p->ai_addr)->sin_addr.s_addr;
#ifdef LOG_GETADDRINFO
            inet_ntop(AF_INET, &(((struct sockaddr_in*)p->ai_addr)->sin_addr), buffer, 512);
            dbglog("ipv4:%s\n", buffer);
#endif
            setIPv4Val(ip, ipv4);
            res = 1;
            break;
        }
    }
    if (res < 0 && has_ipv6) {
        memcpy(ip, &ipv6_res, sizeof(IPvX_ADDR));
        res = 1;
    }
    freeaddrinfo(infos);
    return res;
}


#define YDNS_CACHE_SIZE 32
#define YDNS_CACHE_VALIDITY 600000u //10 minutes

typedef struct {
    char *name;
    IPvX_ADDR ip;
    u64 time;
} DnsCache;

DnsCache dnsCache[YDNS_CACHE_SIZE];

static int isStrAnIpV4(const char *hostname)
{
    u64 part_len;
    int iptest = 0;
    const char *p = strchr(hostname, '.');
    if (!p) {
        return 0;
    }
    part_len = p - hostname;
    if (part_len <= 3) {
        char buffer[4];
        memcpy(buffer, hostname, (int)part_len);
        buffer[part_len] = 0;
        iptest = atoi(buffer);
    }
    if (iptest && iptest < 256 && strlen(hostname) < 16) {
        // this is probably an ip
        return 1;
    }
    return 0;
}

static int resolveDNSCache(const char *host, IPvX_ADDR *resolved_ip, char *errmsg)
{
    int i, firstFree = -1;
    IPvX_ADDR ip;
    int res;

    for (i = 0; i < YDNS_CACHE_SIZE; i++) {
        if (dnsCache[i].name && strcmp(dnsCache[i].name, host) == 0) {
            break;
        }
        if (firstFree < 0 && dnsCache[i].name == NULL) {
            firstFree = i;
        }
    }
    if (i < YDNS_CACHE_SIZE) {
        if ((u64)(yapiGetTickCount() - dnsCache[i].time) <= YDNS_CACHE_VALIDITY) {
            memcpy(resolved_ip, &dnsCache[i].ip, sizeof(IPvX_ADDR));
            return 1;
        }
        firstFree = i;
    }
    if (isStrAnIpV4(host)) {
        u32 ipv4 = inet_addr(host);
        setIPv4Val(&ip, ipv4);
        res = 1;
    } else {
        res = yResolveDNS(host, &ip, errmsg);
    }
    if (res > 0 && firstFree < YDNS_CACHE_SIZE) {
        dnsCache[firstFree].name = ystrdup_s(host);
        memcpy(&dnsCache[firstFree].ip, &ip, sizeof(IPvX_ADDR));
        memcpy(resolved_ip, &dnsCache[firstFree].ip, sizeof(IPvX_ADDR));
        dnsCache[firstFree].time = yapiGetTickCount();
        return 1;
    }
    return -1;
}


/********************************************************************************
* Pure TCP functions
*******************************************************************************/

static int yTcpInitBasic(char *errmsg)
{
#ifdef WINDOWS_API
    // Initialize Winsock 2.2
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        return YERRMSG(YAPI_IO_ERROR, "Unable to start Windows Socket");
    }
#endif
    TCPLOG("yTcpInit\n");
    memset(dnsCache, 0, sizeof(dnsCache));
    return YAPI_SUCCESS;
}

static void yTcpShutdownBasic(void)
{
    int i;
    TCPLOG("yTcpShutdown\n");
#ifdef PERF_TCP_FUNCTIONS
    dumpYTcpPerf();
#endif
    for (i = 0; i < YDNS_CACHE_SIZE; i++) {
        if (dnsCache[i].name) {
            yFree(dnsCache[i].name);
            dnsCache[i].name = NULL;
        }
    }

#ifdef WINDOWS_API
    WSACleanup();
#endif
}


#define DEFAULT_TCP_ROUND_TRIP_TIME  30
#define DEFAULT_TCP_MAX_WINDOW_SIZE  (4*65536)

int yTcpOpenBasicEx(YSOCKET *newskt, const IPvX_ADDR *ip, u16 port, u64 mstimeout, char *errmsg)
{
    u8 addr[sizeof(struct sockaddr_storage)];
    int iResult;
    u_long flags;
    YSOCKET skt;
    fd_set readfds, writefds, exceptfds;
    struct timeval timeout;
    int tcp_sendbuffer;
    int addrlen;
    int socktype;

#ifdef WINDOWS_API
    char noDelay = 1;
    int optlen;
#else
    int  noDelay = 1;
    socklen_t optlen;
#ifdef SO_NOSIGPIPE
    int  noSigpipe = 1;
#endif
#endif


#ifdef DEBUG_TCP
    {
        char ipa_buff[IPSTR_SIZE];
        iptoa(&ip, ipa_buff);
        TCPLOG("yTcpOpen %p(socket) [dst=%s port=%d %dms]\n", newskt, ipa_buff, port, mstimeout);
    }
#endif
    memset(&addr, 0, sizeof(addr));
    //----------------------
    // The sockaddr_in structure specifies the address family,
    // IP address, and port of the server to be connected to.
    if (isIPv4(ip)) {
        struct sockaddr_in *addr_v4 = (struct sockaddr_in*)&addr;
        socktype = AF_INET;
        TCPLOG("ytcpOpen %d:%d:%d:%d port=%d skt= %x\n",
               ip->v4.addr.v[0], ip->v4.addr.v[1], ip->v4.addr.v[2], ip-<v4.addr.v[3], port);
        addr_v4->sin_family = AF_INET;
        addr_v4->sin_addr.s_addr = ip->v4.addr.Val;
        addr_v4->sin_port = (u16)htons(port);
        addrlen = sizeof(struct sockaddr_in);
    } else {
        struct sockaddr_in6 *addr_v6 = (struct sockaddr_in6*)&addr;
        socktype = AF_INET6;
        TCPLOG("ytcpOpen %x:%x:%x:%x:%x:%x:%x:%x port=%d\n",
               ntohs(ip->v6.addr[0]), ntohs(ip->v6.addr[1]), ntohs(ip->v6.addr[2]), ntohs(ip->v6.addr[3]), ntohs(ip->v6.addr[4]), ntohs(ip->v6.addr[5]), ntohs(ip->v6.addr[6]), ntohs(ip->v6.addr[7]),
               port);
        addr_v6->sin6_family = AF_INET6;
        memcpy(&addr_v6->sin6_addr, ip->v6.addr, sizeof(IPvX_ADDR));
        addr_v6->sin6_port = htons(port);
        addrlen = sizeof(struct sockaddr_in6);
    }
    YPERF_TCP_ENTER(TCPOpen_socket);
    *newskt = INVALID_SOCKET;
    skt = ysocket(socktype, SOCK_STREAM, IPPROTO_TCP);
    YPERF_TCP_LEAVE(TCPOpen_socket);
    if (skt == INVALID_SOCKET) {
        return yNetSetErr();
    }
    YPERF_TCP_ENTER(TCPOpen_connect);

    //----------------------
    // Connect to server.
    YPERF_TCP_ENTER(TCPOpen_setsockopt_noblock);
    //set socket as non blocking
#ifdef WINDOWS_API
    flags = 1;
    ioctlsocket(skt, FIONBIO, &flags);
#else
    flags = fcntl(skt, F_GETFL, 0);
    fcntl(skt, F_SETFL, flags | O_NONBLOCK);
#ifdef SO_NOSIGPIPE
    setsockopt(skt, SOL_SOCKET, SO_NOSIGPIPE, (void *)&noSigpipe, sizeof(int));
#endif
#endif
    YPERF_TCP_LEAVE(TCPOpen_setsockopt_noblock);
    connect(skt, (struct sockaddr*)&addr, addrlen);

    // wait for the connection with a select
    memset(&timeout, 0, sizeof(timeout));
    if (mstimeout != 0) {
        u64 nbsec = mstimeout / 1000;
        timeout.tv_sec = (long)nbsec;
        timeout.tv_usec = ((int)(mstimeout - (nbsec * 1000))) * 1000;
    } else {
        timeout.tv_sec = 20;
    }
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_ZERO(&exceptfds);
    FD_SET(skt, &readfds);
    FD_SET(skt, &writefds);
    FD_SET(skt, &exceptfds);
    iResult = select((int)skt + 1, &readfds, &writefds, &exceptfds, &timeout);
    if (iResult < 0) {
        yclosesocket(skt);
        return yNetSetErr();
    }
    if (FD_ISSET(skt, &exceptfds) || !FD_ISSET(skt, &writefds)) {
        yclosesocket(skt);
        if (errmsg) {
            char host[IPSTR_SIZE];
            iptoa(ip, host);
            YSPRINTF(errmsg, YOCTO_ERRMSG_LEN, "Unable to connect to %s:%d", host, port);
        }
        return YAPI_IO_ERROR;
    }
    YPERF_TCP_LEAVE(TCPOpen_connect);
    if (iResult == SOCKET_ERROR) {
        yclosesocket(skt);
        return yNetSetErr();
    }
    YPERF_TCP_ENTER(TCPOpen_setsockopt_nodelay);
    if (setsockopt(skt, IPPROTO_TCP, TCP_NODELAY, &noDelay, sizeof(noDelay)) < 0) {
#if 0
        switch(errno) {
            case EBADF:
                dbglog("The argument sockfd is not a valid descriptor.\n");
                break;
            case EFAULT:
                dbglog("The address pointed to by optval is not in a valid part of the process address space. For getsockopt(), "
                        "this error may also be returned if optlen is not in a valid part of the process address space.\n");
                break;
            case EINVAL:
                dbglog("optlen invalid in setsockopt(). In some cases this error can also occur for an invalid value in optval "
                       "(e.g., for the IP_ADD_MEMBERSHIP option described in ip(7)).\n");
                break;
            case ENOPROTOOPT:
                dbglog("The option is unknown at the level indicated.\n");
                break;
            case ENOTSOCK:
                dbglog("The argument sockfd is a file, not a socket.\n");
                break;
        }
#endif
        dbglog("SetSockOpt TCP_NODELAY failed %d\n", errno);
    }
    YPERF_TCP_LEAVE(TCPOpen_setsockopt_nodelay);

    // Get buffer size
    optlen = sizeof(tcp_sendbuffer);
    if (getsockopt(skt, SOL_SOCKET, SO_SNDBUF, (void*)&tcp_sendbuffer, &optlen) >= 0) {
#if 0
        dbglog("Default windows size is %d\n", tcp_sendbuffer);
#endif
        if (tcp_sendbuffer < DEFAULT_TCP_MAX_WINDOW_SIZE) {
            // Set buffer size to 64k
            tcp_sendbuffer = DEFAULT_TCP_MAX_WINDOW_SIZE;
            if (setsockopt(skt, SOL_SOCKET, SO_SNDBUF, (void*)&tcp_sendbuffer, sizeof(tcp_sendbuffer)) < 0) {
#if 0
                switch (errno) {
                case EBADF:
                    dbglog("The argument sockfd is not a valid descriptor.\n");
                    break;
                case EFAULT:
                    dbglog("The address pointed to by optval is not in a valid part of the process address space. For getsockopt(), "
                        "this error may also be returned if optlen is not in a valid part of the process address space.\n");
                    break;
                case EINVAL:
                    dbglog("optlen invalid in setsockopt(). In some cases this error can also occur for an invalid value in optval "
                        "(e.g., for the IP_ADD_MEMBERSHIP option described in ip(7)).\n");
                    break;
                case ENOPROTOOPT:
                    dbglog("The option is unknown at the level indicated.\n");
                    break;
                case ENOTSOCK:
                    dbglog("The argument sockfd is a file, not a socket.\n");
                    break;
                }
#endif
                dbglog("SetSockOpt SO_SNDBUF %d failed %d\n", tcp_sendbuffer, errno);
            }
        }
    } else {
        dbglog("getsockopt: unable to get tcp buffer size\n");
    }

    *newskt = skt;

    return YAPI_SUCCESS;
}

int yTcpOpenBasic(YSOCKET *newskt, const char *host, u16 port, u64 mstimeout, char *errmsg)
{
    IPvX_ADDR ip;
    int iResult = resolveDNSCache(host, &ip, errmsg);
    if (iResult < 0) {
        return iResult;
    }
#ifdef DEBUG_TCP
    {
        char ipa_buff[IPSTR_SIZE];
        iptoa(&ip, ipa_buff);
        TCPLOG("yTcpOpenBasic %p(socket) [dst=%s(%s):%d %dms]\n", newskt, host, ipa_buff, port, mstimeout);
    }
#endif
    return yTcpOpenBasicEx(newskt, &ip, port, mstimeout, errmsg);
}

void yTcpCloseBasic(YSOCKET skt)
{
    // cleanup
    yclosesocket(skt);
}

#if 1
// check it a socket is still valid and empty (ie: nothing to read and writable)
// return 1 if the socket is valid or a error code
int yTcpCheckSocketStillValidBasic(YSOCKET skt, char *errmsg)
{
    int iResult, res;
    fd_set readfds, writefds, exceptfds;
    struct timeval timeout;

    // Send an initial buffer
#ifndef WINDOWS_API
retry:
#endif
    memset(&timeout, 0, sizeof(timeout));
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_ZERO(&exceptfds);
    FD_SET(skt, &readfds);
    FD_SET(skt, &writefds);
    FD_SET(skt, &exceptfds);
    res = select((int)skt + 1, &readfds, &writefds, &exceptfds, &timeout);
    if (res < 0) {
#ifndef WINDOWS_API
        if(SOCK_ERR == EAGAIN || SOCK_ERR == EINTR){
            goto retry;
        } else
#endif
        {
            res = yNetSetErr();
            yTcpCloseBasic(skt);
            return res;
        }
    }
    if (FD_ISSET(skt, &exceptfds)) {
        yTcpCloseBasic(skt);
        return YERRMSG(YAPI_IO_ERROR, "Exception on socket");
    }
    if (!FD_ISSET(skt, &writefds)) {
        yTcpCloseBasic(skt);
        return YERRMSG(YAPI_IO_ERROR, "Socket not ready for write");
    }

    if (FD_ISSET(skt, &readfds)) {
        char buffer[128];
        iResult = (int)yrecv(skt, buffer, sizeof(buffer), 0);
        if (iResult == 0) {
            yTcpCloseBasic(skt);
            return YERR(YAPI_NO_MORE_DATA);
        }
        if (iResult < 0) {
            yTcpCloseBasic(skt);
            return YERR(YAPI_IO_ERROR);
        } else {
            yTcpCloseBasic(skt);
            return YERR(YAPI_DOUBLE_ACCES);
        }
    }
    return 1;
}
#endif

int yTcpWriteBasic(YSOCKET skt, const u8 *buffer, int len, char *errmsg)
{
    int res;
    int tosend = len;
    const u8 *p = buffer;
    while (tosend > 0) {
        res = (int)ysend(skt, (const char*)p, tosend, SEND_NOSIGPIPE);
        if (res == SOCKET_ERROR) {
#ifdef WINDOWS_API
            if (SOCK_ERR != WSAEWOULDBLOCK)
#else
            if(SOCK_ERR != EAGAIN || SOCK_ERR == EINTR)

#endif
            {
                return yNetSetErr();
            }
        } else {
            tosend -= res;
            p += res;
            // unable to send all data
            // wait a bit with a select
            if (tosend != res) {
                struct timeval timeout;
                fd_set fds;
                memset(&timeout, 0, sizeof(timeout));
                // Upload of large files (external firmware updates) may need
                // a long time to process (on OSX: seen more than 40 seconds !)
                timeout.tv_sec = 60;
                FD_ZERO(&fds);
                FD_SET(skt, &fds);
                res = select((int)skt + 1,NULL, &fds,NULL, &timeout);
                if (res < 0) {
#ifndef WINDOWS_API
                    if(SOCK_ERR == EAGAIN || SOCK_ERR == EINTR){
                        continue;
                    } else
#endif
                    {
                        return yNetSetErr();
                    }
                } else if (res == 0) {
                    return YERRMSG(YAPI_TIMEOUT, "Timeout during TCP write");
                }
            }
        }
    }
    return len;
}


int yTcpReadBasic(YSOCKET skt, u8 *buffer, int len, char *errmsg)
{
    int iResult = (int)yrecv(skt, (char*)buffer, len, 0);
    if (iResult == 0) {
        return YERR(YAPI_NO_MORE_DATA);
    } else if (iResult < 0) {
#ifdef WINDOWS_API
        if (SOCK_ERR == WSAEWOULDBLOCK) {
            return 0;
        }
#else
        if (SOCK_ERR == EAGAIN || SOCK_ERR == EINTR) {
            return 0;
        }
#endif
        return yNetSetErr();
    }
    return iResult;
}

u32 yTcpGetRcvBufSizeBasic(YSOCKET skt)
{
    u32 winsize;
    socklen_t optlen = sizeof(winsize);
    if (getsockopt(skt,SOL_SOCKET,SO_RCVBUF, (u8*)&winsize, &optlen) < 0) {
        return 0;
    }
    return winsize;
}

/*
 *  yTCP Multi socket API these function can be dispatched on secure socket
 *  or basic socket
 */

//#define DEBUG_MULTI_TCP
#ifdef DEBUG_MULTI_TCP
#define MTCPLOG  dbglog
#else
#if defined(_MSC_VER)
#if (_MSC_VER > MSC_VS2003)
#define MTCPLOG(fmt,...)
#else
__forceinline void __MTCPLOG(fmt,...){}
#define MTCPLOG __MTCPLOG
#endif
#else
#define MTCPLOG(fmt,args...)
#endif
#endif


int yTcpInitMulti(char *errmsg)
{
    int res;
    MTCPLOG("MTCP: Init\n");
    res = yTcpInitBasic(errmsg);
#ifndef NO_YSSL
    if (!YISERR(res)) {
        res = yTcpInitSSL(errmsg);
    }
#endif
    return res;
}

int yTcpOpenMulti(YSOCKET_MULTI *newskt, const char *host, u16 port, int useSSL, u64 mstimeout, char *errmsg)
{
    YSOCKET_MULTI tmpsock;
    MTCPLOG("MTCP: Open %p [dst=%s:%d %dms]\n", newskt, host, port, mstimeout);
    tmpsock = yMalloc(sizeof(YSOCKET_MULTI_ST));
    memset(tmpsock, 0, sizeof(YSOCKET_MULTI_ST));
#ifdef DUMP_YSOCKET_MULTI_TRAFFIC
    tmpsock->creation_tm = yapiGetTickCount();
    dump_socket(tmpsock, 1, "MTCP: New socket (Open)", NULL, 0);
#endif

    if (!useSSL) {
        int res = yTcpOpenBasic(&tmpsock->basic, host, port, mstimeout, errmsg);
        if (res < 0) {
            yFree(tmpsock);
        } else {
            *newskt = tmpsock;
        }
        return res;
    } else {
#ifndef NO_YSSL
        int skip_cert_validation = useSSL > 1 ? 1 : 0;
        int res = yTcpOpenSSL(&tmpsock->secure, host, port, skip_cert_validation, mstimeout, errmsg);
        if (res < 0) {
            yFree(tmpsock);
        } else {
            tmpsock->secure_socket = 1;
            *newskt = tmpsock;
        }
        return res;
#else
        return YERRMSG(YAPI_NOT_SUPPORTED, "SSL support is not activated.");
#endif
    }
}

int yTcpAcceptMulti(YSOCKET_MULTI *newskt, YSOCKET sock, int useSSL, char *errmsg)
{
    YSOCKET_MULTI tmpsock;
    MTCPLOG("MTCP: Accept %p [sock=%d]\n", newskt, sock);
    tmpsock = yMalloc(sizeof(YSOCKET_MULTI_ST));
    memset(tmpsock, 0, sizeof(YSOCKET_MULTI_ST));
#ifdef DUMP_YSOCKET_MULTI_TRAFFIC
    tmpsock->creation_tm = yapiGetTickCount();
    dump_socket(tmpsock, 1, "MTCP: New socket (Accept)", NULL, 0);
#endif
    if (!useSSL) {
        tmpsock->basic = sock;
        *newskt = tmpsock;
        return YAPI_SUCCESS;
    } else {
#ifndef NO_YSSL
        int res = yTcpAcceptSSL(&tmpsock->secure, sock, errmsg);
        if (res < 0) {
            yFree(tmpsock);
        } else {
            tmpsock->secure_socket = 1;
            *newskt = tmpsock;
        }
        return res;
#else
        return YERRMSG(YAPI_NOT_SUPPORTED, "SSL support is not activated.");
#endif
    }
}

void yTcpCloseMulti(YSOCKET_MULTI skt)
{
    int is_secure = 0;
    if (skt) {
        is_secure = skt->secure_socket;
    }
    MTCPLOG("MTCP: Close %p (%s)\n", skt, is_secure ?"secure":"basic");
#ifdef DUMP_YSOCKET_MULTI_TRAFFIC
    dump_socket(skt, 0, "close Socket", NULL, 0);
#endif

    if (skt == NULL) {
        return;
    }
    if (!is_secure) {
        yTcpCloseBasic(skt->basic);
    }
#ifndef NO_YSSL
    else {
        yTcpCloseSSL(skt->secure);
    }
#endif
    yFree(skt);
}

YSOCKET yTcpFdSetMulti(YSOCKET_MULTI skt, void *set, YSOCKET sktmax)
{
    //MTCPLOG("MTCP: FD_SET %p (%s)\n", skt, skt->secure_socket?"secure":"basic");
    //dump_socket(skt, 0, "yTcpFdSetMulti", NULL, 0);

    YASSERT(skt != NULL, 0);
    if (!skt->secure_socket) {
        FD_SET(skt->basic, (fd_set*)set);
        if (skt->basic > sktmax) {
            sktmax = skt->basic;
        }
        return sktmax;
    } else {
#ifndef NO_YSSL
        return yTcpFdSetSSL(skt->secure, set, sktmax);
#else
        return sktmax;
#endif
    }
}

int yTcpFdIsSetMulti(YSOCKET_MULTI skt, void *set)
{
    int res;
    YASSERT(skt != NULL, 0);

    if (!skt->secure_socket) {
        res = FD_ISSET(skt->basic, (fd_set*)set);
    } else {
#ifndef NO_YSSL
        res = yTcpFdIsSetSSL(skt->secure, set);
#else
        res = 0;
#endif
    }
#ifdef DUMP_YSOCKET_MULTI_TRAFFIC
    //dbglog("MTCP: FD_ISSET %p->%d (%s)\n", skt,res, skt->secure_socket?"secure":"basic");
    {
        char buffer[512];
        YSPRINTF(buffer, 512, "MTCP: FD_ISSET %p->%d (%s)\n", skt, res, skt->secure_socket ? "secure" : "basic");
        dump_socket(skt, 0, buffer, NULL, 0);
    }
#endif
    return res;
}

int yTcpCheckSocketStillValidMulti(YSOCKET_MULTI skt, char *errmsg)
{
    //MTCPLOG("MTCP: Test still valid %p (%s)\n", skt, skt->secure_socket?"secure":"basic");
    YASSERT(skt != NULL, 0);
#ifdef DUMP_YSOCKET_MULTI_TRAFFIC
    dump_socket(skt, 0, "yTcpCheckSocketStillValidMulti", NULL, 0);
#endif
    if (!skt->secure_socket) {
        return yTcpCheckSocketStillValidBasic(skt->basic, errmsg);
    } else {
#ifndef NO_YSSL
        return yTcpCheckSocketStillValidSSL(skt->secure, errmsg);
#else
        return YERRMSG(YAPI_NOT_SUPPORTED, "SSL support is not activated.");
#endif
    }
}

int yTcpReadMulti(YSOCKET_MULTI skt, u8 *buffer, int len, char *errmsg)
{
    int res;
    YASSERT(skt != NULL, 0);
    if (!skt->secure_socket) {
        res = yTcpReadBasic(skt->basic, buffer, len, errmsg);
    } else {
#ifndef NO_YSSL
        res = yTcpReadSSL(skt->secure, buffer, len, errmsg);
#else
        res = YERRMSG(YAPI_NOT_SUPPORTED, "SSL support is not activated.");
#endif
    }
    MTCPLOG("MTCP: Read %d->%d bytes (sock=%p %s)\n", len, res, skt, skt->secure_socket?"secure":"basic");
#ifdef DUMP_YSOCKET_MULTI_TRAFFIC
    dump_socket(skt, 0, "yTcpReadMulti", buffer, res);
#endif
    return res;
}

u32 yTcpGetRcvBufSizeMulti(YSOCKET_MULTI skt)
{
    u32 winsize;
    YASSERT(skt != NULL, 0);
#ifdef DUMP_YSOCKET_MULTI_TRAFFIC
    dump_socket(skt, 0, "yTcpGetRcvBufSizeMulti", NULL, 0);
#endif
    if (!skt->secure_socket) {
        return yTcpGetRcvBufSizeBasic(skt->basic);
    } else {
#ifndef NO_YSSL
        winsize = yTcpGetRcvBufSizeSSL(skt->secure);
#else
        winsize = 0;
#endif
    }
    return winsize;
}

int yTcpWriteMulti(YSOCKET_MULTI skt, const u8 *buffer, int len, char *errmsg)
{
    YASSERT(skt != NULL, 0);
    MTCPLOG("MTCP: write %d bytes (sock=%p %s)\n", len, skt, skt->secure_socket?"secure":"basic");
#ifdef DUMP_YSOCKET_MULTI_TRAFFIC
    dump_socket(skt, 0, "yTcpWriteMulti", buffer, len);
#endif

    if (!skt->secure_socket) {
        return yTcpWriteBasic(skt->basic, buffer, len, errmsg);
    } else {
#ifndef NO_YSSL
        return yTcpWriteSSL(skt->secure, buffer, len, errmsg);
#else
        return YERRMSG(YAPI_NOT_SUPPORTED, "SSL support is not activated.");
#endif
    }
}

void yTcpShutdownMulti(void)
{
    MTCPLOG("MTCP: shutdown\n");
#ifndef NO_YSSL
    yTcpShutdownSSL();
#endif
    yTcpShutdownBasic();
}


static int yTcpDownloadEx(const char *url, const char *default_host, int default_port, int default_usessl, u8 **out_buffer, u32 mstimeout, char *errmsg)
{
    int len, domlen;
    const char *end, *p;
    const char *pos, *posplus;
    int use_ssl = default_usessl;
    const char *host = default_host;
    const char *path = NULL;
    int portno = default_port;

    if (YSTRNCMP(url, "http://", 7) == 0) {
        url += 7;
        use_ssl = 0;
        portno = DEFAULT_HTTP_PORT;
    } else if (YSTRNCMP(url, "https://", 8) == 0) {
        url += 8;
        use_ssl = 1;
        portno = DEFAULT_HTTPS_PORT;
    }
    // search for any authentication info
    p = url;
    while (*p && *p != '@' && *p != '/') {
        p++;
    }
    if (*p == '@') {
        url = ++p;
    }
    end = url + strlen(url);
    p = strchr(url, '/');
    if (p) {
        len = (int)(end - p);
        if (len > 1) {
            path = ystrndup_s(p, len);
        }
        end = p;
    }
    pos = strchr(url, ':');
    posplus = pos + 1;
    if (pos && pos < end) {
        len = (int)(end - posplus);
        if (len < 7) {
            char buffer[8];
            memcpy(buffer, posplus, len);
            buffer[len] = '\0';
            portno = atoi(buffer);
        }
        end = pos;
    }
    domlen = (int)(end - url);
    host = ystrndup_s(url, domlen);
#if 0
    dbglog("URL: yTcpDownloadEx %s %s:%d %s\n", use_ssl ?"https":"http", host, portno, path);
#endif
    return yTcpDownload(host, portno, use_ssl, path, out_buffer, mstimeout, errmsg);
}

int yTcpDownload(const char *host, int port, int usessl, const char *url, u8 **out_buffer, u32 mstimeout, char *errmsg)
{
    YSOCKET_MULTI skt;
    int res, len, nread;
    char request[512];
    u8 *replybuf = yMalloc(512);
    int replybufsize = 512;
    int replysize = 0;
    fd_set fds;
    u64 expiration;
    int open_res;
    expiration = yapiGetTickCount() + mstimeout;
    open_res = yTcpOpenMulti(&skt, host, port, usessl, mstimeout, errmsg);
    if (open_res < 0) {
        yFree(replybuf);
        return open_res;
    }
    len = YSPRINTF(request, 512, "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n"
                   "Accept-Encoding:\r\nUser-Agent: Yoctopuce\r\n\r\n", url, host);
    //write header
    res = yTcpWriteMulti(skt, (u8*)request, len, errmsg);
    if (YISERR(res)) {
        goto exit;
    }
    while (expiration - yapiGetTickCount() > 0) {
        struct timeval timeout;
        int max;
        u64 ms = expiration - yapiGetTickCount();
        memset(&timeout, 0, sizeof(timeout));
        timeout.tv_sec = (long)ms / 1000;
        timeout.tv_usec = (int)(ms % 1000) * 1000;
        /* wait for data */
        FD_ZERO(&fds);
        max = (int)yTcpFdSetMulti(skt, &fds, 0);
        //FD_SET(skt,&fds);
        res = select(max + 1, &fds,NULL,NULL, &timeout);
        if (res < 0) {
#ifndef WINDOWS_API
            if(SOCK_ERR == EAGAIN || SOCK_ERR == EINTR){
                continue;
            } else
#endif
            {
                res = yNetSetErr();
                goto exit;
            }
        }
        if (replysize + 256 >= replybufsize) {
            // need to grow receive buffer
            int newsize = replybufsize << 1;
            u8 *newbuf = (u8*)yMalloc(newsize);
            if (replybuf) {
                memcpy(newbuf, replybuf, replysize);
                yFree(replybuf);
            }
            replybuf = newbuf;
            replybufsize = newsize;
        }
        nread = yTcpReadMulti(skt, replybuf + replysize, replybufsize - replysize, errmsg);
        if (nread < 0) {
            // any connection closed by peer ends up with YAPI_NO_MORE_DATA
            if (nread == YAPI_NO_MORE_DATA) {
                res = replysize;
            } else {
                res = nread;
            }
            goto exit;
        } else {
            replysize += nread;
        }
    }
    res = YERR(YAPI_TIMEOUT);

exit:
    yTcpCloseMulti(skt);

    if (res < 0) {
        yFree(replybuf);
    } else {
        *out_buffer = replybuf;
        if (YSTRNCMP((char*)replybuf, "HTTP/1.1 200", 12) == 0) {
            // check if we need to decode chunks encoding
            int data_ofs = ymemfind(replybuf, res, (u8*)"\r\n\r\n", 4);
            if (data_ofs > 0) {
                u8 *p = replybuf;
                u8 *d = p + data_ofs;
                char buffer[128];
                char *pt;
                const char *ept = buffer + 128;
                char c = '\0';
                int decode_chunk = 0;
                while (p < d) {
                    pt = buffer;
                    while (p < d && pt < ept && (c = *p++) != ':' && c != '\r' && c != '\n') {
                        if (c != ' ') {
                            *pt++ = c;
                        }
                    }
                    if (p >= d) {
                        break;
                    }
                    *pt = 0;
                    if (c == ':') {
                        int parse_val = 0;
                        p++;
                        if (YSTRICMP(buffer, "Transfer-Encoding") == 0) {
                            parse_val = 1;
                        }
                        pt = buffer;
                        while (p < d && pt < ept && (c = *p++) != '\r' && c != '\n') {
                            if (c != ' ') {
                                *pt++ = c;
                            }
                        }
                        *pt = 0;
                        if (parse_val) {
                            if (YSTRICMP(buffer, "chunked") == 0) {
                                decode_chunk = 1;
                                break;
                            }
                        }
                    }
                }
                if (decode_chunk) {
                    u8 *newdata = yMalloc(res);
                    u8 *w = newdata;
                    u32 chunklen;
                    data_ofs += 4;
                    memcpy(w, replybuf, data_ofs);
                    w += data_ofs;
                    p = replybuf + data_ofs;
                    d = replybuf + res;
                    do {
                        int nbdigit = 0;
                        pt = buffer;
                        while (p < d && pt < ept && (c = *p++) != '\n') {
                            if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')) {
                                *pt++ = c;
                                nbdigit++;
                            }
                        }
                        *pt = 0;
                        chunklen = decodeHex(buffer, nbdigit);
                        if (chunklen) {
                            memcpy(w, p, chunklen);
                            p += chunklen;
                        }
                    } while (chunklen);
                    *out_buffer = newdata;
                    yFree(replybuf);
                }
            }
        } else if (replysize > 12 && YSTRNCMP((char*)replybuf, "HTTP/1.1 30", 11) == 0) {
            int end_header = ymemfind(replybuf, res, (u8*)"\r\n\r\n", 4);
            int loc_ofs = ymemfind(replybuf, res, (u8*)"Location:", 9);
            if (loc_ofs > 0 && end_header > 0 && loc_ofs < replysize && end_header < replysize && loc_ofs < end_header) {
                int data_ofs = loc_ofs + 9;
                int end_line_ofs = ymemfind(replybuf + data_ofs, end_header - data_ofs, (u8*)"\r\n", 2);
                if (end_line_ofs > 0) {
                    char redirect_buff[512];
                    char *d = redirect_buff;
                    char *start = (char*)replybuf + data_ofs;
                    char *end = (char*)replybuf + data_ofs + end_line_ofs;
                    //left trim
                    while (*start == ' ' && start < end) {
                        start++;
                    }
                    while (*start != ' ' && start < end && (start - end) < 511) {
                        *d++ = *start++;
                    }
                    *d = 0;
                    if (expiration > yapiGetTickCount()) {
                        u32 remaining_ms = (u32)(expiration - yapiGetTickCount());
                        return yTcpDownloadEx(redirect_buff, host, port, usessl, out_buffer, remaining_ms, errmsg);
                    } else {
                        return YERR(YAPI_TIMEOUT);
                    }
                }
            }
        }
    }
    return res;
}


static int yTcpCheckReqTimeout(struct _RequestSt *req, char *errmsg)
{
    if (req->timeout_tm != 0) {
        u64 now = yapiGetTickCount();
        u64 duration = now - req->open_tm;
        u64 last_io = (req->write_tm > req->read_tm ? req->write_tm : req->read_tm);
        u64 idle_durration = now - last_io;

        if (idle_durration < YIO_IDLE_TCP_TIMEOUT) {
            return YAPI_SUCCESS;
        }
#ifdef DEBUG_SLOW_TCP
        else {
            u64 last_wr = now - req->write_tm;
            u64 last_rd = now - req->read_tm;

            dbglog("Long Idle TCP request %p = %"FMTu64"ms total = %"FMTu64"ms (read=%"FMTu64"ms write=%"FMTu64")\n",
                req, idle_durration, duration, last_rd, last_wr);
        }
#endif
        if (duration > req->timeout_tm) {
            req->errcode = YAPI_TIMEOUT;
            YSPRINTF(req->errmsg, YOCTO_ERRMSG_LEN, "TCP request took too long (%dms)", duration);
            return YERRMSG(YAPI_TIMEOUT, req->errmsg);
        }
#ifdef DEBUG_SLOW_TCP
        else {
            if (duration > (req->timeout_tm - (req->timeout_tm / 4))) {
                dbglog("Slow TCP request %p = %dms\n",req,duration);
                dbglog("req = %s\n",req->headerbuf);
            }
        }
#endif

    }
    return YAPI_SUCCESS;
}


static int copyHostHeader(char *dst, int dstsize, const char *hostname, char *errmsg)
{
    const char *field = "Host: ";
    int field_len = YSTRLEN(field);
    if (dstsize < field_len) {
        return YERR(YAPI_IO_ERROR);
    }
    YSTRCPY(dst, dstsize, field);
    dst += field_len;
    dstsize -= field_len;

    if (dstsize < YSTRLEN(hostname)) {
        return YERR(YAPI_IO_ERROR);
    }
    YSTRCPY(dst, dstsize, hostname);
    dst += YSTRLEN(hostname);
    dstsize -= YSTRLEN(hostname);

    if (dstsize < HTTP_crlf_len) {
        return YERR(YAPI_IO_ERROR);
    }
    YSTRCPY(dst, dstsize, HTTP_crlf);
    return field_len + YSTRLEN(hostname) + HTTP_crlf_len;
}

/********************************************************************************
* HTTP request functions (HTTP request that DO NOT use WebSocket)
*******************************************************************************/


// access mutex taken by caller
static int yHTTPOpenReqEx(struct _RequestSt *req, u64 mstimout, char *errmsg)
{
    char *p, *last;
    char first_line[4096];
    int avail = sizeof(first_line);
    char *d;
    int res;
    const char *contentType = "\r\nContent-Type";
    int contentTypeLen = (int)strlen(contentType);
    const char *multipart = "multipart/form-data";
    int multipartLen = (int)strlen(multipart);
    const char *xupload = "x-upload";
    int xuploadLen = (int)strlen(xupload);
    int use_ssl;

    YASSERT(req->proto == PROTO_HTTP || req->proto == PROTO_SECURE_HTTP, req->proto);
    TCPLOG("yTcpOpenReqEx %p [%x:%x %d]\n", req, req->http.skt, req->http.reuseskt, mstimout);

    req->replypos = -1; // not ready to consume until header found
    req->replysize = 0;
    memset(req->replybuf, 0, req->replybufsize);
    req->errcode = YAPI_SUCCESS;


    if (req->http.reuseskt != INVALID_SOCKET_MULTI && (res = yTcpCheckSocketStillValidMulti(req->http.reuseskt, NULL)) == 1) {
        req->http.skt = req->http.reuseskt;
        req->http.reuseskt = INVALID_SOCKET_MULTI;
    } else {
        req->http.reuseskt = INVALID_SOCKET_MULTI;
        use_ssl = req->proto == PROTO_SECURE_HTTP;
        if (use_ssl && req->hub->info.serial[0] && req->hub->info.has_unsecure_open_port) {
            // if in info.json we have a non TLS port we can skip certificate validation
            use_ssl = 2;
        }
        res = yTcpOpenMulti(&req->http.skt, req->hub->url.host, req->hub->url.portno, use_ssl, mstimout, errmsg);
        if (YISERR(res)) {
            // yTcpOpen has reset the socket to INVALID
            yTcpCloseMulti(req->http.skt);
            req->http.skt = INVALID_SOCKET_MULTI;
            TCPLOG("yTcpOpenReqEx error %p [%x]\n", req, req->http.skt);
            return res;
        }
    }


    // we need to parse the first line and format it correctly

    //copy method (GET or POST) 
    d = first_line;
    p = req->headerbuf;
    while (avail && *p && *p != ' ') {
        *d++ = *p++;
        avail--;
    }

    if (avail) {
        *d++ = ' ';
        avail--;
    }

    // add potential subdomain
    if (req->hub->url.subdomain[0]) {
        int sub_len = YSTRLEN(req->hub->url.subdomain);
        if (sub_len + 1 < avail) {
            memcpy(d, req->hub->url.subdomain, sub_len);
            avail -= sub_len;
            d += sub_len;
        }
    }

    //copy request (/api.json....)
    p++; //skip space between 
    while (avail && *p && *p != ' ' && *p != '\r') {
        *d++ = *p++;
        avail--;
    }

    if (avail) {
        *d++ = ' ';
        avail--;
    }

    if (req->hub->info.use_pure_http && avail >= 8) {
        memcpy(d, "HTTP/1.1", 8);
        avail -= 8;
        d += 8;
    }

    // skip end of first line
    while (*p && *p != '\r') p++;
    last = p;
    // Search for Content-Type header: it must be preserved as it may countain a boundary
    // 
    // VirtualHub-4web quirk: we have to switch from "multipart/form-data" to "x-upload"
    // to bypass PHP own processing of uploads. The exact value has anyway always be 
    // ignored by VirtualHub and YoctoHubs, as long as a boundary is defined.
    while (*p == '\r' && *(p + 1) == '\n' && *(p + 2) != '\r') {
        p += 2;
        while (*p && *p != '\r') p++;
        if (YSTRNCMP(last, contentType, contentTypeLen) == 0) {
            unsigned len = (unsigned)(p - last);
            if ((unsigned)avail > len) {
                // there is enough space to insert header, check if we need to change it
                char *v = last + contentTypeLen;
                while (v < p && *v != ':') v++;
                v++;
                while (v < p && *v == ' ') v++;
                len = (unsigned)(v - last);
                memcpy(d, last, len);
                d += len;
                avail -= len;
                if (YSTRNCMP(v, multipart, multipartLen) == 0) {
                    // replace multipart/form-data by x-upload
                    v += multipartLen;
                    memcpy(d, xupload, xuploadLen);
                    d += xuploadLen;
                    avail -= xuploadLen;
                }
                len = (unsigned)(p - v);
                memcpy(d, v, len);
                d += len;
                avail -= len;
            }
        }
        last = p;
    }
    // VirtualHub-4web quirk: insert content-length if needed (often required by PHP)
    if (req->bodysize > 0) {
        char contentLength[40];
        int contentLengthLen;
        YSPRINTF(contentLength, sizeof(contentLength), "\r\nContent-Length: %d", req->bodysize);
        contentLengthLen = (int)strlen(contentLength);
        if (avail >= contentLengthLen) {
            memcpy(d, contentLength, contentLengthLen);
            d += contentLengthLen;
            avail -= contentLengthLen;
        }
    }
    if (avail >= 2) {
        *d++ = '\r';
        *d++ = '\n';
        avail -= 2;
    }
    // insert authorization header in needed
    yEnterCriticalSection(&req->hub->access);
    if (req->hub->url.user && req->hub->http.s_realm) {
        int auth_len;
        char method[8];
        char uri[4096];
        const char *s;
        char *m = method, *u = uri;
        // null-terminate method and URI for digest computation
        for (s = first_line; *s != ' ' && (m - method) < (sizeof(method) - 1); s++) {
            *m++ = *s;
        }
        *m = 0;
        s++;
        for (; *s != ' ' && (u - uri) < (sizeof(uri) - 1); s++) {
            *u++ = *s;
        }
        *u = 0;
        auth_len = yDigestAuthorization(d, avail, req->hub->url.user, req->hub->http.s_realm, req->hub->http.s_ha1,
                                        req->hub->http.s_nonce, req->hub->http.s_opaque, &req->hub->http.nc, method, uri);
        d += auth_len;
        avail -= auth_len;
    }
    yLeaveCriticalSection(&req->hub->access);
    res = copyHostHeader(d, avail, req->hub->url.host, errmsg);
    if (YISERR(res)) {
        yTcpCloseMulti(req->http.skt);
        req->http.skt = INVALID_SOCKET_MULTI;
        return res;
    }
    d += res;
    if (req->flags & TCPREQ_KEEPALIVE) {
        YSTRCPY(d, avail, "\r\n");
    } else {
        YSTRCPY(d, avail, "Connection: close\r\n\r\n");
    }
    //write header
    res = yTcpWriteMulti(req->http.skt, (const u8*)first_line, (int)strlen(first_line), errmsg);
    if (YISERR(res)) {
        yTcpCloseMulti(req->http.skt);
        req->http.skt = INVALID_SOCKET_MULTI;
        return res;
    }
    if (req->bodysize > 0) {
        //write body
        res = yTcpWriteMulti(req->http.skt, (u8*)req->bodybuf, req->bodysize, errmsg);
        if (YISERR(res)) {
            yTcpCloseMulti(req->http.skt);
            req->http.skt = INVALID_SOCKET_MULTI;
            TCPLOG("yTcpOpenReqEx write failed for Req %p[%x]\n", req, req->http.skt);
            return res;
        }
    }
    req->write_tm = yapiGetTickCount();

    if (req->hub->wuce.listensock != INVALID_SOCKET) {
        return yDringWakeUpSocket(&req->hub->wuce, 1, errmsg);
    } else {
        return YAPI_SUCCESS;
    }
}


static void yHTTPCloseReqEx(struct _RequestSt *req, int canReuseSocket)
{
    TCPLOG("yHTTPCloseReqEx %p[%d]\n", req, canReuseSocket);

    // mutex already taken by caller
    req->flags &= ~TCPREQ_KEEPALIVE;
    if (req->callback) {
        u32 len = req->replysize - req->replypos;
        u8 *ptr = req->replybuf + req->replypos;
        if (req->errcode == YAPI_NO_MORE_DATA) {
            req->callback(req->context, ptr, len, YAPI_SUCCESS, "");
        } else {
            req->callback(req->context, ptr, len, req->errcode, req->errmsg);
        }
        req->callback = NULL;
        // ASYNC Request are automatically released
        req->flags &= ~TCPREQ_IN_USE;
    }

    if (req->http.skt != INVALID_SOCKET_MULTI) {
        if (canReuseSocket) {
            req->http.reuseskt = req->http.skt;
        } else {
            yTcpCloseMulti(req->http.skt);
        }
        req->http.skt = INVALID_SOCKET_MULTI;
    }
    ySetEvent(&req->finished);
}


static int yHTTPMultiSelectReq(struct _RequestSt **reqs, int size, u64 ms, WakeUpSocket *wuce, char *errmsg)
{
    fd_set fds;
    struct timeval timeout;
    int res, i;
    YSOCKET sktmax = 0;

    memset(&timeout, 0, sizeof(timeout));
    timeout.tv_sec = (long)ms / 1000;
    timeout.tv_usec = (int)(ms % 1000) * 1000;
    /* wait for data */
    //dbglog("select %p\n", reqs);


    FD_ZERO(&fds);
    if (wuce) {
        //dbglog("listensock %p %d\n", reqs, wuce->listensock);
        FD_SET(wuce->listensock, &fds);
        sktmax = wuce->listensock;
    }
    for (i = 0; i < size; i++) {
        struct _RequestSt *req;
        req = reqs[i];
        YASSERT(req->proto == PROTO_HTTP || req->proto == PROTO_SECURE_HTTP, req->proto);
        if (req->http.skt == INVALID_SOCKET_MULTI) {
            return YERR(YAPI_INVALID_ARGUMENT);
        } else {
            //dbglog("sock %p %p:%p\n", reqs, req, req->http.skt);
            sktmax = yTcpFdSetMulti(req->http.skt, &fds, sktmax);
        }
    }
    if (sktmax == 0) {
        return YAPI_SUCCESS;
    }
    res = select((int)sktmax + 1, &fds, NULL, NULL, &timeout);
    if (res < 0) {
#ifndef WINDOWS_API
        if(SOCK_ERR == EAGAIN || SOCK_ERR == EINTR){
            return 0;
        } else
#endif
        {
            res = yNetSetErr();
            for (i = 0; i < size; i++) {
                TCPLOG("yHTTPSelectReq %p[%X] (%s)\n", reqs[i], reqs[i]->http.skt, errmsg);
            }
            return res;
        }
    }
    if (res >= 0) {
        if (wuce && FD_ISSET(wuce->listensock, &fds)) {
            YPROPERR(yConsumeWakeUpSocket(wuce, errmsg));
        }
        for (i = 0; i < size; i++) {
            struct _RequestSt *req;
            req = reqs[i];
            if (yTcpFdIsSetMulti(req->http.skt, &fds)) {
                yEnterCriticalSection(&req->access);
                if (req->replysize >= req->replybufsize - 256) {
                    // need to grow receive buffer
                    int newsize = req->replybufsize << 1;
                    u8 *newbuf = (u8*)yMalloc(newsize);
                    memcpy(newbuf, req->replybuf, req->replysize);
                    yFree(req->replybuf);
                    req->replybuf = newbuf;
                    req->replybufsize = newsize;
                }
                res = yTcpReadMulti(req->http.skt, req->replybuf + req->replysize, req->replybufsize - req->replysize, errmsg);
                req->read_tm = yapiGetTickCount();
                if (res < 0) {
                    // any connection closed by peer ends up with YAPI_NO_MORE_DATA
                    req->replypos = 0;
                    req->errcode = YERRTO((YRETCODE) res, req->errmsg);
                    TCPLOG("yHTTPSelectReq %p[%x] connection closed by peer\n", req, req->http.skt);
                    yHTTPCloseReqEx(req, 0);
                } else if (res > 0) {
                    req->replysize += res;
                    if (req->replypos < 0) {
                        // Need to analyze http headers
                        if (req->replysize == 8 && !memcmp(req->replybuf, "0K\r\n\r\n\r\n", 8)) {
                            TCPLOG("yHTTPSelectReq %p[%x] ultrashort reply\n", req, req->http.skt);
                            // successful abbreviated reply (keepalive)
                            req->replypos = 0;
                            req->replybuf[0] = 'O';
                            req->errcode = YERRTO(YAPI_NO_MORE_DATA, req->errmsg);
                            yHTTPCloseReqEx(req, 1);
                        } else if (req->replysize >= 4 && !memcmp(req->replybuf, "OK\r\n", 4)) {
                            // successful short reply, let it go through
                            req->replypos = 0;
                        } else if (req->replysize >= 12) {
                            if (memcmp(req->replybuf, "HTTP/1.1 401", 12) != 0) {
                                // no authentication required, let it go through
                                req->replypos = 0;
                            } else {
                                // authentication required, process authentication headers
                                char *method = NULL, *realm = NULL, *qop = NULL, *nonce = NULL, *opaque = NULL;
                                if (!req->hub->url.user || req->retryCount++ > 3) {
                                    // No credential provided, give up immediately
                                    req->replypos = 0;
                                    req->replysize = 0;
                                    req->errcode = YERRTO(YAPI_UNAUTHORIZED, req->errmsg);
                                    yHTTPCloseReqEx(req, 0);
                                } else if (yParseWWWAuthenticate((char*)req->replybuf, req->replysize, &method, &realm, &qop, &nonce, &opaque) >= 0) {
                                    // Authentication header fully received, we can close the connection
                                    if (!strcmp(method, "Digest") && !strcmp(qop, "auth")) {
                                        // partial close to reopen with authentication settings
                                        yTcpCloseMulti(req->http.skt);
                                        req->http.skt = INVALID_SOCKET_MULTI;
                                        // device requests Digest qop-authentication, good
                                        yEnterCriticalSection(&req->hub->access);
                                        yDupSet(&req->hub->http.s_realm, realm);
                                        yDupSet(&req->hub->http.s_nonce, nonce);
                                        yDupSet(&req->hub->http.s_opaque, opaque);
                                        if (req->hub->url.user && req->hub->url.password) {
                                            ComputeAuthHA1(req->hub->http.s_ha1, req->hub->url.user, req->hub->url.password, req->hub->http.s_realm);
                                        }
                                        req->hub->http.nc = 0;
                                        yLeaveCriticalSection(&req->hub->access);
                                        // reopen connection with proper auth parameters
                                        // callback and context parameters are preserved
                                        req->errcode = yHTTPOpenReqEx(req, req->timeout_tm, req->errmsg);
                                        if (YISERR(req->errcode)) {
                                            yHTTPCloseReqEx(req, 0);
                                        }
                                    } else {
                                        // unsupported authentication method for devices, give up
                                        req->replypos = 0;
                                        req->errcode = YERRTO(YAPI_UNAUTHORIZED, req->errmsg);
                                        yHTTPCloseReqEx(req, 0);
                                    }
                                }
                            }
                        }
                    }
                    if (req->errcode == YAPI_SUCCESS) {
                        req->errcode = yTcpCheckReqTimeout(req, req->errmsg);
                    }
                }
                yLeaveCriticalSection(&req->access);
            }
        }
    }

    return YAPI_SUCCESS;
}


int ySocketOpenBindMulti(YSOCKET_MULTI *newskt, IPvX_ADDR *local_ip, int is_udp, int sin6_scope_id, u16 port, u16 sockFlags, char *errmsg)
{
    int res, sockfamily, addrlen;
    u32 optval;
    YSOCKET sock;
    u8 addr[sizeof(struct sockaddr_storage)];
    memset(addr, 0, sizeof(struct sockaddr_storage));
#if defined(DEBUG_MULTI_TCP) || defined(DEBUG_SOCKET_USAGE)
    char buff[IPSTR_SIZE];
#endif

    if (sockFlags & YSOCKFLAG_IPV6) {
        struct sockaddr_in6 *addr_v6 = (struct sockaddr_in6*)&addr;
        sockfamily = PF_INET6;
        addr_v6->sin6_family = AF_INET6;
        if (local_ip) {
            memcpy(&addr_v6->sin6_addr, local_ip->v6.addr, sizeof(addr_v6->sin6_addr));
            addr_v6->sin6_scope_id = sin6_scope_id;
        } else {
            addr_v6->sin6_addr = in6addr_any;
        }
        addr_v6->sin6_port = htons(port);
        addrlen = sizeof(struct sockaddr_in6);
    } else {
        struct sockaddr_in *addr_v4 = (struct sockaddr_in*)&addr;
        sockfamily = PF_INET;
        addr_v4->sin_family = AF_INET;
        if (local_ip) {
            addr_v4->sin_addr.s_addr = local_ip->v4.addr.Val;
        } else {
            addr_v4->sin_addr.s_addr = INADDR_ANY;
        }
        addr_v4->sin_port = htons(port);
        addrlen = sizeof(struct sockaddr_in);
    }
    if (is_udp) {
        sock = ysocket(sockfamily, SOCK_DGRAM, IPPROTO_UDP);
    }else {
        sock = ysocket(sockfamily, SOCK_STREAM, IPPROTO_TCP);
    }
    if (sock == INVALID_SOCKET) {
        return yNetLogErr();
    }

    optval = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&optval, sizeof(optval)) < 0) {
        res = yNetLogErr();
        closesocket(sock);
        return res;
    }
#ifdef SO_REUSEPORT
    if (sockFlags & YSOCKFLAG_SO_REUSEPORT) {
        setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, (char *)&optval, sizeof(optval));
    }
#endif

    if (sockFlags & YSOCKFLAG_SO_BROADCAST) {
        setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char*)&optval, sizeof(optval));
    }
    if (ybind(sock, (struct sockaddr*)&addr, addrlen) < 0) {
        res = yNetLogErr();
        closesocket(sock);
        return res;
    }
#ifdef DEBUG_SOCKET_USAGE
    {
        iptoa(local_ip, buff);
        dbglog("udp bind socket %d to %s port=%d flags=%d\n", sock, buff, port, sockFlags);
    }
#endif

    *newskt = yMalloc(sizeof(YSOCKET_MULTI_ST));
    memset(*newskt, 0, sizeof(YSOCKET_MULTI_ST));
    (*newskt)->basic = sock;
    if (sockFlags & YSOCKFLAG_IPV6) {
        (*newskt)->ipv6 = 1;
    }
#ifdef DEBUG_MULTI_TCP
    iptoa(local_ip, buff);
    MTCPLOG("MUdp: Open %p [addr=%s port=%d]\n", *newskt, buff, port);
#endif
    return YAPI_SUCCESS;
}

int yUdpWriteMulti(YSOCKET_MULTI skt, IPvX_ADDR *dest_ip, u16 dest_port, const u8 *buffer, int len, char *errmsg)
{
    int sent, socklen;
    struct sockaddr_storage storage;
    struct sockaddr_in6 *addr_ipv6 = (struct sockaddr_in6*)&storage;
    struct sockaddr_in *addr_ipv4 = (struct sockaddr_in*)&storage;
    memset(&storage, 0, sizeof(struct sockaddr_storage));
    if (skt->ipv6) {
        socklen = sizeof(struct sockaddr_in6);
        memset(addr_ipv6, 0, socklen);
        addr_ipv6->sin6_family = AF_INET6;
        addr_ipv6->sin6_port = htons(dest_port);
        memcpy(&addr_ipv6->sin6_addr, &dest_ip->v6.addr, sizeof(addr_ipv6->sin6_addr));
        sent = (int)ysendto(skt->basic, (const char*) buffer, len, 0, (struct sockaddr*)&storage, socklen);
    } else {
        socklen = sizeof(struct sockaddr_in);
        memset(&storage, 0, socklen);
        addr_ipv4->sin_family = AF_INET;
        addr_ipv4->sin_port = htons(dest_port);
        addr_ipv4->sin_addr.s_addr = dest_ip->v4.addr.Val;
        sent = (int)ysendto(skt->basic, (const char*) buffer, len, 0, (struct sockaddr*)&storage, socklen);
    }
    if (sent < 0) {
        return yNetSetErr();
    }
    return sent;
}

int yUdpReadMulti(YSOCKET_MULTI skt, u8 *buffer, int len, IPvX_ADDR *ip_out, u16 *port_out, char *errmsg)
{

    struct sockaddr_storage storage;
    socklen_t sockaddr_remote_size;
    IPvX_ADDR remote_ip;
    u16 remote_port;

    sockaddr_remote_size = sizeof(struct sockaddr_in6);
    // received packet
    int received = (int)yrecvfrom(skt->basic, (char*) buffer, len, 0, (struct sockaddr*) &storage, &sockaddr_remote_size);
    if (received > 0) {

        if (sockaddr_remote_size == sizeof(struct sockaddr_in6)) {
            struct sockaddr_in6 *addr_ipv6 = (struct sockaddr_in6*)&storage;
            memcpy(&remote_ip.v6.addr, &addr_ipv6->sin6_addr, sizeof(addr_ipv6->sin6_addr));
            remote_port = ntohs(addr_ipv6->sin6_port);
        } else {
            struct sockaddr_in *addr_ipv4 = (struct sockaddr_in*)&storage;
            setIPv4Val(&remote_ip, addr_ipv4->sin_addr.s_addr);
            remote_port = ntohs(addr_ipv4->sin_port);
        }
        if (ip_out) {
            *ip_out = remote_ip;
        }
        if (port_out) {
            *port_out = remote_port;
        }
    } else if (received < 0) {
        return yNetSetErr();
    }
    return received;
}

#ifndef IPV6_ADD_MEMBERSHIP
#define IPV6_ADD_MEMBERSHIP IPV6_JOIN_GROUP
#endif

int yUdpRegisterMCAST(YSOCKET_MULTI skt, IPvX_ADDR *mcastAddr, int interfaceNo)
{
    int res = YAPI_SUCCESS;
    if (skt->ipv6 == 0) {
        struct ip_mreq mcast_membership;
        memset(&mcast_membership, 0, sizeof(mcast_membership));
        mcast_membership.imr_multiaddr.s_addr = mcastAddr->v4.addr.Val;
        mcast_membership.imr_interface.s_addr = INADDR_ANY;
        if (setsockopt(skt->basic, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void*)&mcast_membership, sizeof(mcast_membership)) < 0) {
            res = yNetLogErr();
        }
    } else {
        struct ipv6_mreq mcast_membership6;
        memset(&mcast_membership6, 0, sizeof(mcast_membership6));
        memcpy(&mcast_membership6.ipv6mr_multiaddr, mcastAddr, sizeof(mcast_membership6.ipv6mr_multiaddr));
        mcast_membership6.ipv6mr_interface = interfaceNo;
        if (setsockopt(skt->basic, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, (void*)&mcast_membership6, sizeof(mcast_membership6)) < 0) {
            res = yNetLogErr();
        }
    }
    return res;
}

/********************************************************************************
* WebSocket implementation for generic requests
*******************************************************************************/
#if 0
static void dumpReqQueue(const char * msg, HubSt* hub, int tcpchan)
{
    struct _RequestSt* req;
    char buffer[1024];
    req = hub->ws.chan[tcpchan].requests;
    YSPRINTF(buffer,2048,"dmp_%s(%d):", msg, tcpchan);
    dbglog("%s\n", buffer);
    while (req != NULL ) {
        char sbuffer[512];
        YSPRINTF(sbuffer,512," %p(%d:%d %d->%d)", req, req->ws.state, req->ws.asyncId, req->ws.requestpos,req->ws.requestsize);
        dbglog("%s\n", sbuffer);
        req = req->ws.next;
    }
}

static void dumpbin(char *out, u8* buffer, int pktlen)
{
    int i;
    for (i = 0; i  < pktlen ;i++)
    {
        u8 val = *buffer++;
        if (val <32 ) {
            val = ' ';
        } else if (val > 126) {
            val = '~';
        }
        *out++ = val;
    }
}
#endif


#define MAX_QUEUE_SIZE 16

static int yWSOpenReqEx(struct _RequestSt *req, int tcpchan, u64 mstimeout, char *errmsg)
{
    HubSt *hub = req->hub;
    RequestSt *r;
    int headlen;
    u8 *p;
    int count = 0;
    u64 start = yapiGetTickCount();
    YASSERT(req->proto == PROTO_LEGACY || req->proto == PROTO_WEBSOCKET || req->proto == PROTO_SECURE_WEBSOCKET, req->proto);
    memset(&req->ws, 0, sizeof(WSReqSt));
    // merge first line and header
    headlen = YSTRLEN(req->headerbuf);
    req->ws.requestsize = headlen + (req->bodysize ? req->bodysize : 4);
    req->ws.requestbuf = yMalloc(req->ws.requestsize);
    p = req->ws.requestbuf;
    memcpy(p, req->headerbuf, headlen);
    p += headlen;
    //todo: create request buffer more efficiently
    if (req->bodysize) {
        memcpy(p, req->bodybuf, req->bodysize);
    } else {
        memcpy(p, "\r\n\r\n", 4);
    }
    req->ws.channel = tcpchan;
    req->timeout_tm = mstimeout;
    req->ws.state = REQ_OPEN;
    YASSERT(tcpchan < MAX_ASYNC_TCPCHAN, tcpchan);

retry:
    if (start + mstimeout < yapiGetTickCount()) {
        return YERRMSG(YAPI_IO_ERROR, "Unable to queue request (WebSocket)");
    }

    if (req->hub->ws.base_state != WS_BASE_CONNECTED) {
        if (req->hub->mandatory && req->hub->state < NET_HUB_TOCLOSE) {
            // mandatory hub retry until timeout unless we are trying to close the connection
            yApproximateSleep(500);
            goto retry;
        }
        return YERRMSG(YAPI_IO_ERROR, "Hub is not ready (WebSocket)");
    }

    if (count) {
        yApproximateSleep(100);
    }

    yEnterCriticalSection(&hub->ws.chan[tcpchan].access);
    if (req->callback) {
        yEnterCriticalSection(&hub->access);
        req->ws.asyncId = hub->ws.s_next_async_id++;
        if (hub->ws.s_next_async_id >= 127) {
            hub->ws.s_next_async_id = 48;
        }
        yLeaveCriticalSection(&hub->access);
    }
    req->ws.next = NULL; // just in case
    if (hub->ws.chan[tcpchan].requests) {
        count = 0;
        r = hub->ws.chan[tcpchan].requests;
        while (r->ws.next && count < MAX_QUEUE_SIZE) {
            r = r->ws.next;
            count++;
        }
        if (count == MAX_QUEUE_SIZE && r->ws.next) {
            //too many request in queue sleep a bit
            yLeaveCriticalSection(&hub->ws.chan[tcpchan].access);
            goto retry;
        }

        r->ws.next = req;
    } else {
        hub->ws.chan[tcpchan].requests = req;
    }
#if 0
    WSLOG("req(%s:%p): open req chan=%d timeout=%dms asyncId=%d\n", req->hub->name, req, tcpchan, (int)mstimeout, req->ws.asyncId);
#if 0
    dumpReqQueue("open", hub, tcpchan);
    {
        char dump_buf[512];
        int len = req->ws.requestsize >=512 ? 511:req->ws.requestsize;
        dumpbin(dump_buf,req->ws.requestbuf,len);
        dump_buf[len] = 0;
        WSLOG("uop(%p):%s\n", req, dump_buf);
    }
#endif
#endif
    yLeaveCriticalSection(&hub->ws.chan[tcpchan].access);
    req->write_tm = yapiGetTickCount();
    return yDringWakeUpSocket(&hub->wuce, 1, errmsg);
}


static int yWSSelectReq(struct _RequestSt *req, u64 mstimeout, char *errmsg)
{
    int done = yWaitForEvent(&req->finished, (int)mstimeout);

    REQLOG("ws_req:%p: select for %d ms %d\n", req, (int)mstimeout, done);

    if (done) {
        req->errcode = YAPI_NO_MORE_DATA;
    }
    return YAPI_SUCCESS;
}


static void yWSCloseReq(struct _RequestSt *req)
{
    u32 len;
    u8 *ptr;

#ifdef DEBUG_WEBSOCKET
    u64 duration;
    duration = yapiGetTickCount() - req->open_tm;
    WSLOG("req(%s:%p) close req after %"FMTu64"ms (%"FMTu64"ms) with %d bytes errcode = %d\n", req->hub->url.org_url, req, duration, (req->write_tm - req->open_tm), req->replysize, req->errcode);
#endif

    YASSERT(req->proto == PROTO_LEGACY || req->proto == PROTO_WEBSOCKET || req->proto == PROTO_SECURE_WEBSOCKET, req->proto);
    if (req->callback) {
        // async close
        len = req->replysize - req->replypos;
        ptr = req->replybuf + req->replypos;
        if (req->errcode == YAPI_NO_MORE_DATA) {
            req->callback(req->context, ptr, len, YAPI_SUCCESS, "");
        } else {
            req->callback(req->context, ptr, len, req->errcode, req->errmsg);
        }
        req->callback = NULL;
    }
    if (req->ws.first_write_tm && (req->ws.state == REQ_OPEN || req->ws.state == REQ_CLOSED_BY_HUB)) {
        int res;
        req->ws.flags |= WS_FLG_NEED_API_CLOSE;
        yLeaveCriticalSection(&req->access);
        // release lock to let the other thread signal the end of the request
        res = yWaitForEvent(&req->finished, 5000);
        yEnterCriticalSection(&req->access);
        if (!res) {
            dbglog("hub(%s) websocket close without ack\n", req->hub->url.host);
        }
    }
    req->ws.state = REQ_CLOSED_BY_BOTH;
}


static void yWSRemoveReq(struct _RequestSt *req)
{
    HubSt *hub = req->hub;
    RequestSt *r, *p;
    int tcpchan;

#ifdef DEBUG_WEBSOCKET
    u64 duration;
    duration = yapiGetTickCount() - req->open_tm;
    WSLOG("req(%s:%p) remove req after %"FMTu64"ms (%"FMTu64"ms) with %d bytes errcode = %d\n", req->hub->url.org_url, req, duration, (req->write_tm - req->open_tm), req->replysize, req->errcode);
#endif
    tcpchan = req->ws.channel;
    YASSERT(tcpchan < MAX_ASYNC_TCPCHAN, tcpchan);

    yEnterCriticalSection(&hub->ws.chan[tcpchan].access);
    r = hub->ws.chan[tcpchan].requests;
    p = NULL;
    while (r && r != req) {
        p = r;
        r = r->ws.next;
    }
    YASSERT(r, 0);
    if (r) {
        if (p == NULL) {
            hub->ws.chan[tcpchan].requests = r->ws.next;
        } else {
            p->ws.next = r->ws.next;
        }
    }
    yLeaveCriticalSection(&hub->ws.chan[tcpchan].access);
}

/********************************************************************************
* Generic Request functions (HTTP or WS)
*******************************************************************************/

#ifdef TRACE_TCP_REQ

static void dumpTCPReq(const char *fileid, int lineno, struct _RequestSt *req)
{
    int w;
    int has_cs  =yTryEnterCriticalSection(&req->access);
    const char *proto;
    const char *state;

    dbglog("dump TCPReq %p from %s:%d\n", req, fileid, lineno);
    if (req->hub){
        dbglog("Hub: %s\n", req->hub->name);
    } else{
        dbglog("Hub: null\n");
    }


    switch (req->ws.state) {
        case REQ_CLOSED_BY_BOTH:
            state ="state=REQ_CLOSED_BY_BOTH";
            break;
        case REQ_OPEN:
            state ="state=REQ_OPEN";
            break;
        case REQ_CLOSED_BY_HUB:
            state ="state=REQ_CLOSED_BY_HUB";
            break;
        case REQ_CLOSED_BY_API:
            state ="state=REQ_CLOSED_BY_API";
            break;
        case REQ_ERROR:
            state ="state=REQ_ERROR";
            break;
        default:
            state ="state=??";
            break;
    }

    dbglog("%s retcode=%d (retrycount=%d) errmsg=%s\n", state, req->errcode, req->retryCount, req->errmsg);
    switch(req->proto){
        case PROTO_LEGACY: proto ="PROTO_LEGACY"; break;
        case PROTO_AUTO: proto = "PROTO_AUTO"; break;
        case PROTO_SECURE: proto = "PROTO_SECURE"; break;
        case PROTO_HTTP: proto ="PROTO_HTTP"; break;
        case PROTO_WEBSOCKET: proto ="PROTO_WEBSOCKET"; break;
        case PROTO_SERCURE_HTTP: proto ="PROTO_SERCURE_HTTP"; break;
        case PROTO_SERCURE_WEBSOCKET: proto ="PROTO_SERCURE_WEBSOCKET"; break;
        default: proto ="unk"; break;
    }
    dbglog("proto=%s socket=%x (reuse=%x) flags=%x\n", proto, req->http.skt, req->http.reuseskt, req->flags);
    dbglog("time open=%"FMTx64" last read=%"FMTx64" last write=%"FMTx64"  timeout=%"FMTx64"\n", req->open_tm, req->read_tm, req->write_tm, req->timeout_tm);
    dbglog("read=%d (readpos=%d)\n", req->replysize, req->replysize);
    dbglog("callback=%p context=%p\n", req->callback, req->context);
    if (req->headerbuf){
        dbglog("req[%s]\n", req->headerbuf);
    } else {
        dbglog("null\n");
    }
    w = yWaitForEvent(&req->finished, 0);
    dbglog("finished=%d\n", w);
    if (has_cs) {
        yLeaveCriticalSection(&req->access);
    }

}
#endif


struct _RequestSt* yReqAlloc(struct _HubSt *hub)
{
    struct _RequestSt *req = yMalloc(sizeof(struct _RequestSt));
    memset(req, 0, sizeof(struct _RequestSt));
    req->proto = hub->url.proto;
    TCPLOG("yTcpInitReq %p[%s:%x]\n", req, hub->url.host, req->proto);
    req->replybufsize = 1500;
    req->replybuf = (u8*)yMalloc(req->replybufsize);
    yInitializeCriticalSection(&req->access);
    yCreateManualEvent(&req->finished, 1);
    req->hub = hub;
    switch (req->proto) {
    case PROTO_HTTP:
    case PROTO_SECURE_HTTP:
        req->http.reuseskt = INVALID_SOCKET_MULTI;
        req->http.skt = INVALID_SOCKET_MULTI;
        break;
    default:
        break;
    }
    return req;
}


int yReqOpen(struct _RequestSt *req, int wait_for_start, int tcpchan, const char *request, int reqlen, u64 mstimeout, yapiRequestAsyncCallback callback, void *context, RequestProgress progress_cb, void *progress_ctx, char *errmsg)
{
    int minlen, i, res;
    u64 startwait;

    YPERF_TCP_ENTER(TCPOpenReq);
    if (wait_for_start <= 0) {
        yEnterCriticalSection(&req->access);
        if (req->flags & TCPREQ_IN_USE) {
            yLeaveCriticalSection(&req->access);
            return YERR(YAPI_DEVICE_BUSY);
        }
    } else {
        YPERF_TCP_ENTER(TCPOpenReq_wait);
        yEnterCriticalSection(&req->access);
        startwait = yapiGetTickCount();
        while (req->flags & TCPREQ_IN_USE) {
            u64 duration;
            // There is an ongoing request to be finished
            yLeaveCriticalSection(&req->access);
            duration = yapiGetTickCount() - startwait;
            if (duration > wait_for_start) {
                dbglog("Last request in not finished after %"FMTu64" ms\n", duration);
#ifdef TRACE_TCP_REQ
                dumpTCPReq(__FILENAME__, __LINE__, req);
#endif
                return YERRMSG(YAPI_TIMEOUT, "last TCP request is not finished");
            }
            yWaitForEvent(&req->finished, 100);
            yEnterCriticalSection(&req->access);
        }
        YPERF_TCP_LEAVE(TCPOpenReq_wait);
    }


    req->flags = 0;
    if (request[0] == 'G' && request[1] == 'E' && request[2] == 'T') {
        //for GET request discard all except the first line
        for (i = 0; i < reqlen; i++) {
            if (request[i] == '\r') {
                reqlen = i;
                break;
            }
        }
        if (i > 3 && !req->hub->info.use_pure_http) {
            if (request[i - 3] == '&' && request[i - 2] == '.' && request[i - 1] == ' ') {
                req->flags |= TCPREQ_KEEPALIVE;
            }
        }
        req->bodysize = 0;
    } else {
        const char *p = request;
        int bodylen = reqlen - 4;

        while (bodylen > 0 && (p[0] != '\r' || p[1] != '\n' ||
            p[2] != '\r' || p[3] != '\n')) {
            p++;
            bodylen--;
        }
        p += 4;
        reqlen = (int)(p - request);
        // Build a request body buffer
        if (req->bodybufsize < bodylen) {
            if (req->bodybuf)
                yFree(req->bodybuf);
            req->bodybufsize = bodylen + (bodylen >> 1);
            req->bodybuf = (char*)yMalloc(req->bodybufsize);
        }
        memcpy(req->bodybuf, p, bodylen);
        req->bodysize = bodylen;
    }
    // Build a request buffer with at least a terminal NUL but
    // include space for Connection: close and Authorization: headers
    minlen = reqlen + 500;
    if (req->headerbufsize < minlen) {
        if (req->headerbuf)
            yFree(req->headerbuf);
        req->headerbufsize = minlen + (reqlen >> 1);
        req->headerbuf = (char*)yMalloc(req->headerbufsize);
    }
    memcpy(req->headerbuf, request, reqlen);
    req->headerbuf[reqlen] = 0;
    req->retryCount = 0;
    req->callback = callback;
    req->context = context;
    req->progressCb = progress_cb;
    req->progressCtx = progress_ctx;
    req->read_tm = req->write_tm = req->open_tm = yapiGetTickCount();
    req->timeout_tm = mstimeout;

    // Really build and send the request
    if (req->proto == PROTO_HTTP || req->proto == PROTO_SECURE_HTTP) {
        res = yHTTPOpenReqEx(req, mstimeout, errmsg);
    } else {
        res = yWSOpenReqEx(req, tcpchan, mstimeout, errmsg);
    }
    if (res == YAPI_SUCCESS) {
        req->errmsg[0] = '\0';
        req->flags |= TCPREQ_IN_USE;
        yResetEvent(&req->finished);
    }

    yLeaveCriticalSection(&req->access);

    YPERF_TCP_LEAVE(TCPOpenReq);
    return res;
}

int yReqSelect(struct _RequestSt *tcpreq, u64 ms, char *errmsg)
{
    if (tcpreq->proto == PROTO_HTTP || tcpreq->proto == PROTO_SECURE_HTTP) {
        return yHTTPMultiSelectReq(&tcpreq, 1, ms, NULL, errmsg);
    } else {
        return yWSSelectReq(tcpreq, ms, errmsg);
    }
}

int yReqMultiSelect(struct _RequestSt **tcpreq, int size, u64 ms, WakeUpSocket *wuce, char *errmsg)
{
    // multi select make no sense in WebSocket since all data come from the same socket
    return yHTTPMultiSelectReq(tcpreq, size, ms, wuce, errmsg);
}


int yReqIsEof(struct _RequestSt *req, char *errmsg)
{
    int res;
    yEnterCriticalSection(&req->access);
    if (req->errcode == YAPI_NO_MORE_DATA) {
        res = 1;
    } else if (req->errcode == 0) {
        res = req->errcode = yTcpCheckReqTimeout(req, errmsg);
    } else if (req->errcode == YAPI_UNAUTHORIZED) {
        res = YERRMSG((YRETCODE) req->errcode, "Access denied, authorization required");
    } else {
        res = YERRMSG((YRETCODE) req->errcode, req->errmsg);
    }
    yLeaveCriticalSection(&req->access);
    return res;
}


int yReqGet(struct _RequestSt *req, u8 **buffer)
{
    int avail;

    yEnterCriticalSection(&req->access);
    yTcpCheckReqTimeout(req, req->errmsg);
    if (req->replypos < 0) {
        // data is not yet ready to consume (still processing header)
        avail = 0;
    } else {
        avail = req->replysize - req->replypos;
        if (buffer) {
            *buffer = req->replybuf + req->replypos;
        }
    }
    yLeaveCriticalSection(&req->access);

    return avail;
}


int yReqRead(struct _RequestSt *req, u8 *buffer, int len)
{
    int avail;

    yEnterCriticalSection(&req->access);
    yTcpCheckReqTimeout(req, req->errmsg);
    if (req->replypos < 0) {
        // data is not yet ready to consume (still processing header)
        len = 0;
    } else {
        avail = req->replysize - req->replypos;
        if (len > avail) {
            len = avail;
        }
        if (len && buffer) {
            memcpy(buffer, req->replybuf + req->replypos, len);
        }
        if (req->replypos + len == req->replysize) {
            req->replypos = 0;
            req->replysize = 0;
            if (req->proto != PROTO_HTTP && req->proto != PROTO_SECURE_HTTP) {
                if (req->ws.state == REQ_CLOSED_BY_BOTH) {
                    req->errcode = YAPI_NO_MORE_DATA;
                }
            }

        } else {
            req->replypos += len;
        }
    }
    yLeaveCriticalSection(&req->access);

    return len;
}


void yReqClose(struct _RequestSt *req)
{
    TCPLOG("yTcpCloseReq %p\n", req);
#if 0
    {
        u64 now = yapiGetTickCount();
        u64 duration = now - req->open_tm;
        u64 last_wr = req->write_tm - req->open_tm;
        u64 last_rd = req->read_tm - req->open_tm;

        dbglog("request %p  total=%"FMTu64"ms (read=%"FMTu64"ms write=%"FMTu64")\n",
            req, duration, last_rd, last_wr);
    }
#endif
    yEnterCriticalSection(&req->access);
    if (req->flags & TCPREQ_IN_USE) {

        if (req->proto == PROTO_HTTP || req->proto == PROTO_SECURE_HTTP) {
            yHTTPCloseReqEx(req, 0);
        } else {
#if 0
            u64 last = req->ws.last_write_tm - req->open_tm;
            u64 first = req->ws.first_write_tm - req->open_tm;

            dbglog("request.ws %p first_write=%"FMTu64"ms last_write=%"FMTu64")\n",
                req, first, last);
#endif
            yWSCloseReq(req);
        }
        req->flags &= ~TCPREQ_IN_USE;
    }
    yLeaveCriticalSection(&req->access);
    if (req->proto != PROTO_HTTP && req->proto != PROTO_SECURE_HTTP) {
        yWSRemoveReq(req);
    }
}


int yReqIsAsync(struct _RequestSt *req)
{
    int res;
    yEnterCriticalSection(&req->access);
    res = (req->flags & TCPREQ_IN_USE) && (req->callback != NULL);
    yLeaveCriticalSection(&req->access);
    return res;
}


void yReqFree(struct _RequestSt *req)
{
    TCPLOG("yTcpFreeReq %p\n", req);
    if (req->proto == PROTO_HTTP || req->proto == PROTO_SECURE_HTTP) {
        if (req->http.skt != INVALID_SOCKET_MULTI) {
            yTcpCloseMulti(req->http.skt);
        }
        if (req->http.reuseskt != INVALID_SOCKET_MULTI) {
            yTcpCloseMulti(req->http.reuseskt);
        }
    } else {
        if (req->ws.requestbuf)
            yFree(req->ws.requestbuf);
    }
    if (req->headerbuf)
        yFree(req->headerbuf);
    if (req->bodybuf)
        yFree(req->bodybuf);
    if (req->replybuf)
        yFree(req->replybuf);
    yCloseEvent(&req->finished);
    yDeleteCriticalSection(&req->access);
    yFree(req);
    //memset(req, 0, sizeof(struct _RequestSt));
}


int yReqHasPending(struct _HubSt *hub)
{
    int i;
    RequestSt *req = NULL;

    if (hub->url.proto == PROTO_HTTP || hub->url.proto == PROTO_SECURE_HTTP) {
        for (i = 0; i < ALLOC_YDX_PER_HUB; i++) {
            req = yContext->tcpreq[i];
            if (req && yReqIsAsync(req)) {
                return 1;
            }
        }
    } else {
        int tcpchan;
        for (tcpchan = 0; tcpchan < MAX_ASYNC_TCPCHAN; tcpchan++) {
            yEnterCriticalSection(&hub->ws.chan[tcpchan].access);
            if (hub->ws.chan[tcpchan].requests) {
                req = hub->ws.chan[tcpchan].requests;
                while (req && req->ws.requestsize == req->ws.requestpos && req->ws.state == REQ_CLOSED_BY_BOTH) {
                    req = req->ws.next;
                }
                if (req != NULL) {
                    //dbglog("still request pending on hub %s (%p)\n", hub->name, req);
                    yLeaveCriticalSection(&hub->ws.chan[tcpchan].access);
                    return 1;
                }
            }
            yLeaveCriticalSection(&hub->ws.chan[tcpchan].access);
        }
    }
    return 0;
}


/********************************************************************************
* WebSocket functions
*******************************************************************************/

static const char *ws_header_start = " HTTP/1.1\r\nSec-WebSocket-Version: 13\r\nUser-Agent: Yoctopuce\r\nSec-WebSocket-Key: ";
static const char *ws_header_end = "\r\nConnection: keep-alive, Upgrade\r\nUpgrade: websocket\r\nHost: ";

#define YRand32() rand()

/*****************************************************************************
Function:
WORD Base64Encode(BYTE* cSourceData, WORD wSourceLen,
BYTE* cDestData, WORD wDestLen)

Description:
Encodes a binary array to Base-64.

Precondition:
None

Parameters:
cSourceData - Pointer to a string of binary data
wSourceLen  - Length of the binary source data
cDestData   - Pointer to write the Base-64 encoded data
wDestLen    - Maximum length that can be written to cDestData

Returns:
Number of encoded bytes written to cDestData.  This will always be
a multiple of 4.

Remarks:
Encoding cannot be performed in-place.  If cSourceData overlaps with
cDestData, the behavior is undefined.

Encoded data is always at least 1/3 larger than the source data.  It may
be 1 or 2 bytes larger than that.
***************************************************************************/
static u16 Base64Encode(const u8 *cSourceData, u16 wSourceLen, u8 *cDestData, u16 wDestLen)
{
    u8 i, j;
    u8 vOutput[4];
    u16 wOutputLen;

    wOutputLen = 0;
    while (wDestLen >= 4u) {
        // Start out treating the output as all padding
        vOutput[0] = 0xFF;
        vOutput[1] = 0xFF;
        vOutput[2] = 0xFF;
        vOutput[3] = 0xFF;

        // Get 3 input octets and split them into 4 output hextets (6-bits each)
        if (wSourceLen == 0u)
            break;
        i = *cSourceData++;
        wSourceLen--;
        vOutput[0] = (i & 0xFC) >> 2;
        vOutput[1] = (i & 0x03) << 4;
        if (wSourceLen) {
            i = *cSourceData++;
            wSourceLen--;
            vOutput[1] |= (i & 0xF0) >> 4;
            vOutput[2] = (i & 0x0F) << 2;
            if (wSourceLen) {
                i = *cSourceData++;
                wSourceLen--;
                vOutput[2] |= (i & 0xC0) >> 6;
                vOutput[3] = i & 0x3F;
            }
        }

        // Convert hextets into Base 64 alphabet and store result
        for (i = 0; i < 4u; i++) {
            j = vOutput[i];

            if (j <= 25u)
                j += 'A' - 0;
            else if (j <= 51u)
                j += 'a' - 26;
            else if (j <= 61u)
                j += '0' - 52;
            else if (j == 62u)
                j = '+';
            else if (j == 63u)
                j = '/';
            else // Padding
                j = '=';

            *cDestData++ = j;
        }

        // Update counters
        wDestLen -= 4;
        wOutputLen += 4;
    }

    return wOutputLen;
}


/********************************************************************************
* WebSocket internal function
*******************************************************************************/


//todo: factorize  GenereateWebSockeyKey + VerifyWebsocketKey

// compute a new nonce for http request
// the buffer passed as argument must be at least 28 bytes long
static int GenereateWebSockeyKey(const u8 *url, u32 urllen, char *buffer)
{
    u32 salt[2];
    HASH_SUM ctx;
    u8 rawbuff[16];

    // Our nonce is base64_encoded [ MD5( Rand32,(ytime^Rand), ) ]
    salt[0] = YRand32();
    salt[1] = yapiGetTickCount() & 0xff;
    MD5Initialize(&ctx);
    MD5AddData(&ctx, (u8*)salt, 2);
    MD5AddData(&ctx, url, urllen);
    MD5Calculate(&ctx, rawbuff);
    return Base64Encode(rawbuff, 16, (u8*)buffer, 28);
}


static int VerifyWebsocketKey(const char *data, u16 hdrlen, const char *reference, u16 reference_len)
{
    u8 buf[80];
    const char *magic = YOCTO_WEBSOCKET_MAGIC;
    u8 *sha1;

    // compute correct key
    if (hdrlen >= sizeof(buf)) {
#ifndef MICROCHIP_API
        dbglog("Bad WebSocket header (%d)\n", hdrlen);
#else
        ylog("WS!");
#endif
        return 0;
    }
    memcpy(buf, reference, reference_len);
#ifdef USE_FAR_YFSTR
    apiGetStr(magic.hofs, (char*)buf + CbCtx.websocketkey.len);
#else
    memcpy(buf + reference_len, magic, YOCTO_WEBSOCKET_MAGIC_LEN + 1);
#endif
    sha1 = ySHA1((char*)buf);
    Base64Encode(sha1, 20, buf, 80);
    if (memcmp(buf, data, hdrlen) == 0) {
        return 1;
    }
    return 0;
}


#define WS_CONNEXION_TIMEOUT 10000
#define WS_MAX_DATA_LEN  124


/*
*   send WebSocket frame for a hub
*/
static int ws_sendFrame(HubSt *hub, int stream, int tcpchan, const u8 *data, int datalen, char *errmsg)
{
    u32 buffer_32[33];
    u32 mask;
    int i;
    WSStreamHead strym;
    u8 *p = (u8*)buffer_32;
#ifdef DEBUG_SLOW_TCP
    u64 start = yapiGetTickCount();
#endif
    int tcp_write_res;

    YASSERT(datalen <= WS_MAX_DATA_LEN, datalen);
#ifdef DEBUG_WEBSOCKET
    // disable masking for debugging
    mask = 0;
#else
    mask = YRand32();
#endif
    // do not start at offset zero on purpose
    // we want the buffer to be aligned on u32
    p[0] = 0x82;
    p[1] = (u8)(datalen + 1) | 0x80;;
    p[2] = ((u8*)&mask)[2];
    p[3] = ((u8*)&mask)[3];

    p[4] = ((u8*)&mask)[0];
    p[5] = ((u8*)&mask)[1];
    strym.tcpchan = tcpchan;
    strym.stream = stream;
    p[6] = strym.encaps ^ p[2];
    if (datalen) {
        p[7] = *data ^ p[3];
    }
    if (datalen > 1) {
        memcpy(buffer_32 + 2, data + 1, datalen - 1);
        for (i = 0; i < (datalen - 1 + 3) >> 2; i++) {
            buffer_32[i + 2] ^= mask;
        }
    }
    TRACE_WS_SOCK_APPEND("ws_dump.txt", 0, "<--- WRITE", p, datalen + 7, hub->ws.bws_open_tm);
    tcp_write_res = yTcpWriteMulti(hub->ws.skt, p, datalen + 7, errmsg);
#ifdef DEBUG_SLOW_TCP
    u64 delta = yapiGetTickCount() - start;
    if (delta > 10) {
        dbglog("WS: yTcpWrite took %"FMTu64"ms (stream=%d chan=%d res=%d)\n", delta, strym.stream, strym.tcpchan, tcp_write_res);
    }
#endif
    return tcp_write_res;
}

/*
*   send authentication meta
*/
static int ws_sendAuthenticationMeta(HubSt *hub, char *errmsg)
{
    USB_Meta_Pkt meta_out;
    memset(&meta_out, 0, sizeof(USB_Meta_Pkt));
    meta_out.auth.metaType = USB_META_WS_AUTHENTICATION;

#if 1
    if (hub->ws.remoteVersion < USB_META_WS_PROTO_V2) {
        meta_out.auth.version = USB_META_WS_PROTO_V1;
    } else {
        meta_out.auth.version = USB_META_WS_PROTO_V2;
    }
#else
    meta_out.auth.version = USB_META_WS_PROTO_V1;
#endif
    if (hub->url.user && hub->url.password) {
        u8 ha1[16];
        meta_out.auth.flags = USB_META_WS_AUTH_FLAGS_VALID;
        meta_out.auth.nonce = INTEL_U32(hub->ws.nounce);
        ComputeAuthHA1(ha1, hub->url.user, hub->url.password, hub->info.serial);
        CheckWSAuth(hub->ws.remoteNounce, ha1, NULL, meta_out.auth.sha1);
    }
    return ws_sendFrame(hub,YSTREAM_META, 0, (const u8*)&meta_out, USB_META_WS_AUTHENTICATION_SIZE, errmsg);
}

static void ws_appendTCPData(RequestSt *req, u8 *buffer, int pktlen)
{
    if (pktlen) {
        if (req->replybufsize < req->replysize + pktlen) {
            u8 *newbuff;
            req->replybufsize <<= 1;
            newbuff = yMalloc(req->replybufsize);
            memcpy(newbuff, req->replybuf, req->replysize);
            yFree(req->replybuf);
            req->replybuf = newbuff;
        }

        memcpy(req->replybuf + req->replysize, buffer, pktlen);
        req->replysize += pktlen;
    }
    req->read_tm = yapiGetTickCount();
}
#ifdef DEBUG_WEBSOCKET
const char *ystream_dbg_label[] = {
    "YSTREAM_EMPTY",
    "YSTREAM_TCP",
    "YSTREAM_TCP_CLOSE",
    "YSTREAM_NOTICE",
    "YSTREAM_REPORT",
    "YSTREAM_META",
    "YSTREAM_REPORT_V2",
    "YSTREAM_NOTICE_V2",
    "YSTREAM_TCP_NOTIF",
    "YSTREAM_TCP_ASYNCCLOSE",
};
#endif

/*
*   ws_parseIncomingFrame parse incoming WebSocket frame
*/
static int ws_parseIncomingFrame(HubSt *hub, u8 *buffer, int pktlen, char *errmsg)
{
    WSStreamHead strym;
    RequestSt *req;
    int flags;
    const char *user;
    const char *pass;
    int maxtcpws;
    int asyncid;
#ifdef DEBUG_WEBSOCKET
    u64 reltime = yapiGetTickCount() - hub->ws.connectionTime;
#endif
    YASSERT(pktlen > 0, pktlen);
    strym.encaps = buffer[0];
    buffer++;
    pktlen--;

#if 0
    dbglog("WS: IN %s tcpchan%d len=%d\n", ystream_dbg_label[strym.stream],strym.tcpchan,pktlen);
#endif

    switch (strym.stream) {
    case YSTREAM_TCP_NOTIF:
        if (pktlen > 0) {
#if 0
        {
                FILE *f;
                //printf("%s", buffer);
                YASSERT(YFOPEN(&f, "req_trace\\api_not.txt", "ab") == 0);
                fwrite(buffer, 1, pktlen, f);
                fclose(f);
            }
#endif
            yPushFifo(&hub->not_fifo, buffer, pktlen);
            while (handleNetNotification(hub));
        }
        break;
    case YSTREAM_EMPTY:
        return YAPI_SUCCESS;
    case YSTREAM_TCP_ASYNCCLOSE:
        if (strym.tcpchan > 3) {
            dbglog("WS: Unexpected frame for tcpChan %d (TCP_ASYNCCLOSE)\n", strym.tcpchan);
            return YAPI_IO_ERROR;
        }
        yEnterCriticalSection(&hub->ws.chan[strym.tcpchan].access);
        req = hub->ws.chan[strym.tcpchan].requests;
        if (req == NULL) {
            dbglog("WS: Drop frame for closed tcpChan %d (TCP_ASYNCCLOSE/empty)\n", strym.tcpchan);
            yLeaveCriticalSection(&hub->ws.chan[strym.tcpchan].access);
            return YAPI_IO_ERROR;
        }
        while (req != NULL && (req->ws.state == REQ_CLOSED_BY_BOTH)) {
            req = req->ws.next;
        }

        yLeaveCriticalSection(&hub->ws.chan[strym.tcpchan].access);

#if 0
        {
            char dump_buf[512];
            dumpbin(dump_buf,buffer, pktlen);
            dump_buf[pktlen] = 0;
            WSLOG("use(%p):%s\n", req, dump_buf);
        }
#endif
        if (req == NULL) {
            dbglog("WS: Drop frame for closed tcpChan %d (TCP_ASYNCCLOSE)\n", strym.tcpchan);
            return YAPI_IO_ERROR;
        }
        asyncid = buffer[pktlen - 1];
        pktlen--;
        if (req->ws.asyncId != asyncid) {
            YSPRINTF(errmsg, YOCTO_ERRMSG_LEN, "WS: Incorrect async-close signature on tcpChan %d %p (%d:%d)\n",
                     strym.tcpchan, req, req->ws.asyncId, asyncid);
            //dumpReqQueue("recv",hub, strym.tcpchan);
            return YAPI_IO_ERROR;
        }
        WSLOG("req(%s:%p) close async %d\n", req->hub->url.org_url, req, req->ws.asyncId);
        yEnterCriticalSection(&req->access);
        req->ws.state = REQ_CLOSED_BY_BOTH;
        ws_appendTCPData(req, buffer, pktlen);
        yWSCloseReq(req);
        yLeaveCriticalSection(&req->access);
        ySetEvent(&req->finished);
        yWSRemoveReq(req);
    // async request are automatically closed
        yReqFree(req);
        break;
    case YSTREAM_TCP:
    case YSTREAM_TCP_CLOSE:
        if (strym.tcpchan > 3) {
            dbglog("WS: Unexpected frame for tcpChan %d (%s)\n", strym.tcpchan,
                   (strym.stream == YSTREAM_TCP_CLOSE ? "TCP_CLOSE" : "TCP"));
            return YAPI_IO_ERROR;
        }

        yEnterCriticalSection(&hub->ws.chan[strym.tcpchan].access);
        req = hub->ws.chan[strym.tcpchan].requests;
        if (req == NULL) {
            dbglog("WS: Drop frame for closed tcpChan %d (%s/empty)\n", strym.tcpchan,
                   (strym.stream == YSTREAM_TCP_CLOSE ? "TCP_CLOSE" : "TCP"));
            yLeaveCriticalSection(&hub->ws.chan[strym.tcpchan].access);
            return YAPI_IO_ERROR;
        }
        while (req != NULL && req->ws.state == REQ_CLOSED_BY_BOTH) {
            req = req->ws.next;
        }
        yLeaveCriticalSection(&hub->ws.chan[strym.tcpchan].access);
#if 0
        {
            char dump_buf[512];
            dumpbin(dump_buf,buffer, pktlen);
            dump_buf[pktlen] = 0;
            WSLOG("use(%p):%s\n", req, dump_buf);
        }
#endif
        if (req == NULL) {
            dbglog("WS: Drop frame for closed tcpChan %d (%s)\n", strym.tcpchan,
                   (strym.stream == YSTREAM_TCP_CLOSE ? "TCP_CLOSE" : "TCP"));
            return YAPI_IO_ERROR;
        }
        YASSERT(req->ws.state != REQ_CLOSED_BY_HUB, req->ws.state);
        if (strym.stream == YSTREAM_TCP_CLOSE) {
            if (req->ws.asyncId) {
                YSPRINTF(errmsg, YOCTO_ERRMSG_LEN, "WS: Synchronous close received instead of async-%d close for tcpchan %d (%p)\n",
                         req->ws.asyncId, strym.tcpchan, req);
                //dumpReqQueue("recv",hub, strym.tcpchan);
                return YAPI_IO_ERROR;
            }
            if (req->ws.state == REQ_OPEN) {
                // We could change state to REQ_CLOSED_BY_HUB, but it is not worth doing it
                // since state changes to CLOSED_BY_BOTH immediately after
                int res = ws_sendFrame(hub, YSTREAM_TCP_CLOSE, strym.tcpchan, NULL, 0, errmsg);
                if (res < 0) {
                    dbglog("WS: req(%s:%p) unable to ack remote close (%d/%s)\n", req->hub->url.host, req, res, errmsg);
                }
                yEnterCriticalSection(&req->access);
                req->ws.flags &= ~WS_FLG_NEED_API_CLOSE;
                yLeaveCriticalSection(&req->access);
            }
            WSLOG("req(%s:%p) close %d\n", req->hub->url.org_url, req, req->replysize);
            yEnterCriticalSection(&req->access);
            req->ws.state = REQ_CLOSED_BY_BOTH;
            yLeaveCriticalSection(&req->access);
        }
        ws_appendTCPData(req, buffer, pktlen);
        if (strym.stream == YSTREAM_TCP_CLOSE) {
            ySetEvent(&req->finished);
        }
        break;
    case YSTREAM_META: {
        USB_Meta_Pkt *meta = (USB_Meta_Pkt*)(buffer);
        //WSLOG("%"FMTu64": META type=%d len=%d\n",reltime, meta->announce.metaType, pktlen);
        switch (meta->announce.metaType) {
        case USB_META_WS_ANNOUNCE:
            if (meta->announce.version < USB_META_WS_PROTO_V1 || pktlen < USB_META_WS_ANNOUNCE_SIZE) {
                return YAPI_SUCCESS;
            }
            hub->ws.remoteVersion = meta->announce.version;
            hub->ws.remoteNounce = INTEL_U32(meta->announce.nonce);
            maxtcpws = INTEL_U16(meta->announce.maxtcpws);
            if (maxtcpws > 0) {
                hub->ws.tcpMaxWindowSize = maxtcpws;
            }
            YSTRCPY(hub->info.serial, YOCTO_SERIAL_LEN, meta->announce.serial);
            hub->serial_hash = yHashPutStr(meta->announce.serial);
            WSLOG("hub(%s) Announce: %s (v%d / %x)\n", hub->url.org_url, meta->announce.serial, meta->announce.version, hub->ws.remoteNounce);
            if (checkForSameHubAccess(hub, hub->serial_hash, errmsg) < 0) {
                // fatal error do not try to reconnect
                hub->state = NET_HUB_TOCLOSE;
                return YAPI_DOUBLE_ACCES;
            }
            hub->ws.nounce = YRand32();
            hub->ws.base_state = WS_BASE_AUTHENTICATING;
            hub->ws.connectionTime = yapiGetTickCount();
            return ws_sendAuthenticationMeta(hub, errmsg);
        case USB_META_WS_AUTHENTICATION:
            if (hub->ws.base_state != WS_BASE_AUTHENTICATING)
                return YAPI_SUCCESS;
            if (meta->auth.version < USB_META_WS_PROTO_V1 || (u32)pktlen < USB_META_WS_AUTHENTICATION_SIZE) {
                return YAPI_SUCCESS;
            }
            hub->ws.tcpRoundTripTime = (u32)(yapiGetTickCount() - hub->ws.connectionTime + 1);
            if (hub->ws.tcpMaxWindowSize < 2048 && hub->ws.tcpRoundTripTime < 7) {
                // Fix overly optimistic round-trip on YoctoHubs
                hub->ws.tcpRoundTripTime = 7;
            }
#ifdef DEBUG_WEBSOCKET
            {
                int uploadRate = hub->ws.tcpMaxWindowSize * 1000 / hub->ws.tcpRoundTripTime;
                dbglog("RTT=%dms, WS=%d, uploadRate=%f KB/s\n", hub->ws.tcpRoundTripTime, hub->ws.tcpMaxWindowSize, uploadRate/1000.0);
            }
#endif

            flags = meta->auth.flags;
            if ((flags & USB_META_WS_AUTH_FLAGS_RW) != 0) {
                hub->rw_access = 1;
            }
            if (hub->url.user) {
                user = hub->url.user;
            } else {
                user = "";
            }

            if (hub->url.password) {
                pass = hub->url.password;
            } else {
                pass = "";
            }
            if ((flags & USB_META_WS_AUTH_FLAGS_VALID) != 0) {
                u8 ha1[16];
                ComputeAuthHA1(ha1, user, pass, hub->info.serial);
                if (CheckWSAuth(hub->ws.nounce, ha1, meta->auth.sha1, NULL)) {
                    hub->ws.base_state = WS_BASE_CONNECTED;
                    hub->state = NET_HUB_ESTABLISHED;
                    hub->retryCount = 0;
                    hub->attemptDelay = 500;
                    hub->errcode = YAPI_SUCCESS;
                    WSLOG("hub(%s): connected as %s\n", hub->url.org_url, user);
                    log_hub_state(&hub->url, "connected", "WebSocket");
                } else {
                    YSPRINTF(errmsg, YOCTO_ERRMSG_LEN, "Authentication as %s failed (%s:%d)", user, __FILENAME__, __LINE__);
                    return YAPI_UNAUTHORIZED;
                }
            } else {
                if (hub->url.user == NULL || hub->url.password == NULL || hub->url.password[0] == 0) {
                    hub->ws.base_state = WS_BASE_CONNECTED;
                    hub->state = NET_HUB_ESTABLISHED;
                    hub->retryCount = 0;
                    hub->attemptDelay = 500;
                    hub->errcode = YAPI_SUCCESS;
                    WSLOG("hub(%s): connected\n", hub->url.org_url);
                    log_hub_state(&hub->url, "connected", "WebSocket");
                } else {
                    if (YSTRCMP(user, "admin") == 0 && !hub->rw_access) {
                        YSPRINTF(errmsg, YOCTO_ERRMSG_LEN, "Authentication as %s failed", user);
                    } else {
                        YSPRINTF(errmsg, YOCTO_ERRMSG_LEN, "Authentication error : hub has no password for %s", user);
                    }
                    return YAPI_UNAUTHORIZED;
                }
            }
            break;
        case USB_META_WS_ERROR:
            if (INTEL_U16(meta->error.htmlcode) == 401) {
                return YERR(YAPI_UNAUTHORIZED);
            } else {
                YSPRINTF(errmsg, YOCTO_ERRMSG_LEN, "Remote hub closed connection with error %d", INTEL_U16(meta->error.htmlcode));
                return YAPI_IO_ERROR;
            }
        case USB_META_ACK_UPLOAD: {
            int tcpchan = meta->uploadAck.tcpchan;
            yEnterCriticalSection(&hub->ws.chan[tcpchan].access);
            req = hub->ws.chan[tcpchan].requests;
            while (req != NULL && req->ws.state != REQ_OPEN) {
                req = req->ws.next;
            }
            yLeaveCriticalSection(&hub->ws.chan[tcpchan].access);
            if (req) {
                u32 ackBytes = meta->uploadAck.totalBytes[0] + (meta->uploadAck.totalBytes[1] << 8) + (meta->uploadAck.totalBytes[2] << 16) + (meta->uploadAck.totalBytes[3] << 24);
                u64 ackTime = yapiGetTickCount();
                if (hub->ws.chan[tcpchan].lastUploadAckTime && ackBytes > hub->ws.chan[tcpchan].lastUploadAckBytes) {
                    int deltaBytes;
                    u64 deltaTime;
                    u32 newRate;
                    hub->ws.chan[tcpchan].lastUploadAckBytes = ackBytes;
                    hub->ws.chan[tcpchan].lastUploadAckTime = ackTime;

                    deltaBytes = ackBytes - hub->ws.chan[tcpchan].lastUploadRateBytes;
                    deltaTime = ackTime - hub->ws.chan[tcpchan].lastUploadRateTime;
                    WSLOG("delta  bytes=%d  time=%"FMTu64"ms\n", deltaBytes, deltaTime);
                    if (deltaTime < 500) {
                        break; // wait more
                    }
                    if (deltaTime < 1000 && deltaBytes < 65536) {
                        break; // wait more
                    }
                    hub->ws.chan[tcpchan].lastUploadRateBytes = ackBytes;
                    hub->ws.chan[tcpchan].lastUploadRateTime = ackTime;
                    if (req->progressCb && req->ws.requestsize) {
                        req->progressCb(req->progressCtx, ackBytes, req->ws.requestsize);
                    }
                    newRate = (u32)(deltaBytes * 1000 / deltaTime);
                    hub->ws.uploadRate = (u32)(0.8 * hub->ws.uploadRate + 0.3 * newRate);
                    WSLOG("New rate: %.2f KB/s (based on %.2f KB in %.2fs)\n", hub->ws.uploadRate / 1000.0, deltaBytes / 1000.0, deltaTime / 1000.0);
                } else {
                    WSLOG("First ack received (rate=%d)\n", hub->ws.uploadRate);
                    hub->ws.chan[tcpchan].lastUploadAckBytes = ackBytes;
                    hub->ws.chan[tcpchan].lastUploadAckTime = ackTime;
                    hub->ws.chan[tcpchan].lastUploadRateBytes = ackBytes;
                    hub->ws.chan[tcpchan].lastUploadRateTime = ackTime;
                    if (req->progressCb && req->ws.requestsize) {
                        req->progressCb(req->progressCtx, ackBytes, req->ws.requestsize);
                    }
                }
            }
        }
        break;
        default:
            WSLOG("unhandled Meta pkt %d\n", meta->announce.metaType);
            break;
        }
    }
    break;
    case YSTREAM_NOTICE:
    case YSTREAM_REPORT:
    case YSTREAM_REPORT_V2:
    case YSTREAM_NOTICE_V2:
    default:
        dbglog("Invalid WS stream type (%d)\n", strym.stream);
    }
    return YAPI_SUCCESS;
}

// return 1 if there is still a request pending, 0 if all is done, -1 on error
static int ws_requestStillPending(HubSt *hub)
{
    int tcpchan;
    for (tcpchan = 0; tcpchan < MAX_ASYNC_TCPCHAN; tcpchan++) {
        RequestSt *req = NULL;
        yEnterCriticalSection(&hub->ws.chan[tcpchan].access);
        req = hub->ws.chan[tcpchan].requests;
        while (req && req->ws.state == REQ_CLOSED_BY_BOTH) {
            req = req->ws.next;
        }
        yLeaveCriticalSection(&hub->ws.chan[tcpchan].access);
        if (req) {
            return 1;
        }
    }
    return 0;
}


static RequestSt* getNextReqToSend(HubSt *hub, int tcpchan)
{
    RequestSt *req;
    yEnterCriticalSection(&hub->ws.chan[tcpchan].access);
    req = hub->ws.chan[tcpchan].requests;
    while (req) {
        if ((req->ws.flags & WS_FLG_NEED_API_CLOSE) || (req->ws.requestpos < req->ws.requestsize && req->ws.state == REQ_OPEN)) {
            yLeaveCriticalSection(&hub->ws.chan[tcpchan].access);
            return req;
        }
        if (req->ws.asyncId || req->ws.state == REQ_CLOSED_BY_BOTH) {
            req = req->ws.next;
        } else {
            yLeaveCriticalSection(&hub->ws.chan[tcpchan].access);
            return NULL;
        }
    }
    yLeaveCriticalSection(&hub->ws.chan[tcpchan].access);
    return req;
}

static RequestSt* closeAllReq(HubSt *hub, int err, const char *errmsg)
{
    RequestSt *req = NULL;

    int tcpchan;
    for (tcpchan = 0; tcpchan < MAX_ASYNC_TCPCHAN; tcpchan++) {

        yEnterCriticalSection(&hub->ws.chan[tcpchan].access);
        req = hub->ws.chan[tcpchan].requests;
        while (req) {
            yEnterCriticalSection(&req->access);
            if (req->ws.state != REQ_CLOSED_BY_BOTH) {
                req->errcode = err;
                YSTRCPY(req->errmsg, YOCTO_ERRMSG_LEN, errmsg);
                req->ws.state = REQ_CLOSED_BY_BOTH;
                ySetEvent(&req->finished);
                yLeaveCriticalSection(&req->access);
            }
            req = req->ws.next;
        }

        yLeaveCriticalSection(&hub->ws.chan[tcpchan].access);
    }
    return req;
}


/*
*   look through all pending request if there is some data that we can send
*
*/
static int ws_processRequests(HubSt *hub, char *errmsg)
{
    int tcpchan;
    int res;

    if (hub->state != NET_HUB_ESTABLISHED) {
        return YAPI_SUCCESS;
    }

    if (hub->ws.next_transmit_tm && hub->ws.next_transmit_tm > yapiGetTickCount()) {
        //u64 wait = hub->ws.next_transmit_tm - yapiGetTickCount();
        //WSLOG("skip reqProcess for %"FMTu64" ms\n", wait);
        return YAPI_SUCCESS;
    }

    for (tcpchan = 0; tcpchan < MAX_ASYNC_TCPCHAN; tcpchan++) {
        RequestSt *req;
        while ((req = getNextReqToSend(hub, tcpchan)) != NULL) {
            int throttle_start;
            int throttle_end;
            if (req->ws.flags & WS_FLG_NEED_API_CLOSE) {
                int res = ws_sendFrame(hub, YSTREAM_TCP_CLOSE, tcpchan, NULL, 0, errmsg);
                if (res < 0) {
                    dbglog("req(%s:%p) unable to ack remote close (%d/%s)\n", req->hub->url.host, req, res, errmsg);
                }
                yEnterCriticalSection(&req->access);
                req->ws.flags &= ~WS_FLG_NEED_API_CLOSE;
                if (req->ws.state == REQ_OPEN) {
                    req->ws.state = REQ_CLOSED_BY_API;
                } else {
                    req->ws.state = REQ_CLOSED_BY_BOTH;
                    ySetEvent(&req->finished);
                }
                yLeaveCriticalSection(&req->access);
                continue;
            }
            throttle_start = req->ws.requestpos;
            throttle_end = req->ws.requestsize;
            // We do not need to mutex lastUploadAckBytes, lastUploadAckTime, tcpMaxWindowSize  and tcpRoundTripTime
            // because they are only used by the WS thread
            if (throttle_end > 2108 && hub->ws.remoteVersion >= USB_META_WS_PROTO_V2 && tcpchan == 0) {
                // Perform throttling on large uploads
                if (req->ws.requestpos == 0) {
                    // First chunk is always first multiple of full window (124 bytes) above 2KB
                    throttle_end = 2108;
                    // Prepare to compute effective transfer rate
                    hub->ws.chan[tcpchan].lastUploadAckBytes = 0;
                    hub->ws.chan[tcpchan].lastUploadAckTime = 0;
                    // Start with initial RTT based estimate
                    hub->ws.uploadRate = hub->ws.tcpMaxWindowSize * 1000 / hub->ws.tcpRoundTripTime;
                } else if (hub->ws.chan[tcpchan].lastUploadAckTime == 0) {
                    // first block not yet acked, wait more
                    //WSLOG("wait for first ack");
                    throttle_end = 0;
                } else {
                    // adapt window frame to available bandwidth
                    int bytesOnTheAir = req->ws.requestpos - hub->ws.chan[tcpchan].lastUploadAckBytes;
                    u32 uploadRate = hub->ws.uploadRate;
                    u64 timeOnTheAir = (yapiGetTickCount() - hub->ws.chan[tcpchan].lastUploadAckTime);
                    u64 toBeSent = 2 * uploadRate + 1024 - bytesOnTheAir + (uploadRate * timeOnTheAir / 1000);
                    if (toBeSent + bytesOnTheAir > DEFAULT_TCP_MAX_WINDOW_SIZE) {
                        toBeSent = DEFAULT_TCP_MAX_WINDOW_SIZE - bytesOnTheAir;
                    }
                    WSLOG("throttling: %d bytes/s (%"FMTu64" + %d = %"FMTu64")\n", hub->ws.uploadRate, toBeSent, bytesOnTheAir, bytesOnTheAir + toBeSent);
                    if (toBeSent < 64) {
                        u64 waitTime = 1000 * (128 - toBeSent) / hub->ws.uploadRate;
                        if (waitTime < 2) waitTime = 2;
                        hub->ws.next_transmit_tm = yapiGetTickCount() + waitTime;
                        WSLOG("WS: %d sent %"FMTu64"ms ago, waiting %"FMTu64"ms...\n", bytesOnTheAir, timeOnTheAir, waitTime);
                        throttle_end = 0;
                    }
                    if (throttle_end > req->ws.requestpos + toBeSent) {
                        // when sending partial content, round up to full frames
                        if (toBeSent > 124) {
                            toBeSent = (toBeSent / 124) * 124;
                        }
                        throttle_end = req->ws.requestpos + (u32)toBeSent;
                    }
                }
            }
            while (req->ws.requestpos < throttle_end) {
                int stream = YSTREAM_TCP;
                int datalen = throttle_end - req->ws.requestpos;
                if (datalen > WS_MAX_DATA_LEN) {
                    datalen = WS_MAX_DATA_LEN;
                }
                if (req->ws.requestpos == 0) {
                    req->ws.first_write_tm = yapiGetTickCount();
                } else if (req->ws.requestpos < 180 && req->ws.requestpos + datalen >= 192) {
                    // on a YoctoHub, the input FIFO is limited to 192, and we can only
                    // accept a frame if it fits entirely in the input FIFO. So make sure
                    // the beginning of the request gets delivered entirely
                    datalen = 191 - req->ws.requestpos;
                }

                if (req->ws.asyncId && (req->ws.requestpos + datalen == req->ws.requestsize)) {
                    // last frame of an async request
                    u8 tmp_data[128];

                    if (datalen == WS_MAX_DATA_LEN) {
                        // last frame is already full we must send the async close in another one
                        res = ws_sendFrame(hub, stream, tcpchan, req->ws.requestbuf + req->ws.requestpos, datalen, errmsg);
                        if (YISERR(res)) {
                            req->errcode = res;
                            YSTRCPY(req->errmsg, YOCTO_ERRMSG_LEN, errmsg);
                            ySetEvent(&req->finished);
                            return res;
                        }
                        WSLOG("\n++++ ws_req:%p: send %d bytes on chan%d (%d/%d)\n", req, datalen, tcpchan, req->ws.requestpos, req->ws.requestsize);
                        req->ws.requestpos += datalen;
                        datalen = 0;
                    }
                    stream = YSTREAM_TCP_ASYNCCLOSE;
                    if (datalen) {
                        memcpy(tmp_data, req->ws.requestbuf + req->ws.requestpos, datalen);
                    }
                    tmp_data[datalen] = req->ws.asyncId;
                    res = ws_sendFrame(hub, stream, tcpchan, tmp_data, datalen + 1, errmsg);
                    WSLOG("\n++++ ws_req(%p) sent %d bytes with async close chan%d:%d (%d/%d)\n", req, datalen, tcpchan, req->ws.asyncId, req->ws.requestpos, req->ws.requestsize);
                    //dumpReqQueue("as_c", req->hub, tcpchan);
                    req->ws.last_write_tm = yapiGetTickCount();
                } else {
                    res = ws_sendFrame(hub, stream, tcpchan, req->ws.requestbuf + req->ws.requestpos, datalen, errmsg);
                    req->ws.last_write_tm = yapiGetTickCount();
                    WSLOG("\n++++ ws_req:%p: sent %d bytes on chan%d:%d (%d/%d)\n", req, datalen, tcpchan, req->ws.asyncId, req->ws.requestpos, req->ws.requestsize);
                }
                if (YISERR(res)) {
                    req->errcode = res;
                    YSTRCPY(req->errmsg, YOCTO_ERRMSG_LEN, errmsg);
                    ySetEvent(&req->finished);
                    return res;
                }
                req->ws.requestpos += datalen;
            }
            if (req->ws.requestpos < req->ws.requestsize) {
                int sent = req->ws.requestpos - throttle_start;
                // not completely sent, cannot do more for now
                if (sent && hub->ws.uploadRate > 0) {
                    u64 waitTime = 1000 * sent / hub->ws.uploadRate;
                    if (waitTime < 2) waitTime = 2;
                    hub->ws.next_transmit_tm = yapiGetTickCount() + waitTime;
                    WSLOG("Sent %dbytes, waiting %"FMTu64"ms...\n", sent, waitTime);
                } else {
                    hub->ws.next_transmit_tm = yapiGetTickCount() + 100;
                }
                break;
            }

        }
    }
    return YAPI_SUCCESS;
}


/*
*   Open Base tcp socket (done in background by yws_thread)
*/
static int ws_openBaseSocket(HubSt *basehub, int first_notification_connection, int mstimout, char *errmsg)
{
    int res, request_len;
    char request[256];
    struct _WSNetHubSt *wshub = &basehub->ws;
    int usessl = 0;

    wshub->base_state = 0;
    wshub->strym_state = 0;
    wshub->remoteVersion = 0;
    wshub->remoteNounce = 0;
    wshub->nounce = 0;
    wshub->bws_open_tm = 0;
    wshub->bws_timeout_tm = 0;
    wshub->bws_read_tm = 0;
    wshub->next_transmit_tm = 0;
    wshub->connectionTime = 0;
    wshub->tcpRoundTripTime = 0;
    wshub->tcpMaxWindowSize = 0;
    wshub->uploadRate = 0;
    wshub->openRequests = 0;

    wshub->skt = INVALID_SOCKET_MULTI;
    wshub->s_next_async_id = 48;
    if (basehub->url.proto == PROTO_HTTP || basehub->url.proto == PROTO_SECURE_HTTP) {
        return YERRMSG(YAPI_IO_ERROR, "not a WebSocket url");
    }
#if 0
    if (!basehub->info.serial[0]) {
        int load_res = LoadInfoJson(basehub, errmsg);
        // YAPI_NOT_SUPPORTED -> old hub that does not support info.json
        if (load_res  < 0 && load_res != YAPI_NOT_SUPPORTED) {
            return load_res;
        }
    }
#endif
    WSLOG("hub(%s) try to open WS connection at %d\n", basehub->url.org_url, basehub->notifAbsPos);
    YSPRINTF(request, 256, "GET %s/not.byn?abs=%u", basehub->url.subdomain, basehub->notifAbsPos);
    usessl = basehub->url.proto == PROTO_SECURE_WEBSOCKET;
    if (usessl && basehub->info.serial[0] && basehub->info.has_unsecure_open_port) {
        // if in info.json we have a non TLS port we can skip certificate validation
        usessl = 2;
    }
    res = yTcpOpenMulti(&wshub->skt, basehub->url.host, basehub->url.portno, usessl, mstimout, errmsg);
    if (YISERR(res)) {
        // yTcpOpen has reset the socket to INVALID
        //yTcpCloseMulti(wshub->skt);
        wshub->skt = INVALID_SOCKET_MULTI;
        return res;
    }
    wshub->bws_open_tm = yapiGetTickCount();
    wshub->bws_timeout_tm = mstimout;
    //write header
    request_len = YSTRLEN(request);

    TRACE_WS_SOCK_APPEND("ws_dump.txt", 0, "<--- WRITE", (const u8*)request, request_len, wshub->bws_open_tm);
    res = yTcpWriteMulti(wshub->skt, (u8*)request, request_len, errmsg);
    if (YISERR(res)) {
        yTcpCloseMulti(wshub->skt);
        wshub->skt = INVALID_SOCKET_MULTI;
        return res;
    }
    TRACE_WS_SOCK_APPEND("ws_dump.txt", 0, "<--- WRITE", (const u8*)ws_header_start, YSTRLEN(ws_header_start), wshub->bws_open_tm);
    res = yTcpWriteMulti(wshub->skt, (u8*)ws_header_start, YSTRLEN(ws_header_start), errmsg);
    if (YISERR(res)) {
        yTcpCloseMulti(wshub->skt);
        wshub->skt = INVALID_SOCKET_MULTI;
        return res;
    }

    wshub->websocket_key_len = GenereateWebSockeyKey((u8*)request, request_len, wshub->websocket_key);
    TRACE_WS_SOCK_APPEND("ws_dump.txt", 0, "<--- WRITE", (const u8*)wshub->websocket_key, wshub->websocket_key_len, wshub->bws_open_tm);
    res = yTcpWriteMulti(wshub->skt, (u8*)wshub->websocket_key, wshub->websocket_key_len, errmsg);
    if (YISERR(res)) {
        yTcpCloseMulti(wshub->skt);
        wshub->skt = INVALID_SOCKET_MULTI;
        return res;
    }
    TRACE_WS_SOCK_APPEND("ws_dump.txt", 0, "<--- WRITE", (const u8*)ws_header_end, YSTRLEN(ws_header_end), wshub->bws_open_tm);
    res = yTcpWriteMulti(wshub->skt, (u8*)ws_header_end, YSTRLEN(ws_header_end), errmsg);
    if (YISERR(res)) {
        yTcpCloseMulti(wshub->skt);
        wshub->skt = INVALID_SOCKET_MULTI;
        return res;
    }
    res = yTcpWriteMulti(wshub->skt, (u8*)basehub->url.host, YSTRLEN(basehub->url.host), errmsg);
    if (YISERR(res)) {
        yTcpCloseMulti(wshub->skt);
        wshub->skt = INVALID_SOCKET_MULTI;
        return res;
    }
    res = yTcpWriteMulti(wshub->skt, (u8*)HTTP_crlfcrlf, HTTP_crlfcrlf_len, errmsg);
    if (YISERR(res)) {
        yTcpCloseMulti(wshub->skt);
        wshub->skt = INVALID_SOCKET_MULTI;
        return res;
    }

    return YAPI_SUCCESS;
}


/*
*   Close Base tcp socket (done in background by yws_thread)
*/
static void ws_closeBaseSocket(struct _WSNetHubSt *base_req)
{
    yTcpCloseMulti(base_req->skt);
    TRACE_WS_SOCK_APPEND("ws_dump.txt", 0, "Close", NULL, 0, base_req->bws_open_tm);
    base_req->skt = INVALID_SOCKET_MULTI;
    yFifoEmpty(&base_req->mainfifo);
}

void ws_cleanup(struct _HubSt *basehub)
{
    ws_closeBaseSocket(&basehub->ws);
}

/*
*   select used by background thread
*/
static int ws_thread_select(struct _WSNetHubSt *base_req, u64 ms, WakeUpSocket *wuce, char *errmsg)
{
    fd_set fds;
    struct timeval timeout;
    int res;
    YSOCKET sktmax = 0;

    memset(&timeout, 0, sizeof(timeout));
    timeout.tv_sec = (long)ms / 1000;
    timeout.tv_usec = (int)(ms % 1000) * 1000;
    /* wait for data */
    FD_ZERO(&fds);
    if (wuce) {
        FD_SET(wuce->listensock, &fds);
        sktmax = wuce->listensock;
    }

    if (base_req->skt == INVALID_SOCKET_MULTI) {
        return YERR(YAPI_INVALID_ARGUMENT);
    } else {
        sktmax = yTcpFdSetMulti(base_req->skt, &fds, sktmax);
    }
    if (sktmax == 0) {
        return YAPI_SUCCESS;
    }
    res = select((int)sktmax + 1, &fds, NULL, NULL, &timeout);
    if (res < 0) {
#ifndef WINDOWS_API
        if (SOCK_ERR == EAGAIN || SOCK_ERR == EINTR) {
            return 0;
        } else
#endif
        {
            res = yNetSetErr();
            return res;
        }
    }
    if (res >= 0) {
        if (wuce && FD_ISSET(wuce->listensock, &fds)) {
            int signal = yConsumeWakeUpSocket(wuce, errmsg);
            //dbglog("exit from sleep with WUCE (%d)\n", signal);
            YPROPERR(signal);
        }
        if (yTcpFdIsSetMulti(base_req->skt, &fds)) {
            int avail = yFifoGetFree(&base_req->mainfifo);
            int nread = 0;
            if (avail) {
                u8 buffer[2048];
                if (avail > 2048) {
                    avail = 2048;
                }
                nread = yTcpReadMulti(base_req->skt, buffer, avail, errmsg);
                if (nread > 0) {
                    TRACE_WS_SOCK_APPEND("ws_dump.txt", 0, "---> READ", buffer, nread, base_req->bws_open_tm);
                    yPushFifo(&base_req->mainfifo, buffer, nread);
                }
            }
            return nread;
        }
    }
    return YAPI_SUCCESS;
}


static void ws_threadUpdateRetryCount(HubSt *hub)
{
    hub->attemptDelay = 500 << hub->retryCount;
    if (hub->attemptDelay > 8000)
        hub->attemptDelay = 8000;
    hub->retryCount++;
#ifdef DEBUG_WEBSOCKET
    dbglog("WS: hub(%s): IO error on ws_thread:(%d) %s\n", hub->url.org_url, hub->errcode, hub->errmsg);
    dbglog("WS: hub(%s): retry in %dms (%d retries)\n", hub->url.org_url, hub->attemptDelay, hub->retryCount);
#endif
}

/**
 *   Background  thread for WebSocket Hub
 */
void* ws_thread(void *ctx)
{
    char *p;
    yThread *thread = (yThread*)ctx;
    char errmsg[YOCTO_ERRMSG_LEN];
    HubSt *hub = (HubSt*)thread->ctx;
    int res;
    int first_notification_connection = 1;
    u8 header[8];
    char buffer[2048];
    int buffer_ofs = 0;
    int continue_processing;
    int is_http_redirect;
    int io_error_count;

    yThreadSignalStart(thread);
    WSLOG("hub(%s) start thread \n", hub->url.org_url);

    while (!yThreadMustEnd(thread) && hub->state != NET_HUB_TOCLOSE) {

        WSLOG("hub(%s) try to open base socket (%d/%dms/%d)\n", hub->url.org_url, hub->retryCount, hub->attemptDelay, hub->state);
        if (hub->retryCount > 0) {
            u64 timeout = yapiGetTickCount() + hub->attemptDelay;
            do {
                //minimal timeout is always 500
                yApproximateSleep(100);
            } while (timeout > yapiGetTickCount() && !yThreadMustEnd(thread));
        }
        if (hub->state == NET_HUB_TOCLOSE) {
            break;
        }
        TRACE_WS_SOCK_APPEND("ws_dump.txt", 1, "Open Socket", NULL, 0, yapiGetTickCount());
        res = ws_openBaseSocket(hub, first_notification_connection, hub->netTimeout, errmsg);
        hub->lastAttempt = yapiGetTickCount();
        if (YISERR(res)) {
            WSLOG("hub(%s) openBaseSocket failed(err=%x / %s)\n", hub->url.host, res, errmsg);
            yEnterCriticalSection(&hub->access);
            hub->errcode = ySetErr(res, hub->errmsg, errmsg, NULL, 0);
            yLeaveCriticalSection(&hub->access);
            if (res == YAPI_SSL_UNK_CERT) {
                // fatal error do not retry to reconnect
                hub->state = NET_HUB_TOCLOSE;
            }
            ws_threadUpdateRetryCount(hub);
            continue;
        }

        io_error_count = 0;
        WSLOG("hub(%s) base socket opened (skt=%x)\n", hub->url.org_url, hub->ws.skt);
        hub->state = NET_HUB_TRYING;
        hub->ws.base_state = WS_BASE_HEADER_SENT;
        hub->ws.connectionTime = 0;
        hub->ws.tcpRoundTripTime = DEFAULT_TCP_ROUND_TRIP_TIME;
        hub->ws.tcpMaxWindowSize = DEFAULT_TCP_MAX_WINDOW_SIZE;
        errmsg[0] = 0;
        continue_processing = 1;
        do {
            u64 wait;
            u64 now = yapiGetTickCount();
            if (hub->ws.next_transmit_tm >= now) {
                wait = hub->ws.next_transmit_tm - now;
            } else {
                wait = 1000;
            }
            is_http_redirect = 0;
            //dbglog("select %"FMTu64"ms on main socket\n", wait);
            res = ws_thread_select(&hub->ws, wait, &hub->wuce, errmsg);
#if 1
            if (YISERR(res)) {
                TRACE_WS_SOCK_APPEND("ws_dump.txt", 0, "ws_thread_select error", NULL, 0, hub->lastAttempt);
                WSLOG("hub(%s) ws_thread_select error %d", hub->url.host, res);
            }
#endif

            if (res > 0) {
                int need_more_data = 0;
                int avail, rw;
                int hdrlen;
                u32 mask;
                int websocket_ok;
                int pktlen;
                hub->ws.lastTraffic = yapiGetTickCount();
                do {
                    u16 pos;
                    //something to handle;
                    switch (hub->ws.base_state) {
                    case WS_BASE_HEADER_SENT:
                        pos = ySeekFifo(&hub->ws.mainfifo, (const u8*)"\r\n\r\n", 4, 0, 0, 0);
                        if (pos == 0xffff) {
                            if ((u64)(yapiGetTickCount() - hub->lastAttempt) > WS_CONNEXION_TIMEOUT) {
                                res = YERR(YAPI_TIMEOUT);
                            } else {
                                need_more_data = 1;
                            }
                            break;
                        } else if (pos >= 2044) {
                            res = YERRMSG(YAPI_IO_ERROR, "Bad reply header");
                            // fatal error do not retry to reconnect
                            hub->state = NET_HUB_TOCLOSE;
                            break;
                        }
                        pos = ySeekFifo(&hub->ws.mainfifo, (const u8*)"\r\n", 2, 0, 0, 0);
                        yPopFifo(&hub->ws.mainfifo, (u8*)buffer, pos + 2);
                        if (YSTRNCMP(buffer, "HTTP/1.1 ", 9) != 0) {
                            res = YERRMSG(YAPI_IO_ERROR, "Bad reply header");
                            // fatal error do not retry to reconnect
                            hub->state = NET_HUB_TOCLOSE;
                            break;
                        }
                        p = buffer + 9;

                        if (YSTRNCMP(p, "301", 3) != 0 || YSTRNCMP(p, "302", 3) != 0 || YSTRNCMP(p, "309", 3) != 0) {
                            is_http_redirect = 1;
                        } else if (YSTRNCMP(p, "101", 3) != 0) {
                            res = YERRMSG(YAPI_NOT_SUPPORTED, "hub does not support WebSocket");
                            // fatal error do not retry to reconnect
                            hub->state = NET_HUB_TOCLOSE;
                            break;
                        }
                        websocket_ok = 0;
                        pos = ySeekFifo(&hub->ws.mainfifo, (const u8*)"\r\n", 2, 0, 0, 0);
                        while (pos != 0) {
                            yPopFifo(&hub->ws.mainfifo, (u8*)buffer, pos + 2);
                            if (pos > 22 && YSTRNICMP(buffer, "Sec-WebSocket-Accept: ", 22) == 0) {
                                if (!VerifyWebsocketKey(buffer + 22, pos, hub->ws.websocket_key, hub->ws.websocket_key_len)) {
                                    websocket_ok = 1;
                                } else {
                                    res = YERRMSG(YAPI_IO_ERROR, "hub does not use same WebSocket protocol");
                                    // fatal error do not retry to reconnect
                                    hub->state = NET_HUB_TOCLOSE;
                                    break;
                                }
                            } else if (pos > 10 && YSTRNICMP(buffer, "Location: ", 10) == 0 && is_http_redirect) {
                                HubURLSt new_url;
                                int parsed_res;
                                int notpos = ymemfind((u8*)buffer, pos - 10, (u8*)"not.byn", 7);
                                if (notpos > 0) {
                                    buffer[notpos] = 0;
                                } else {
                                    buffer[pos] = 0;
                                }
                                WSLOG("Redirect to %s\n", buffer + 10);
                                parsed_res = yParseHubURL(&new_url, buffer + 10, errmsg);
                                if (parsed_res >= 0) {
                                    // update only host, proto and port
                                    p = hub->url.host;
                                    hub->url.host = ystrdup(new_url.host);
                                    if (new_url.proto == PROTO_SECURE_HTTP) {
                                        hub->url.proto = PROTO_SECURE_WEBSOCKET;
                                    }
                                    hub->url.portno = new_url.portno;
                                    yFreeParsedURL(&new_url);
                                    yFree(p);
#ifdef DEBUG_WEBSOCKET
                                    int len = sprintfURL(buffer, sizeof(buffer), &hub->url, 0);
                                    WSLOG("new URL is %s\n", buffer);
#endif
                                }
                                break;
                            }
                            if ((u64)(yapiGetTickCount() - hub->lastAttempt) > WS_CONNEXION_TIMEOUT) {
                                break;
                            }
                            pos = ySeekFifo(&hub->ws.mainfifo, (const u8*)"\r\n", 2, 0, 0, 0);
                        }
                        yPopFifo(&hub->ws.mainfifo, NULL, 2);
                        if (websocket_ok) {
                            hub->ws.base_state = WS_BASE_SOCKET_UPGRADED;
                            buffer_ofs = 0;
                        } else if (is_http_redirect) {
                            res = YERRMSG(YAPI_IO_ERROR, "Redirection");
                        } else {
                            res = YERRMSG(YAPI_IO_ERROR, "Invalid WebSocket header");
                            // fatal error do not retry to reconnect
                            hub->state = NET_HUB_TOCLOSE;
                        }
                        break;
                    case WS_BASE_SOCKET_UPGRADED:
                    case WS_BASE_AUTHENTICATING:
                    case WS_BASE_CONNECTED:

                        avail = yFifoGetUsed(&hub->ws.mainfifo);
                        if (avail < 2) {
                            need_more_data = 1;
                            break;
                        }
                        rw = (avail < 7 ? avail : 7);
                        yPeekFifo(&hub->ws.mainfifo, header, rw, 0);
                        pktlen = header[1] & 0x7f;
                        if (pktlen > 125) {
                            // Unsupported long frame, drop all incoming data (probably 1+ frame(s))
                            res = YERRMSG(YAPI_IO_ERROR, "Unsupported long WebSocket frame");
                            break;
                        }

                        if (header[1] & 0x80) {
                            // masked frame
                            hdrlen = 6;
                            if (avail < hdrlen + pktlen) {
                                need_more_data = 1;
                                break;
                            }
                            memcpy(&mask, header + 2, sizeof(u32));
                        } else {
                            // plain frame
                            hdrlen = 2;
                            if (avail < hdrlen + pktlen) {
                                need_more_data = 1;
                                break;
                            }
                            mask = 0;
                        }

                        if ((header[0] & 0x7f) != 0x02) {
                            // Non-data frame
                            if (header[0] == 0x88) {
                                //if (USBTCPIsPutReady(sock) < 8) return;
                                // websocket close, reply with a close
                                header[0] = 0x88;
                                header[1] = 0x82;
                                mask = YRand32();
                                memcpy(header + 2, &mask, sizeof(u32));
                                header[6] = 0x03 ^ ((u8*)&mask)[0];
                                header[7] = 0xe8 ^ ((u8*)&mask)[1];
                                res = yTcpWriteMulti(hub->ws.skt, header, 8, errmsg);
                                if (YISERR(res)) {
                                    break;
                                }
                                TRACE_WS_SOCK_APPEND("ws_dump.txt", 0, "<--- WRITE", header, 8, hub->ws.bws_open_tm);
                                res = YAPI_NO_MORE_DATA;
                                YSTRCPY(errmsg, YOCTO_ERRMSG_LEN, "WebSocket connection close received");
                                hub->ws.base_state = WS_BASE_OFFLINE;
#ifdef DEBUG_WEBSOCKET
                                dbglog("WS: IO error on base socket of %s: %s\n", hub->url.org_url, errmsg);
#endif
                            } else {
                                // unhandled packet
                                dbglog("unhandled packet:%x%x\n", header[0], header[1]);
                                io_error_count++;
                                if (io_error_count >= 5) {
                                    res = YERRMSG(YAPI_IO_ERROR, "Too many IO error");
                                    break;
                                }
                            }
                            yPopFifo(&hub->ws.mainfifo, NULL, hdrlen + pktlen);
                            break;
                        }
                    // drop frame header
                        yPopFifo(&hub->ws.mainfifo, NULL, hdrlen);
                    // append
                        yPopFifo(&hub->ws.mainfifo, (u8*)buffer + buffer_ofs, pktlen);
                        if (mask) {
                            int i;
                            for (i = 0; i < (pktlen + 1 + 3) >> 2; i++) {
                                buffer[buffer_ofs + i] ^= mask;
                            }
                        }

                        if (header[0] == 0x02) {
                            //  fragmented binary frame
                            WSStreamHead strym;
                            strym.encaps = buffer[buffer_ofs];
                            if (strym.stream == YSTREAM_META) {
                                // unsupported fragmented META stream, should never happen
                                dbglog("Warning:fragmented META\n");
                                break;
                            }
                            buffer_ofs += pktlen;
                            break;
                        }
                        request_pending_logs(hub);
                        res = ws_parseIncomingFrame(hub, (u8*)buffer, buffer_ofs + pktlen, errmsg);
                        if (YISERR(res)) {
                            WSLOG("hub(%s) ws_parseIncomingFrame error %d:%s\n", hub->url.org_url, res, errmsg);
                            break;
                        }
                        buffer_ofs = 0;
                        break;
                    case WS_BASE_OFFLINE:
                        break;
                    }
                } while (!need_more_data && !YISERR(res));
            }
            if (hub->send_ping && ((u64)(yapiGetTickCount() - hub->ws.lastTraffic)) > NET_HUB_NOT_CONNECTION_TIMEOUT) {
                dbglog("PING: network hub %s didn't respond for too long (%d)\n", hub->url.org_url, res);
                continue_processing = 0;
                closeAllReq(hub, res, errmsg);
            } else if (!YISERR(res)) {
                res = ws_processRequests(hub, errmsg);
                if (YISERR(res)) {
                    WSLOG("hub(%s) ws_processRequests error %d:%s\n", hub->url.org_url, res, errmsg);
                }
            }

            if (YISERR(res)) {
                continue_processing = 0;
            } else if ((yThreadMustEnd(thread) || hub->state == NET_HUB_TOCLOSE) && !ws_requestStillPending(hub)) {
                continue_processing = 0;
            }
        } while (continue_processing);
        if (YISERR(res)) {
            WSLOG("WS: hub(%s) IO error %d:%s\n", hub->url.org_url, res, errmsg);
            if (!is_http_redirect) {
                yEnterCriticalSection(&hub->access);
                hub->errcode = ySetErr(res, hub->errmsg, errmsg, NULL, 0);
                yLeaveCriticalSection(&hub->access);
                closeAllReq(hub, res, errmsg);
                if (res == YAPI_UNAUTHORIZED || res == YAPI_DOUBLE_ACCES) {
                    hub->state = NET_HUB_TOCLOSE;
                } 
            }
        }
        WSLOG("hub(%s) close base socket %d:%s\n", hub->url.org_url, res, errmsg);
        ws_closeBaseSocket(&hub->ws);
        log_hub_state(&hub->url, "disconnected", "WebSocket");
        if (hub->state != NET_HUB_TOCLOSE) {
            hub->state = NET_HUB_DISCONNECTED;
        }
    }
    WSLOG("hub(%s:%p) exit thread \n", hub->url.org_url, hub);
    hub->state = NET_HUB_CLOSED;
    yThreadSignalEnd(thread);
    return NULL;
}


/********************************************************************************
 * UDP functions
 *******************************************************************************/

//#define DEBUG_NET_DETECTION


#ifdef DEBUG_NET_DETECTION

void ip2a(u32 ip, char *buffer)
{
    unsigned char bytes[4];
    bytes[0] = ip & 0xFF;
    bytes[1] = (ip >> 8) & 0xFF;
    bytes[2] = (ip >> 16) & 0xFF;
    bytes[3] = (ip >> 24) & 0xFF;
    YSPRINTF(buffer, 125, "%d.%d.%d.%d", bytes[0], bytes[1], bytes[2], bytes[3]);
}
#endif


#ifdef WINDOWS_API
// Link with Iphlpapi.lib
#pragma comment(lib, "IPHLPAPI.lib")

YSTATIC int legacyDetectNetworkInterfaces(IPvX_ADDR *only_ip, os_ifaces *interfaces, int max_nb_interfaces)
{
    INTERFACE_INFO winIfaces[NB_OS_IFACES];
    DWORD returnedSize, nbifaces, i;
    SOCKET sock;
    int nbDetectedIfaces = 0;

    memset(interfaces, 0, max_nb_interfaces * sizeof(os_ifaces));
    sock = WSASocket(AF_INET, SOCK_DGRAM, 0, 0, 0, 0);
    if (sock == INVALID_SOCKET) {
        yNetLogErr();
        return -1;
    }
    if (WSAIoctl(sock, SIO_GET_INTERFACE_LIST, NULL, 0, winIfaces, sizeof(winIfaces), &returnedSize, NULL, NULL) < 0) {
        yNetLogErr();
        closesocket(sock);
        return -1;
    }
    closesocket(sock);
    nbifaces = returnedSize / sizeof(INTERFACE_INFO);
#ifdef DEBUG_NET_DETECTION
    dbglog("windows returned %d interfaces\n", nbifaces);
#endif
    for (i = 0; i < nbifaces; i++) {
        if (winIfaces[i].iiFlags & IFF_LOOPBACK)
            continue;
        if (winIfaces[i].iiFlags & IFF_UP) {
            if (winIfaces[i].iiFlags & IFF_MULTICAST)
                interfaces[nbDetectedIfaces].flags |= OS_IFACE_CAN_MCAST;
            if (!isIPEmpty(only_ip) && isIPv4(only_ip) && only_ip->v4.addr.Val != winIfaces[i].iiAddress.AddressIn.sin_addr.S_un.S_addr) {
                continue;
            }
            interfaces[nbDetectedIfaces].ip.v4.addr.Val = winIfaces[i].iiAddress.AddressIn.sin_addr.S_un.S_addr;
#ifdef DEBUG_NET_DETECTION
            {
                char buffer[128];
                ip2a(interfaces[nbDetectedIfaces].ip.v4.addr.Val, buffer);
                dbglog(" iface%d: ip %s (%x)\n", i, buffer, interfaces[nbDetectedIfaces].flags);
            }
#endif
            nbDetectedIfaces++;
        }
    }
#ifdef DEBUG_NET_DETECTION
    dbglog("%d interfaces are usable\n", nbDetectedIfaces);
#endif
    return nbDetectedIfaces;
}


YSTATIC int yDetectNetworkInterfaces(IPvX_ADDR *only_ip, os_ifaces *interfaces, int max_nb_interfaces)
{
    IP_ADAPTER_ADDRESSES *addresses = NULL;
    ULONG addres_len = 0;
    int i;
    int nbDetectedIfaces = 0;

    ULONG dwRetVal = GetAdaptersAddresses(AF_UNSPEC, 0, NULL, addresses, &addres_len);
    if (dwRetVal == ERROR_BUFFER_OVERFLOW) {
        addresses = yMalloc(addres_len);
        dwRetVal = GetAdaptersAddresses(AF_UNSPEC, 0, NULL, addresses, &addres_len);
    }
    memset(interfaces, 0, max_nb_interfaces * sizeof(os_ifaces));
    if (dwRetVal == NO_ERROR) {
        IP_ADAPTER_ADDRESSES *paddr = addresses;
        while (paddr) {
            PIP_ADAPTER_UNICAST_ADDRESS pUnicast;
            if (paddr->IfType != IF_TYPE_SOFTWARE_LOOPBACK && paddr->OperStatus == IfOperStatusUp) {
                pUnicast = paddr->FirstUnicastAddress;
                for (i = 0; pUnicast != NULL; i++) {
                    if (pUnicast->Address.iSockaddrLength == sizeof(struct sockaddr_in)) {
                        struct sockaddr_in *addr_v4 = (struct sockaddr_in*)pUnicast->Address.lpSockaddr;
                        if (only_ip == NULL || isIPEmpty(only_ip) || (isIPv4(only_ip) && only_ip->v4.addr.Val == addr_v4->sin_addr.s_addr)) {
                            setIPv4Val(&interfaces[nbDetectedIfaces].ip, addr_v4->sin_addr.s_addr);
                            interfaces[nbDetectedIfaces].ifindex = paddr->IfIndex;
                        } else {
                            pUnicast = pUnicast->Next;
                            continue;
                        }
                    } else {
                        struct sockaddr_in6 *addr_v6 = (struct sockaddr_in6*)pUnicast->Address.lpSockaddr;
                        if (only_ip == NULL || isIPEmpty(only_ip) || (!isIPv4(only_ip) && memcmp(&only_ip->v6.addr, &addr_v6->sin6_addr, sizeof(IPvX_ADDR)) == 0)) {
                            memcpy(&interfaces[nbDetectedIfaces].ip.v6.addr, &addr_v6->sin6_addr, sizeof(IPvX_ADDR));
                            interfaces[nbDetectedIfaces].ifindex = paddr->IfIndex;
                        } else {
                            pUnicast = pUnicast->Next;
                            continue;
                        }
                    }
                    if ((paddr->Flags & IP_ADAPTER_NO_MULTICAST) == 0) {
                        interfaces[nbDetectedIfaces].flags |= OS_IFACE_CAN_MCAST;
                    }
#ifdef DEBUG_NET_DETECTION
                    {
                        char buffer[IPSTR_SIZE];
                        iptoa(&interfaces[nbDetectedIfaces].ip, buffer);
                        dbglog(" iface%d: ip %s (%x iface=%d)\n", nbDetectedIfaces, buffer, interfaces[nbDetectedIfaces].flags, interfaces[nbDetectedIfaces].ifindex);
                    }
#endif
                    nbDetectedIfaces++;
                    pUnicast = pUnicast->Next;
                }
            }
            paddr = paddr->Next;
        }
    } else {
        char errmsg[YOCTO_ERRMSG_LEN];
        if (dwRetVal == ERROR_NO_DATA) {
            nbDetectedIfaces = legacyDetectNetworkInterfaces(only_ip, interfaces, max_nb_interfaces);
        } else {
            yNetSetErrEx(__FILENAME__, __LINE__, dwRetVal, errmsg);
            dbglog(errmsg);
        }

    }
    if (addresses) {
        yFree(addresses);
    }
#ifdef DEBUG_NET_DETECTION
    dbglog("%d interfaces are usable\n", nbDetectedIfaces);
#endif
    return nbDetectedIfaces;
}
#else

#include <net/if.h>
#include <ifaddrs.h>
YSTATIC int yDetectNetworkInterfaces(IPvX_ADDR *only_ip, os_ifaces *interfaces, int max_nb_interfaces)
{
#if 1
    struct ifaddrs *if_addrs = NULL;
    struct ifaddrs *p = NULL;
    int nbDetectedIfaces = 0;
    memset(interfaces, 0, max_nb_interfaces * sizeof(os_ifaces));
    if (getifaddrs(&if_addrs) != 0){
        yNetLogErr();
        return -1;
    }
    p = if_addrs;
    while (p) {
        if (p->ifa_addr && (p->ifa_addr->sa_family == AF_INET || p->ifa_addr->sa_family == AF_INET6)) {
            IPvX_ADDR ip;
            if (p->ifa_addr->sa_family == AF_INET) {
                struct sockaddr_in *tmp = (struct sockaddr_in*)p->ifa_addr;
                setIPv4Val(&ip, tmp->sin_addr.s_addr);
            } else {
                struct sockaddr_in6 *tmp6 = (struct sockaddr_in6*)p->ifa_addr;
                memcpy(&ip, &tmp6->sin6_addr, sizeof(IPvX_ADDR));
            }
            if (only_ip != NULL && !isIPEmpty(only_ip) && memcmp(only_ip, &ip, sizeof(IPvX_ADDR))!=0){
#if 0//def DEBUG_NET_DETECTION
                char buff[IPSTR_SIZE];
                char buff2[IPSTR_SIZE];
                iptoa(&ip, buff);
                iptoa(only_ip, buff2);
                dbglog("drop %s : %s !=%s (%X)\n", p->ifa_name, buff, buff2, p->ifa_flags);
#endif
                p = p->ifa_next;
                continue;
            }
            if ((p->ifa_flags & IFF_LOOPBACK) == 0){
                if (p->ifa_flags & IFF_UP && p->ifa_flags & IFF_RUNNING){
                    if (p->ifa_flags & IFF_MULTICAST){
                        interfaces[nbDetectedIfaces].flags |= OS_IFACE_CAN_MCAST;
                    }
                    ystrcpy(interfaces[nbDetectedIfaces].name,OS_IFACE_NAME_MAX_LEN, p->ifa_name);
                    interfaces[nbDetectedIfaces].ifindex = if_nametoindex(p->ifa_name);  
                    if (interfaces[nbDetectedIfaces].ifindex == 0) {
                        dbglog("Warning: Unable to get interfaces index for %s\n", p->ifa_name);
                    }
                    interfaces[nbDetectedIfaces].ip = ip;
#ifdef DEBUG_NET_DETECTION
                    {
                        char buff[IPSTR_SIZE];
                        iptoa(&ip, buff);
                        dbglog("Iface %s : %s (flags=%X iface=%d)\n", p->ifa_name, buff, p->ifa_flags,interfaces[nbDetectedIfaces].ifindex);
                    }
#endif
                    nbDetectedIfaces++;
                }
            }
#ifdef DEBUG_NET_DETECTION
            else {
                char buff[IPSTR_SIZE];
                iptoa(&ip, buff);
                dbglog("drop %s : %s (%X)\n", p->ifa_name, buff, p->ifa_flags);
            }
#endif
        }
        p = p->ifa_next;
    }
    freeifaddrs(if_addrs);
#else
    int nbDetectedIfaces = 1;
    memset(interfaces, 0, max_nb_interfaces * sizeof(os_ifaces));
    interfaces[0].flags |= OS_IFACE_CAN_MCAST;
    setIPv4Val(&interfaces[0].ip,INADDR_ANY);
#endif
    return nbDetectedIfaces;
}

#endif


static const char *discovery =
    "M-SEARCH * HTTP/1.1\r\n"
    "HOST:" YSSDP_MCAST_ADDR_STR ":" TOSTRING(YSSDP_PORT) "\r\n"
    "MAN:\"ssdp:discover\"\r\n"
    "MX:5\r\n"
    "ST:" YSSDP_URN_YOCTOPUCE"\r\n"
    "\r\n";


static const char *discovery_v6 =
    "M-SEARCH * HTTP/1.1\r\n"
    "HOST:" YSSDP_MCAST_ADDR6_STR ":" TOSTRING(YSSDP_PORT) "\r\n"
    "MAN:\"ssdp:discover\"\r\n"
    "MX:5\r\n"
    "ST:" YSSDP_URN_YOCTOPUCE"\r\n"
    "\r\n";


#define SSDP_NOTIFY "NOTIFY * HTTP/1.1\r\n"
#define SSDP_M_SEARCH "M-SEARCH * HTTP/1.1\r\n"
#define SSDP_HTTP "HTTP/1.1 200 OK\r\n"
#define SSDP_LINE_MAX_LEN 80u

#define UDP_IN_FIFO yFifoBuf


static char hexatochar(char hi_c, char lo_c)
{
    u8 hi, lo;
    hi = ((u8)(hi_c) & 0x1f) ^ 0x10;
    lo = ((u8)(lo_c) & 0x1f) ^ 0x10;
    if (hi & 0x10) hi -= 7;
    if (lo & 0x10) lo -= 7;
    return (hi << 4) + lo;
}

static int uuidToSerial(const char *uuid, char *serial)
{
    int i;
    int len, padlen;
    char *s = serial;
    const char *u = uuid;

    for (i = 0, u = uuid; i < 4; i++, u += 2) {
        *s++ = hexatochar(*u, *(u + 1));
    }
    u++;
    for (; i < 6; i++, u += 2) {
        *s++ = hexatochar(*u, *(u + 1));
    }
    u++;
    for (; i < 8; i++, u += 2) {
        *s++ = hexatochar(*u, *(u + 1));
    }
    *s++ = '-';
    u = strstr(uuid, "-COFF-EE");
    if (u == NULL) {
        return -1;
    }
    u += 8;
    while (*u == '0') u++;
    len = YSTRLEN(u);
    if (YSTRNCMP(serial, "VIRTHUB0", YOCTO_BASE_SERIAL_LEN) == 0) {
        padlen = YOCTO_SERIAL_SEED_SIZE - 1;
    } else {
        padlen = 5;
    }
    for (i = len; i < padlen; i++) {
        *s++ = '0';
    }
    *s = 0;
    YSTRCAT(serial, YOCTO_SERIAL_LEN, u);
    return 0;
}


static void ySSDPUpdateCache(SSDPInfos *SSDP, const char *uuid, const char *url, int cacheValidity)
{
    int i;

    if (cacheValidity <= 0)
        cacheValidity = 1800;
    cacheValidity *= 1000;

    for (i = 0; i < NB_SSDP_CACHE_ENTRY; i++) {
        SSDP_CACHE_ENTRY *p = SSDP->SSDPCache[i];
        if (p == NULL)
            break;
        if (YSTRCMP(uuid, p->uuid) == 0) {
            p->detectedTime = yapiGetTickCount();
            p->maxAge = cacheValidity;
            if (YSTRCMP(url, p->url)) {
                if (SSDP->callback) {
                    SSDP->callback(p->serial, url, p->url);
                }
                YSTRCPY(p->url, SSDP_URL_LEN, url);
            } else {
                if (SSDP->callback) {
                    SSDP->callback(p->serial, url, NULL);
                }
            }
            return;
        }
    }
    if (i < NB_SSDP_CACHE_ENTRY) {
        SSDP_CACHE_ENTRY *p = (SSDP_CACHE_ENTRY*)yMalloc(sizeof(SSDP_CACHE_ENTRY));
        YSTRCPY(p->uuid, SSDP_URL_LEN, uuid);
        if (uuidToSerial(p->uuid, p->serial) < 0) {
            yFree(p);
            return;
        }
        YSTRCPY(p->url, SSDP_URL_LEN, url);
        p->detectedTime = yapiGetTickCount();
        p->maxAge = cacheValidity;
        SSDP->SSDPCache[i] = p;
        if (SSDP->callback) {
            SSDP->callback(p->serial, p->url, NULL);
        }
    }
}

static void ySSDPCheckExpiration(SSDPInfos *SSDP)
{
    int i;
    u64 now = yapiGetTickCount();

    for (i = 0; i < NB_SSDP_CACHE_ENTRY; i++) {
        SSDP_CACHE_ENTRY *p = SSDP->SSDPCache[i];
        if (p == NULL)
            break;
        if (p->maxAge > 0 && (u64)(now - p->detectedTime) > p->maxAge) {
            if (SSDP->callback) {
                SSDP->callback(p->serial, NULL, p->url);
            }
            p->maxAge = 0;
        }
    }
}


static void ySSDP_parseSSPDMessage(SSDPInfos *SSDP, char *message, int msg_len)
{
    int len = 0;
    char *p, *start, *lastsep;
    char *location = NULL;
    char *usn = NULL;
    char *cache = NULL;

    if (len >= msg_len) {
        return;
    }

    if (memcmp(message,SSDP_HTTP,YSTRLEN(SSDP_HTTP)) == 0) {
        len = YSTRLEN(SSDP_HTTP);
    } else if (memcmp(message,SSDP_NOTIFY,YSTRLEN(SSDP_NOTIFY)) == 0) {
        len = YSTRLEN(SSDP_NOTIFY);
    }
    if (len) {
        //dbglog("SSDP Message:\n%s\n",message);
        start = p = lastsep = message + len;
        msg_len -= len;
        while (msg_len && *p) {
            switch (*p) {
            case ':':
                if (lastsep == start) {
                    lastsep = p;
                }
                break;
            case '\r':
                if (p == start) {
                    // \r\n\r\n ->end
                    if (msg_len > 1) msg_len = 1;
                    break;
                }

                if (lastsep == start) {
                    //no : on the line -> drop this message
                    return;
                }
                *lastsep++ = 0;
                if (*lastsep == ' ') lastsep++;
                *p = 0;
                if (strcmp(start, "LOCATION") == 0) {
                    location = lastsep;
                } else if (strcmp(start, "USN") == 0) {
                    usn = lastsep;
                } else if (strcmp(start, "CACHE-CONTROL") == 0) {
                    cache = lastsep;
                }
                break;
            case '\n':
                start = lastsep = p + 1;
                break;
            }
            p++;
            msg_len--;
        }
        if (location && usn && cache) {
            const char *uuid, *urn;
            int cacheVal;
            //dbglog("SSDP: location: %s %s %s\n\n",location,usn,cache);
            // parse USN
            p = usn;
            // ReSharper disable once CppPossiblyErroneousEmptyStatements
            while (*p && *p++ != ':');
            if (!*p) return;
            uuid = p;
            // ReSharper disable once CppPossiblyErroneousEmptyStatements
            while (*p && *p++ != ':');
            if (*p != ':') return;
            *(p++ - 1) = 0;
            if (!*p) return;
            urn = p;
            // parse Location
            if (YSTRNCMP(location, "http://", 7) == 0) {
                location += 7;
            }
            p = location;
            while (*p && *p != '/') p++;
            if (*p == '/') *p = 0;
            p = cache;
            // ReSharper disable once CppPossiblyErroneousEmptyStatements
            while (*p && *p++ != '=');
            if (!*p) return;
            cacheVal = atoi(p);
            if (YSTRCMP(urn, YSSDP_URN_YOCTOPUCE) == 0) {
                ySSDPUpdateCache(SSDP, uuid, location, cacheVal);
            }
        }
    }
#if 0
    else {
        dbglog("SSDP drop invalid message:\n%s\n", message);
    }
#endif
}

static os_ifaces detectedIfaces[NB_OS_IFACES];
static int nbDetectedIfaces = 0;
static const IPvX_ADDR ssdp_mcast_addr = {.v6 = {.addr = {0x02ff, 0, 0, 0, 0, 0, 0, 0x0c00}}};


static void* ySSDP_thread(void *ctx)
{
    yThread *thread = (yThread*)ctx;
    SSDPInfos *SSDP = (SSDPInfos*)thread->ctx;
    fd_set fds;
    u8 buffer[1536];
    struct timeval timeout;
    int res, received, i;
    YSOCKET sktmax;
    yFifoBuf inFifo;


    yThreadSignalStart(thread);
    yFifoInit(&inFifo, buffer, sizeof(buffer));

    while (!yThreadMustEnd(thread)) {
        memset(&timeout, 0, sizeof(timeout));
        timeout.tv_sec = (long)1;
        timeout.tv_usec = (int)0;
        /* wait for data */
        FD_ZERO(&fds);
        sktmax = 0;
        for (i = 0; i < nbDetectedIfaces; i++) {
            FD_SET(SSDP->request_sock[i], &fds);
            if (SSDP->request_sock[i] > sktmax) {
                sktmax = SSDP->request_sock[i];
            }
            if (SSDP->notify_sock[i] != INVALID_SOCKET) {
                FD_SET(SSDP->notify_sock[i], &fds);
                if (SSDP->notify_sock[i] > sktmax) {
                    sktmax = SSDP->notify_sock[i];
                }
            }
        }
        res = select((int)sktmax + 1, &fds, NULL, NULL, &timeout);
        if (res < 0) {
#ifndef WINDOWS_API
            if(SOCK_ERR == EAGAIN || SOCK_ERR == EINTR){
                continue;
            } else
#endif
            {
                yNetLogErr();
                break;
            }
        }

        if (!yContext) continue;
        ySSDPCheckExpiration(SSDP);
        if (res != 0) {
            for (i = 0; i < nbDetectedIfaces; i++) {
                struct sockaddr_storage addr;
                socklen_t sockaddr_remote_size = sizeof(struct sockaddr_in6);

                if (FD_ISSET(SSDP->request_sock[i], &fds)) {
                    received = (int)yrecvfrom(SSDP->request_sock[i], (char*)buffer, sizeof(buffer)-1, 0, (struct sockaddr*) &addr, &sockaddr_remote_size);
                    if (received > 0) {
                        buffer[received] = 0;
                        ySSDP_parseSSPDMessage(SSDP, (char*)buffer, received);
                    }
                }
                if (FD_ISSET(SSDP->notify_sock[i], &fds)) {
                    //dbglog("new packet on interface %d\n", i);
                    received = (int)yrecvfrom(SSDP->notify_sock[i], (char *)buffer, sizeof(buffer)-1, 0, (struct sockaddr*) &addr, &sockaddr_remote_size);
                    if (received > 0) {
                        buffer[received] = 0;
                        ySSDP_parseSSPDMessage(SSDP, (char*)buffer, received);
                    }
                }
            }
        }
    }
    yFifoCleanup(&inFifo);
    yThreadSignalEnd(thread);
    return NULL;
}


int ySSDPDiscover(SSDPInfos *SSDP, char *errmsg)
{
    int sent, len, i;
    struct sockaddr_storage storage;
    socklen_t socklen;

    for (i = 0; i < nbDetectedIfaces; i++) {
        if (isIPv4(&detectedIfaces[i].ip)) {
            struct sockaddr_in *addr_ipv4 = (struct sockaddr_in*)&storage;
            socklen = sizeof(struct sockaddr_in);
            memset(&storage, 0, socklen);
            addr_ipv4->sin_family = AF_INET;
            addr_ipv4->sin_port = htons(YSSDP_PORT);
            addr_ipv4->sin_addr.s_addr = inet_addr(YSSDP_MCAST_ADDR_STR);
            len = ystrlen(discovery);
            sent = (int)ysendto(SSDP->request_sock[i], discovery, len, 0, (struct sockaddr*)&storage, socklen);

        } else {
            struct sockaddr_in6 *addr_ipv6 = (struct sockaddr_in6*)&storage;
            socklen = sizeof(struct sockaddr_in6);
            memset(addr_ipv6, 0, socklen);
            addr_ipv6->sin6_family = AF_INET6;
            addr_ipv6->sin6_port = htons(YSSDP_PORT);
            memcpy(&addr_ipv6->sin6_addr, &ssdp_mcast_addr, sizeof(addr_ipv6->sin6_addr));
            len = ystrlen(discovery_v6);
            sent = (int)sendto(SSDP->request_sock[i], discovery_v6, len, 0, (struct sockaddr*)&storage, socklen);

        }
        if (sent < 0) {
            return yNetSetErr();
        }
    }
    return YAPI_SUCCESS;
}


int ySSDPStart(SSDPInfos *SSDP, ssdpHubDiscoveryCallback callback, char *errmsg)
{
    u32 optval;
    int i;
    socklen_t socksize;

    if (SSDP->started)
        return YAPI_SUCCESS;

    memset(SSDP, 0, sizeof(SSDPInfos));
    SSDP->callback = callback;
    nbDetectedIfaces = yDetectNetworkInterfaces(0, detectedIfaces, NB_OS_IFACES);

    for (i = 0; i < nbDetectedIfaces; i++) {
        //create M-search socket
        if (isIPv4(&detectedIfaces[i].ip)) {
            struct ip_mreq mcast_membership;
            struct sockaddr_in sockaddr;
            SSDP->request_sock[i] = ysocket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (SSDP->request_sock[i] == INVALID_SOCKET) {
                return yNetSetErr();
            }
            optval = 1;
            setsockopt(SSDP->request_sock[i], SOL_SOCKET, SO_REUSEADDR, (char*)&optval, sizeof(optval));
#ifdef SO_REUSEPORT
            setsockopt(SSDP->request_sock[i], SOL_SOCKET, SO_REUSEPORT, (char *)&optval, sizeof(optval));
#endif
            // set port to 0 since we accept any port
            socksize = sizeof(sockaddr);
            memset(&sockaddr, 0, socksize);
            sockaddr.sin_family = AF_INET;
            sockaddr.sin_addr.s_addr = detectedIfaces[i].ip.v4.addr.Val;
            if (ybind(SSDP->request_sock[i], (struct sockaddr*)&sockaddr, socksize) < 0) {
                return yNetSetErr();
            }
            //create NOTIFY socket
            SSDP->notify_sock[i] = ysocket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (SSDP->notify_sock[i] == INVALID_SOCKET) {
                return yNetSetErr();
            }

            optval = 1;
            setsockopt(SSDP->notify_sock[i], SOL_SOCKET, SO_REUSEADDR, (char*)&optval, sizeof(optval));
#ifdef SO_REUSEPORT
        setsockopt(SSDP->notify_sock[i], SOL_SOCKET, SO_REUSEPORT, (char *)&optval, sizeof(optval));
#endif

            // set port to 0 since we accept any port
            socksize = sizeof(sockaddr);
            memset(&sockaddr, 0, socksize);
            sockaddr.sin_family = AF_INET;
            sockaddr.sin_port = htons(YSSDP_PORT);
            sockaddr.sin_addr.s_addr = 0;
            if (ybind(SSDP->notify_sock[i], (struct sockaddr*)&sockaddr, socksize) < 0) {
                return yNetSetErr();
            }
            mcast_membership.imr_multiaddr.s_addr = inet_addr(YSSDP_MCAST_ADDR_STR);
            mcast_membership.imr_interface.s_addr = detectedIfaces[i].ip.v4.addr.Val;
            if (setsockopt(SSDP->notify_sock[i], IPPROTO_IP, IP_ADD_MEMBERSHIP, (void*)&mcast_membership, sizeof(mcast_membership)) < 0) {
                yNetLogErr();
                dbglog("Unable to add multicast membership for SSDP");
                yclosesocket(SSDP->notify_sock[i]);
                SSDP->notify_sock[i] = INVALID_SOCKET;
            }
        } else {
            struct ipv6_mreq mcast_membership6;
            struct sockaddr_in6 addr_v6;
            SSDP->request_sock[i] = ysocket(PF_INET6, SOCK_DGRAM, IPPROTO_UDP);
            if (SSDP->request_sock[i] == INVALID_SOCKET) {
                return yNetSetErr();
            }

            optval = 1;
            setsockopt(SSDP->request_sock[i], SOL_SOCKET, SO_REUSEADDR, (char*)&optval, sizeof(optval));
#ifdef SO_REUSEPORT
        setsockopt(SSDP->request_sock[i], SOL_SOCKET, SO_REUSEPORT, (char *)&optval, sizeof(optval));
#endif

            // set port to 0 since we accept any port
            socksize = sizeof(struct sockaddr_in6);
            memset(&addr_v6, 0, socksize);
            addr_v6.sin6_family = AF_INET6;
            addr_v6.sin6_scope_id = detectedIfaces[i].ifindex;
            memcpy(&addr_v6.sin6_addr, detectedIfaces[i].ip.v6.addr, sizeof(addr_v6.sin6_addr));
            if (ybind(SSDP->request_sock[i], (struct sockaddr*)&addr_v6, socksize) < 0) {
                return yNetSetErr();
            }

            //create NOTIFY socket
            SSDP->notify_sock[i] = ysocket(PF_INET6, SOCK_DGRAM, IPPROTO_UDP);
            if (SSDP->notify_sock[i] == INVALID_SOCKET) {
                return yNetSetErr();
            }
            optval = 1;
            setsockopt(SSDP->notify_sock[i], SOL_SOCKET, SO_REUSEADDR, (char*)&optval, sizeof(optval));
#ifdef SO_REUSEPORT
            setsockopt(SSDP->notify_sock[i], SOL_SOCKET, SO_REUSEPORT, (char *)&optval, sizeof(optval));
#endif
            socksize = sizeof(struct sockaddr_in6);
            memset(&addr_v6, 0, socksize);
            addr_v6.sin6_family = AF_INET6;
            addr_v6.sin6_port = htons(YSSDP_PORT);
            memcpy(&addr_v6.sin6_addr, detectedIfaces[i].ip.v6.addr, sizeof(addr_v6.sin6_addr));
            addr_v6.sin6_scope_id = detectedIfaces[i].ifindex;
            if (ybind(SSDP->notify_sock[i], (struct sockaddr*)&addr_v6, socksize) < 0) {
                return yNetSetErr();
            }
            memset(&mcast_membership6, 0, sizeof(mcast_membership6));
            memcpy(&mcast_membership6.ipv6mr_multiaddr, &ssdp_mcast_addr, sizeof(mcast_membership6.ipv6mr_multiaddr));
            mcast_membership6.ipv6mr_interface = detectedIfaces[i].ifindex;
            if (setsockopt(SSDP->notify_sock[i], IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, (void*)&mcast_membership6, sizeof(mcast_membership6)) < 0) {
                yNetLogErr();
                dbglog("Unable to add multicast membership for SSDP");
                yclosesocket(SSDP->notify_sock[i]);
                SSDP->notify_sock[i] = INVALID_SOCKET;
            }
        }
    }
    //yThreadCreate will not create a new thread if there is already one running
    if (yThreadCreateNamed(&SSDP->thread, "ssdp", ySSDP_thread, SSDP) < 0) {
        return YERRMSG(YAPI_IO_ERROR, "Unable to start helper thread");
    }
    SSDP->started = 1;
    return ySSDPDiscover(SSDP, errmsg);
}


void ySSDPStop(SSDPInfos *SSDP)
{
    int i;

    if (yThreadIsRunning(&SSDP->thread)) {
        u64 timeref;
        yThreadRequestEnd(&SSDP->thread);
        timeref = yapiGetTickCount();
        while (yThreadIsRunning(&SSDP->thread) && (yapiGetTickCount() - timeref < 1000)) {
            yApproximateSleep(10);
        }
        yThreadKill(&SSDP->thread);
    }

    //unregister all detected hubs
    for (i = 0; i < NB_SSDP_CACHE_ENTRY; i++) {
        SSDP_CACHE_ENTRY *p = SSDP->SSDPCache[i];
        if (p == NULL)
            continue;
        if (p->maxAge) {
            yapiUnregisterHub(p->url);
            p->maxAge = 0;
            if (SSDP->callback)
                SSDP->callback(p->serial, NULL, p->url);
        }
        yFree(p);
    }

    for (i = 0; i < nbDetectedIfaces; i++) {
        if (SSDP->request_sock[i] != INVALID_SOCKET) {
            yclosesocket(SSDP->request_sock[i]);
            SSDP->request_sock[i] = INVALID_SOCKET;
        }
        if (SSDP->notify_sock[i] != INVALID_SOCKET) {
            yclosesocket(SSDP->notify_sock[i]);
            SSDP->notify_sock[i] = INVALID_SOCKET;
        }
    }
    SSDP->started = 0;
}
