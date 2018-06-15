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


	uint32_t req = 0;
	while(1){
		while(sys_page_alloc(0, &nsipcbuf, PTE_U|PTE_P|PTE_W) < 0);	

		while(1){
			nsipcbuf.pkt.jp_len = sys_recv_pkt(nsipcbuf.pkt.jp_data);
			if(nsipcbuf.pkt.jp_len < 0)
				continue;
			sys_yield();
			break;
		}
		
		while(sys_ipc_try_send(ns_envid, NSREQ_INPUT, &nsipcbuf, PTE_U|PTE_W|PTE_P) < 0);
	}
}
