/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2017 Mellanox Technologies, Ltd
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <netinet/in.h>
#include <setjmp.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>

#include <rte_eal.h>
#include <rte_common.h>
#include <rte_malloc.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_net.h>
#include <rte_flow.h>
#include <rte_cycles.h>
#include <rte_timer.h>
#include "main.h"

static volatile bool force_quit;

static uint16_t port_id;
static uint32_t nr_std_queues = 8;
static uint16_t queues[] = {1, 3, 2, 4, 5, 7, 0, 6};
struct rte_mempool *mbufPool;
struct rte_flow *offloaded_flow;
static uint16_t nr_hairpin_queues = 1;

#define SRC_IP ((0<<24) + (0<<16) + (0<<8) + 0) /* src ip = 0.0.0.0 */
#define DEST_IP ((192<<24) + (168<<16) + (1<<8) + 1) /* dest ip = 192.168.1.1 */
#define FULL_MASK 0xffffffff /* full mask */
#define EMPTY_MASK 0x0 /* empty mask */

static inline void
print_ether_addr(const char *what, struct rte_ether_addr *eth_addr)
{
	char buf[RTE_ETHER_ADDR_FMT_SIZE];
	rte_ether_format_addr(buf, RTE_ETHER_ADDR_FMT_SIZE, eth_addr);
	printf("%s%s", what, buf);
}

static inline void
dump_pkt_info(struct rte_mbuf *m, uint16_t qi)
{
	struct rte_ether_hdr *eth_hdr;

	eth_hdr = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
	print_ether_addr("src=", &eth_hdr->src_addr);
	print_ether_addr(" - dst=", &eth_hdr->dst_addr);
	printf(" - queue=0x%x",(unsigned int)qi);
	uint64_t ol_flags = m->ol_flags;
	if (ol_flags & RTE_MBUF_F_RX_RSS_HASH) {
		printf(" - RSS hash=0x%x", (unsigned int) m->hash.rss);
		printf(" - RSS queue=0x%x", (unsigned int) qi);
	}
	if (ol_flags & RTE_MBUF_F_RX_FDIR) {
		printf(" - FDIR matched ");
		if (ol_flags & RTE_MBUF_F_RX_FDIR_ID)
			printf("ID=0x%x", m->hash.fdir.hi);
		else if (ol_flags & RTE_MBUF_F_RX_FDIR_FLX)
			printf("flex bytes=0x%08x %08x",
			       m->hash.fdir.hi, m->hash.fdir.lo);
		else
			printf("hash=0x%x ID=0x%x ",
			       m->hash.fdir.hash, m->hash.fdir.id);
	}
	printf("\n");
}

static void
timer_callback(__rte_unused struct rte_timer *tim,
		__rte_unused void *arg)
{
	uint16_t port_id;
	RTE_ETH_FOREACH_DEV(port_id) {
		get_meter_stats(port_id, NETDEV_DPDK_METER_METER_ID);
	}
}

static int
timer_main_loop(__rte_unused void* arg)
{
	printf("timer main loop start\n");
	struct rte_timer tim;
	rte_timer_init(&tim);
	uint64_t hz = rte_get_timer_hz();
	unsigned lcore_id = rte_lcore_id();
	//10s
	int ret = 0;
	for (int i = 0; i < 10; i++) {
		ret = rte_timer_reset(&tim, hz * 10, PERIODICAL, lcore_id, timer_callback, &tim);
		if (ret != 0) {
			printf("timer reset failed, ret=%d\n", ret);
			rte_delay_ms(1000);
		} else {
			break;
		}
	}

	while (!force_quit) {

		/* call the timer handler on each core */
		ret = rte_timer_manage();

		rte_delay_ms(1000);
	}
	return 0;
}

