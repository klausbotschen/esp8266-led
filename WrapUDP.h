#ifndef WRAPUDP_H
#define WRAPUDP_H

#include "IPAddress.h"
#include <functional>
#include "lwip/init.h"


class WrapUDP;
struct udp_pcb;
struct pbuf;
#if LWIP_VERSION_MAJOR == 1
struct ip_addr;
typedef struct ip_addr ip_addr_t;
#else
struct ip4_addr;
typedef struct ip4_addr ip_addr_t;
#endif


class WrapUDP
{
protected:
    udp_pcb *_pcb;
    void (*_handler)(pbuf *);
    bool _connected;

    void _recv(udp_pcb *upcb, pbuf *pb, ip_addr_t *addr, uint16_t port);
#if LWIP_VERSION_MAJOR == 1
    static void _s_recv(void *arg, udp_pcb *upcb, pbuf *p, ip_addr_t *addr, uint16_t port);
#else
    static void _s_recv(void *arg, udp_pcb *upcb, pbuf *p, const ip_addr_t *addr, uint16_t port);
#endif

public:
    WrapUDP();
    virtual ~WrapUDP();

    void onPacket(void (*cb)(pbuf *));

    bool listen(ip_addr_t *addr, uint16_t port);
    bool listen(const IPAddress addr, uint16_t port);
    bool listen(uint16_t port);

    bool connect(ip_addr_t *addr, uint16_t port);
    bool connect(const IPAddress addr, uint16_t port);

    void close();

    size_t writeTo(pbuf *p, ip_addr_t *addr, uint16_t port);
    size_t writeTo(pbuf *p, const IPAddress addr, uint16_t port);
    size_t write(pbuf *p);
};

#endif
