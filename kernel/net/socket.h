#ifndef __NET__SOCKET_H__
#define __NET__SOCKET_H__

typedef struct
{
    int   domain;
    int   type;
    int   protocol;
    void *private_data;
} socket_t;

void socket(int domain, int type, int protocol, socket_t *socket);

#endif // __NET__SOCKET_H__