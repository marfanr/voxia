#ifndef __NET__SOCKET_H__
#define __NET__SOCKET_H__

#include "ioforge/ioforge_nic.h"
#include "llist.h"
#include "net/netdev.h"
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

typedef struct sockaddr_in {
	uint16_t sin_family; /* AF_INET */
	uint16_t sin_port;   /* port dalam network byte order */
	uint32_t sin_addr;   /* IP dalam network byte order   */
	uint8_t _pad[8];
} sockaddr_in_t;

struct sockaddr_un {
	uint16_t sun_family;
	char sun_path[108];
};

// follwoing musl
// #define SHUT_RD 0
// #define SHUT_WR 1
// #define SHUT_RDWR 2

// #ifndef SOCK_CLOEXEC
// #define SOCK_CLOEXEC 02000000
// #define SOCK_NONBLOCK 04000
// #endif

typedef enum {
	AF_UNSPEC = 0,
	AF_LOCAL = 1,
	AF_UNIX = 1,       /* alias AF_LOCAL */
	AF_FILE = 1,       /* alias AF_LOCAL */
	AF_INET = 2,
	AF_AX25 = 3,
	AF_IPX = 4,
	AF_APPLETALK = 5,
	AF_NETROM = 6,
	AF_BRIDGE = 7,
	AF_ATMPVC = 8,
	AF_X25 = 9,
	AF_INET6 = 10,
	AF_ROSE = 11,
	AF_DECnet = 12,
	AF_NETBEUI = 13,
	AF_SECURITY = 14,
	AF_KEY = 15,
	AF_NETLINK = 16,
	AF_ROUTE = 16,     /* alias AF_NETLINK */
	AF_PACKET = 17,
	AF_ASH = 18,
	AF_ECONET = 19,
	AF_ATMSVC = 20,
	AF_RDS = 21,
	AF_SNA = 22,
	AF_IRDA = 23,
	AF_PPPOX = 24,
	AF_WANPIPE = 25,
	AF_LLC = 26,
	AF_IB = 27,
	AF_MPLS = 28,
	AF_CAN = 29,
	AF_TIPC = 30,
	AF_BLUETOOTH = 31,
	AF_IUCV = 32,
	AF_RXRPC = 33,
	AF_ISDN = 34,
	AF_PHONET = 35,
	AF_IEEE802154 = 36,
	AF_CAIF = 37,
	AF_ALG = 38,
	AF_NFC = 39,
	AF_VSOCK = 40,
	AF_KCM = 41,
	AF_QIPCRTR = 42,
	AF_SMC = 43,
	AF_XDP = 44,
	AF_MAX = 45,
} sock_family_t;

typedef enum {
	SOCK_STREAM = 1, /* stream (TCP-style, connection-oriented) */
	SOCK_DGRAM,      /* datagram (UDP-style, connectionless)    */
	SOCK_RAW,
	SOCK_RDM,
	SOCK_SEQPACKET,
	SOCK_DCCP,
	SOCK_PACKET,
} sock_type_t;

typedef enum {
	SOCK_STATE_CLOSED = 0,
	SOCK_STATE_BOUND,
	SOCK_STATE_LISTENING,
	SOCK_STATE_CONNECTED,
} sock_state_t;

/* Base socket type — common to all families */
struct socket {
	sock_family_t family;
	sock_type_t type;
	uint16_t protocol;
	uint32_t flags;
	int backlog;
	sock_state_t state;
	void* ops; /* per-family ops table */
};

typedef struct socket socket_t;

/* AF_INET socket */
struct inet_socket {
	socket_t base; /* MUST be first — safe cast to socket_t* */
	netdev_t* netdev;
	uint8_t nonblocking;
	uint8_t broadcast;
	sockaddr_in_t local_addr;
	sockaddr_in_t remote_addr;
	uint64_t rx_packets;
	uint64_t tx_packets;
	uint64_t rx_dropped;
	uint8_t _in_use;
};

/* AF_UNIX socket */
#define UNIX_BACKLOG_MAX 128
#define UNIX_BUF_SIZE   4096

struct unix_socket {
	socket_t base; /* MUST be first */
	char path[108];
	struct llist_head accept_queue;
	socket_t* pending[UNIX_BACKLOG_MAX];
	int pending_head;
	int pending_tail;
	int pending_count;
	/* ring buffer for recv (data from peer) */
	char rbuf[UNIX_BUF_SIZE];
	int rhead;
	int rtail;
	int rcount;
	/* linked peer socket */
	struct unix_socket* peer;
	/* blocked threads for accept/recv wake-up */
	struct thread* blocked_accept_thread;
	struct thread* blocked_recv_thread;
};

/* ops table — works on base socket_t*, families cast internally */
typedef struct socket_ops {
	int (*recv)(socket_t* socket, void* buffer, size_t size);
	int (*recv_zc)(socket_t* socket, void** buffer, size_t size);
	int (*bind)(socket_t* socket, sockaddr_in_t* addr, uint32_t len);
	int (*set_sockopt)(socket_t* socket, uint32_t level, uint32_t optname,
	                   const void* optval, uint32_t optlen);
} inet_socket_ops_t;

int create_socket(sock_family_t family, sock_type_t type, uint16_t protocol,
                   socket_t** socket);

/* sock option*/
#define SOL_SOCKET 1
#define IPPROTO_IP 0
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17

#define SO_BINDTODEVICE 25 /* bind ke netdvev, default by route table      */
#define SO_RCVBUF 8        /* ukuran RX buffer                   */
#define SO_SNDBUF 7        /* ukuran TX buffer                   */
#define SO_BROADCAST 6     /* izinkan broadcast                  */
#define SO_PROMISC 200     /* aktifkan promiscuous di NIC terikat */
#define SO_NONBLOCK 201    /* non-blocking mode                  */

/* error type */
#define SOCK_OK 0
#define SOCK_ERR_NOFD -1    /* tidak ada file descriptor kosong   */
#define SOCK_ERR_INVAL -2   /* argumen tidak valid                */
#define SOCK_ERR_NODEV -3   /* NIC tidak ditemukan                */
#define SOCK_ERR_AGAIN -4   /* non-blocking, tidak ada data       */
#define SOCK_ERR_NOMEM -5   /* buffer pool habis                  */
#define SOCK_ERR_NOTCONN -6 /* belum connect()                    */

#endif // __NET__SOCKET_H__