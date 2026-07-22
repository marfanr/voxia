#ifndef __TTY__TERMIOS_H__
#define __TTY__TERMIOS_H__

typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int speed_t;

#define NCCS 32

struct termios {
	tcflag_t c_iflag;
	tcflag_t c_oflag;
	tcflag_t c_cflag;
	tcflag_t c_lflag;
	cc_t c_line;
	cc_t c_cc[NCCS];
	speed_t __c_ispeed;
	speed_t __c_ospeed;
};

#define ISIG   0000001
#define ICANON 0000002
#define ECHO   0000010

#define INLCR  0000100
#define IGNCR  0000200
#define ICRNL  0000400

#endif // __TTY__TERMIOS_H__