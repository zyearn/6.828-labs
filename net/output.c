#include "ns.h"

extern union Nsipc nsipcbuf;

void
output(envid_t ns_envid)
{
	binaryname = "ns_output";

	// LAB 6: Your code here:
	// 	- read a packet from the network server
	//	- send the packet to the device driver
    int r;

    while (1) {
        if ((r = sys_ipc_recv(&nsipcbuf)) < 0) {
            cprintf("output: sys_ipc_recv %e", r);
            continue;
        }

        if ((thisenv->env_ipc_from != ns_envid) ||
                (thisenv->env_ipc_value != NSREQ_OUTPUT)) {
            continue;        
        }

        while ((r = sys_net_try_send(nsipcbuf.pkt.jp_data, nsipcbuf.pkt.jp_len)) < 0) {
            sys_yield();
        }
    }
}
