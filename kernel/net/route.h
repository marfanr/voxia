#ifndef __NET__ROUTE_H__
#define __NET__ROUTE_H__

#include <type.h>

struct rt_entry {
    uint32_t dest_net;    
    uint32_t netmask;     
    uint32_t gateway_ip;  
    struct net_device* dev; 
    
    struct rt_entry* next; // Pointer untuk iterasi
};

#endif // __NET__ROUTE_H__