static int
main_loop(__rte_unused void* arg)
{
	struct rte_mbuf *mbufs[32];
	struct rte_flow_error error;
	uint16_t nb_rx;
	uint16_t nb_tx;
	uint16_t i;
	uint16_t j;
	printf("main loop start\n");
	while (!force_quit) {
		for (i = 0; i < nr_std_queues; i++) {
			nb_rx = rte_eth_rx_burst(port_id,
						i, mbufs, 32);
			if (nb_rx) {
				for (j = 0; j < nb_rx; j++) {
					struct rte_mbuf *m = mbufs[j];

					dump_pkt_info(m, i);
				}
				nb_tx = rte_eth_tx_burst(port_id, i,
						mbufs, nb_rx);
			}
			/* Free any unsent packets. */
			if (unlikely(nb_tx < nb_rx)) {
				uint16_t buf;
				for (buf = nb_tx; buf < nb_rx; buf++)
					rte_pktmbuf_free(mbufs[buf]);
			}
		}
	}

	/* closing and releasing resources */
	RTE_ETH_FOREACH_DEV(port_id) {
		rte_flow_flush(port_id, &error);
	}
	if ( 2 == rte_eth_dev_count_avail())
		hairpin_two_ports_unbind();

	RTE_ETH_FOREACH_DEV(port_id) {
		rte_eth_dev_stop(port_id);
		rte_eth_dev_close(port_id);
	}
	return 0;
}

#define CHECK_INTERVAL 1000  /* 100ms */
#define MAX_REPEAT_TIMES 90  /* 9s (90 * 100ms) in total */

static void
assert_link_status(void)
{
	struct rte_eth_link link;
	uint8_t rep_cnt = MAX_REPEAT_TIMES;
	int link_get_err = -EINVAL;

	memset(&link, 0, sizeof(link));
	do {
		link_get_err = rte_eth_link_get(port_id, &link);
		if (link_get_err == 0 && link.link_status == RTE_ETH_LINK_UP)
			break;
		rte_delay_ms(CHECK_INTERVAL);
	} while (--rep_cnt);

	if (link_get_err < 0)
		rte_exit(EXIT_FAILURE, ":: error: link get is failing: %s\n",
			 rte_strerror(-link_get_err));
	if (link.link_status == RTE_ETH_LINK_DOWN)
		rte_exit(EXIT_FAILURE, ":: error: link is still down\n");
}

static void
init_port(uint16_t port_id)
{
	int ret;
	uint16_t i;
	struct rte_eth_conf port_conf;
	memset(&port_conf, 0, sizeof(port_conf));
	port_conf.txmode.offloads = RTE_ETH_TX_OFFLOAD_VLAN_INSERT |
								RTE_ETH_TX_OFFLOAD_IPV4_CKSUM  |
								RTE_ETH_TX_OFFLOAD_UDP_CKSUM   |
								RTE_ETH_TX_OFFLOAD_TCP_CKSUM   |
								RTE_ETH_TX_OFFLOAD_SCTP_CKSUM  |
								RTE_ETH_TX_OFFLOAD_TCP_TSO;

	struct rte_eth_txconf txq_conf;
	struct rte_eth_rxconf rxq_conf;
	struct rte_eth_dev_info dev_info;

	ret = rte_eth_dev_info_get(port_id, &dev_info);
	if (ret != 0)
		rte_exit(EXIT_FAILURE,
			"Error during getting device (port %u) info: %s\n",
			port_id, strerror(-ret));

	port_conf.txmode.offloads &= dev_info.tx_offload_capa;
	printf(":: initializing port: %d\n", port_id);
	ret = rte_eth_dev_configure(port_id,
				nr_std_queues + nr_hairpin_queues,
				nr_std_queues + nr_hairpin_queues, &port_conf);
	if (ret < 0) {
		rte_exit(EXIT_FAILURE,
			":: cannot configure device: err=%d, port=%u\n",
			ret, port_id);
	}

	rxq_conf = dev_info.default_rxconf;
	rxq_conf.offloads = port_conf.rxmode.offloads;
	for (i = 0; i < nr_std_queues; i++) {
		ret = rte_eth_rx_queue_setup(port_id, i, 512,
				     rte_eth_dev_socket_id(port_id),
				     &rxq_conf,
				     mbufPool);
		if (ret < 0) {
			rte_exit(EXIT_FAILURE,
				":: Rx queue setup failed: err=%d, port=%u\n",
				ret, port_id);
		}
	}

	txq_conf = dev_info.default_txconf;
	txq_conf.offloads = port_conf.txmode.offloads;

	for (i = 0; i < nr_std_queues; i++) {
		ret = rte_eth_tx_queue_setup(port_id, i, 512,
				rte_eth_dev_socket_id(port_id),
				&txq_conf);
		if (ret < 0) {
			rte_exit(EXIT_FAILURE,
				":: Tx queue setup failed: err=%d, port=%u\n",
				ret, port_id);
		}
	}

#ifndef ISOLATE_ISOLATE_MODE_DEF
	ret = rte_eth_promiscuous_enable(port_id);
	if (ret != 0)
		rte_exit(EXIT_FAILURE,
			":: promiscuous mode enable failed: err=%s, port=%u\n",
			rte_strerror(-ret), port_id);
#endif


	printf(":: initializing port: %d done\n", port_id);
}

