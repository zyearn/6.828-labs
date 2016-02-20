#include "ns.h"

extern union Nsipc nsipcbuf;

void
input(envid_t ns_envid)
{
	binaryname = "ns_input";

	// LAB 6: Your code here:
	// 	- read a packet from the device driver
	//	- send it to the network server
	// Hint: When you IPC a page to the network server, it will be
	// reading from it for a while, so don't immediately receive
	// another packet in to the same physical page.
    #define RCV_PKT_SIZE 2048
    char buf[RCV_PKT_SIZE];
    size_t len;
    int r;

    while (1) {
        while ((r = sys_net_try_receive(buf, &len)) < 0) {
            sys_yield();
        }

        while ((r = sys_page_alloc(0, &nsipcbuf, PTE_U|PTE_P|PTE_W)) < 0) {
            panic("input: sys_page_alloc %e", r);
        }

        nsipcbuf.pkt.jp_len = len;
        memmove(nsipcbuf.pkt.jp_data, buf, len);
            
        while ((r = sys_ipc_try_send(ns_envid, NSREQ_INPUT, &nsipcbuf, PTE_U|PTE_P|PTE_W)) < 0);
    }
}
