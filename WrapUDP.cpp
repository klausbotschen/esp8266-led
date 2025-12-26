#include "Arduino.h"
#include "WrapUDP.h"

extern "C" {
#include "user_interface.h"
#include "lwip/opt.h"
#include "lwip/inet.h"
#include "lwip/udp.h"
#include "lwip/igmp.h"
}



WrapUDP::WrapUDP()
{
    _pcb = NULL;
    _connected = false;
    _handler = NULL;
}

WrapUDP::~WrapUDP()
{
    close();
}

void WrapUDP::onPacket(void (*cb)(pbuf *))
{
    _handler = cb;
}

void WrapUDP::_recv(udp_pcb *upcb, pbuf *pb, ip_addr_t *addr, uint16_t port)
{
    (void)upcb; // its unused, avoid warning
    while(pb != NULL) {
        if(_handler) _handler(pb);
        pbuf * this_pb = pb;
        pb = pb->next;
        this_pb->next = NULL;
        pbuf_free(this_pb);
    }
}

#if LWIP_VERSION_MAJOR == 1
void WrapUDP::_s_recv(void *arg, udp_pcb *upcb, pbuf *p, ip_addr_t *addr, uint16_t port)
#else
void WrapUDP::_s_recv(void *arg, udp_pcb *upcb, pbuf *p, const ip_addr_t *addr, uint16_t port)
#endif
{
    reinterpret_cast<WrapUDP*>(arg)->_recv(upcb, p, (ip_addr_t *)addr, port);
}

/* ********************************************************** */

bool WrapUDP::listen(ip_addr_t *addr, uint16_t port)
{
    close();
    _pcb = udp_new();
    if(_pcb == NULL) {
        return false;
    }
    err_t err = udp_bind(_pcb, addr, port);
    if(err != ERR_OK) {
        close();
        return false;
    }
    udp_recv(_pcb, &_s_recv, (void *) this);
    _connected = true;
    return true;
}

bool WrapUDP::listen(const IPAddress addr, uint16_t port)
{
    ip_addr_t laddr;
    laddr.addr = addr;
    return listen(&laddr, port);
}

bool WrapUDP::listen(uint16_t port)
{
    return listen(IPAddress((uint32_t)INADDR_ANY), port);
}

/* ********************************************************** */

bool WrapUDP::connect(ip_addr_t *addr, uint16_t port)
{
    close();
    _pcb = udp_new();
    if(_pcb == NULL) {
        return false;
    }
    err_t err = udp_connect(_pcb, addr, port);
    if(err != ERR_OK) {
        close();
        return false;
    }
    udp_recv(_pcb, &_s_recv, (void *) this);
    _connected = true;
    return true;
}

bool WrapUDP::connect(const IPAddress addr, uint16_t port)
{
    ip_addr_t daddr;
    daddr.addr = addr;
    return connect(&daddr, port);
}

void WrapUDP::close()
{
    if(_pcb != NULL) {
        if(_connected) {
            udp_disconnect(_pcb);
        }
        udp_remove(_pcb);
        _connected = false;
        _pcb = NULL;
    }
}

/* ********************************************************** */

size_t WrapUDP::writeTo(pbuf *p, ip_addr_t *addr, uint16_t port)
{
    size_t len = 0;
    if(!_pcb && !connect(addr, port)) {
        return 0;
    }
    if(p != NULL) {
        len = p->len;
        err_t err = udp_sendto(_pcb, p, addr, port);
        if(err < ERR_OK) {
            return 0;
        }
        return len;
    }
    return 0;
}

size_t WrapUDP::writeTo(pbuf *p, const IPAddress addr, uint16_t port)
{
    ip_addr_t daddr;
    daddr.addr = addr;
    return writeTo(p, &daddr, port);
}

size_t WrapUDP::write(pbuf *p)
{
	if(_pcb){
		return writeTo(p, &(_pcb->remote_ip), _pcb->remote_port);
	}
    return 0;
}


