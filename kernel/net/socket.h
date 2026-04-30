#ifndef __NET__SOCKET_H__
#define __NET__SOCKET_H__

#include "ioforge/ioforge_nic.h"
#include <type.h>

/* Socket General */
typedef struct sockaddr {
	uint16_t sa_family;
	uint8_t sa_data[14];
} sockaddr_t;

/* Raw / packet socket: bind ke NIC + EtherType */
typedef struct sockaddr_ll {
	uint16_t sll_family;   /* AF_RAW atau AF_PACKET */
	uint16_t sll_protocol; /* EtherType (host byte order) */
	uint8_t sll_nic_id;    /* NIC id (0..MAX_NICS-1), 0xff = semua NIC */
	uint8_t sll_mac[NIC_MAC_LEN];
	uint8_t _pad;
} sockaddr_ll_t;

/* IPv4 socket address */
typedef struct sockaddr_in {
	uint16_t sin_family; /* AF_INET */
	uint16_t sin_port;   /* port dalam network byte order */
	uint32_t sin_addr;   /* IP dalam network byte order   */
	uint8_t _pad[8];
} sockaddr_in_t;

typedef struct socket socket_t;
typedef struct socket_ops {
	int (*recv)(socket_t* socket, void* buffer, size_t size);
	// recv with zero copy
	int (*recv_zc)(socket_t* socket, void** buffer, size_t size);

	int (*bind)(socket_t* socket, sockaddr_in_t* addr, uint32_t len);
	int (*set_sockopt)(socket_t* socket, uint32_t level, uint32_t optname,
			   const void* optval, uint32_t optlen);
} socket_ops_t;

typedef enum {
	AF_RAW = 0,	/* raw Ethernet frame (L2)                */
	AF_INET = 2,	/* IPv4 (TCP/UDP — diimplementasi di atas) */
	AF_INET6 = 10,	/* IPv6                                    */
	AF_PACKET = 17, /* Linux-style packet socket               */
} sock_family_t;

typedef enum {
	SOCK_RAW = 0,	 /* raw, tidak ada transport header         */
	SOCK_DGRAM = 1,	 /* datagram (UDP-style, connectionless)    */
	SOCK_STREAM = 2, /* stream (TCP-style, connection-oriented) */
} sock_type_t;

struct socket {
	sock_family_t family;
	sock_type_t type;
	uint16_t protocol;

	/* untuk binding */
	struct ioforge_nic_service*
		bound_nic;   /* NIC id, 0xff = semua NIC            */
	uint8_t nonblocking; /* SO_NONBLOCK?  */
	uint8_t broadcast;   /* SO_BROADCAST? */

	sockaddr_in_t local_addr;
	sockaddr_in_t remote_addr;

	// net_buffer

	// statistik
	uint64_t rx_packets;
	uint64_t tx_packets;
	uint64_t rx_dropped;

	uint8_t _in_use;

	// ops
	socket_ops_t* ops;
};

void vxSocket(sock_family_t family, sock_type_t type, uint16_t protocol,
	      socket_t** socket);
uint32_t vxInetAddr(const char* addr);
uint16_t vxHtons(uint16_t value);
inline uint16_t vxNtohs(uint16_t netshort);

/* sock option*/
#define SOL_SOCKET 1
#define IPPROTO_IP 0
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17

#define SO_BINDTODEVICE 25 /* bind ke netdvev, default by route table      */
#define SO_RCVBUF 8	   /* ukuran RX buffer                   */
#define SO_SNDBUF 7	   /* ukuran TX buffer                   */
#define SO_BROADCAST 6	   /* izinkan broadcast                  */
#define SO_PROMISC 200	   /* aktifkan promiscuous di NIC terikat */
#define SO_NONBLOCK 201	   /* non-blocking mode                  */

/* error type */
#define SOCK_OK 0
#define SOCK_ERR_NOFD -1    /* tidak ada file descriptor kosong   */
#define SOCK_ERR_INVAL -2   /* argumen tidak valid                */
#define SOCK_ERR_NODEV -3   /* NIC tidak ditemukan                */
#define SOCK_ERR_AGAIN -4   /* non-blocking, tidak ada data       */
#define SOCK_ERR_NOMEM -5   /* buffer pool habis                  */
#define SOCK_ERR_NOTCONN -6 /* belum connect()                    */
#define SOCK_ERR_BADFD -7   /* file descriptor tidak valid        */

#endif // __NET__SOCKET_H__