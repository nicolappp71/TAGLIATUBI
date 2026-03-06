#ifndef MODE_H
#define MODE_H

// Decommenta questa riga quando sei a casa, commentala quando sei in ufficio
#define CASA

#ifdef CASA
#define SERVER_BASE "http://192.168.1.58:10000"
#else
#define SERVER_BASE "http://intranet.cifarelli.loc"
#endif

#endif // MODE_H

// #ifdef CASA
// #define SERVER_BASE "http://192.168.1.58:10000"
// #else
// #define SERVER_BASE "http://intranet.cifarelli.loc"
// #endif