static void
init_ports(void)
{
	uint16_t port_id;

	RTE_ETH_FOREACH_DEV(port_id) {
		init_port(port_id);
	}
}

static void
start_ports(void)
{
	uint16_t port_id;
	int ret;

	RTE_ETH_FOREACH_DEV(port_id) {
		ret = rte_eth_dev_start(port_id);
		if (ret) {
			rte_exit(EXIT_FAILURE,
				"rte_eth_dev_start:err=%d, port=%u\n",
				ret, port_id);
		}
		assert_link_status();
	}

}

static void
signal_handler(int signum)
{
	if (signum == SIGINT || signum == SIGTERM) {
		printf("\n\nSignal %d received, preparing to exit...\n",
				signum);
		force_quit = true;
	}
}

static void
create_meters()
{
	uint16_t port_id;
	RTE_ETH_FOREACH_DEV(port_id) {
		printf(":: create meter policy/profile/meter_id, port_id=%u\n", port_id);
		if (create_meter_policy_profile_meter(port_id)) {
			printf("meter policy/profile/meter_id cannot be created\n");
			rte_exit(EXIT_FAILURE, "error in create_meter_policy_profile_meter");
		}
		printf("done\n");
                if (port_id) {
			printf(":: create offloaded_flow with meter..., port_id=%u\n", port_id);
			if (create_flow_with_meter_in_transfer(port_id)) {
				printf("Flow with meter cannot be created\n");
				rte_exit(EXIT_FAILURE, "error in creating offloaded_flow");
			}
			printf("done\n");
		}
	}
}

static void
set_hairpin_queues(uint16_t nr_ports)
{
	int ret;
	uint16_t port_id;
	printf(":: %u ports active, setup %u ports hairpin...",
			nr_ports, nr_ports);
	if (nr_ports == 2)
		hairpin_two_ports_setup(nr_hairpin_queues);
	else {
		RTE_ETH_FOREACH_DEV(port_id) {
			ret = hairpin_one_port_setup(port_id, nr_hairpin_queues);
			if (ret) {
				printf("\nCannot setup hairpin queues for port %u\n", port_id);
				rte_exit(EXIT_FAILURE, "error in hairpin_one_port_setup");
			}
		}
	}
		
	printf("done\n");
}

static void
bind_two_ports_hairpin(uint16_t nr_ports)
{
	int ret;
	if (nr_ports == 2) {
		printf(":: %u ports hairpin bind...", nr_ports);
		ret = hairpin_two_ports_bind();
		if (ret)
			rte_exit(EXIT_FAILURE, "Cannot bind two hairpin ports");
		printf("done\n");
	}
}


