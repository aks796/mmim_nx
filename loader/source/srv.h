/* srv.h -- no offline server in this port
 *
 * mkt_nx answered the game's network from an in-process server; net_shim.c
 * and the watchdog still ask whether one runs. Pixel Gun talks to the real
 * internet, so the answer is always no and those paths stay dead.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef SRV_H
#define SRV_H

#include <switch.h>

typedef struct MemConn MemConn;

#define SRV_FAKE_IP_BE 0x010200c0u // 192.0.2.1, never handed out here

static inline int srv_port(int tls) { (void)tls; return 0; }
static inline void srv_accept_conn(MemConn *m, int tls) { (void)m, (void)tls; }
static inline Handle srv_thread_handle(void) { return 0; }
static inline unsigned srv_rounds(void) { return 0; }

#endif