int
main(int argc, char **argv)
{
	int ret;
	uint16_t nr_ports;


	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, ":: invalid EAL arguments\n");

	force_quit = false;
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	nr_ports = rte_eth_dev_count_avail();
	if (nr_ports == 0)
		rte_exit(EXIT_FAILURE, ":: no Ethernet ports found\n");
	port_id = 0;
	if (nr_ports != 1 && nr_ports != 2) {
		printf(":: warn: %d ports detected, but we use two ports at max\n",
			nr_ports);
	}
	mbufPool = rte_pktmbuf_pool_create("mbufPool", 40960, 128, 0,
					    RTE_MBUF_DEFAULT_BUF_SIZE,
					    rte_socket_id());
	if (mbufPool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot init mbuf pool\n");

#ifdef ISOLATE_ISOLATE_MODE_DEF
	enable_isolate_mode_init();
#endif
	init_ports();
	set_hairpin_queues(nr_ports);
	start_ports();
	bind_two_ports_hairpin(nr_ports);
	
	// printf(":: create hairpin flows...");
	// if (nr_ports == 2)
	// 	offloaded_flow = hairpin_two_ports_flows_create();
	// else
	// 	offloaded_flow = hairpin_one_port_flows_create();

	// if (!offloaded_flow) {
	// 	printf("Hairpin flows can't be created\n");
	// 	rte_exit(EXIT_FAILURE, "error in creating offloaded_flow");
	// }
	// printf("done\n");

	printf(":: create default flow");
	if (create_default_flow()) {
		printf("\n create default flow failed\n");
		rte_exit(EXIT_FAILURE, "error in create_default_flow");
	}
	printf("done\n");
	
	// printf(":: create offloaded_flow with symmetric RSS action...");
	// if (create_symmetric_rss_flow(port_id, nr_std_queues, queues)){
	// 	printf("Flow with symmetric RSS cannot be created\n");
	// 	rte_exit(EXIT_FAILURE, "error in creating offloaded_flow");
	// }
	// printf("done\n");

	create_meters();

	// printf(":: create GRE RSS offloaded_flow ..");
	// offloaded_flow = create_gre_decap_rss_flow(port_id, nr_std_queues, queues);
	// if (!offloaded_flow) {
	// 	printf("GRE RSS decap flows cannot be created\n");
	// 	rte_exit(EXIT_FAILURE, "error in creating offloaded_flow");
	// }
	// printf("done\n");
	// printf(":: create GRE encap offloaded_flow ..");
	// offloaded_flow = create_gre_encap_flow(port_id);
	// if (!offloaded_flow) {
	// 	printf("GRE encap offloaded_flow cannot be created\n");
	// 	rte_exit(EXIT_FAILURE, "error in create offloaded_flow");
	// }
	// printf("done\n");
	// if (nr_ports == 2) {
	// 	printf(":: create hairpin offloaded_flow with meta ..");
	// 	if (create_hairpin_meta_flow()) {
	// 		printf("Hairpin offloaded_flow with meta data cannot be created\n");
	// 		rte_exit(EXIT_FAILURE, "error in create offloaded_flow");
	// 	}
	// 	printf("done\n");
	// }

	// printf(":: create flows with counter ..");
	// if (create_flow_with_counter(port_id)) {
	// 	printf("Flows with counter cannot be created\n");
	// 	rte_exit(EXIT_FAILURE, "error in creating offloaded_flow");
	// }
	// printf("done\n");
	// ret = sync_all_flows(port_id);
	// if (ret) {
	// 	printf("Failed to sync flows, flows may not take effect!\n");
	// 	rte_exit(EXIT_FAILURE, "error to sync flows");
	// }

	// printf(":: query counters ...\n");
	// if (query_counters(port_id)) {
	// 	printf("Failed to query counters\n");
	// 	rte_exit(EXIT_FAILURE, "error to sync flows");
	// }
	// printf("done\n");
	rte_timer_subsystem_init();
	uint32_t lcore_1 = rte_get_next_lcore(-1, 1, 0);
	uint32_t lcore_2 = rte_get_next_lcore(lcore_1, 1, 0);
	rte_eal_remote_launch(main_loop, NULL, lcore_1);
	rte_eal_remote_launch(timer_main_loop, NULL, lcore_2);

	rte_eal_mp_wait_lcore();

	return 0;
}
