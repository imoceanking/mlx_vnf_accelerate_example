/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2020 Mellanox Technologies, Ltd
 */
#ifndef RTE_VNF_EXAMPLES_H_
#define RTE_VNF_EXAMPLES_H_
#ifdef  __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define MISS_TABLE_ID    (UINT32_MAX - 1)
#define MAX_FLOW_PRIORITY 10
#define MIN_FLOW_PRIORITY 1
#define INVALID_FLOW_MARK 0
#define HAIRPIN_FLOW_MARK 1

#define NETDEV_DPDK_METER_PORT_ID 0
#define NETDEV_DPDK_METER_POLICY_ID 25
#define NETDEV_DPDK_METER_METER_ID 0

#define FIRST_TABLE 1

//#define ISOLATE_ISOLATE_MODE_DEF    0

int
create_default_flow();

struct rte_flow *
create_gtp_u_decap_rss_flow(uint16_t port, uint32_t nb_queues,
					     uint16_t *queues);

struct rte_flow *
create_gtp_u_inner_ip_rss_flow(uint16_t port, uint32_t nb_queues,
			       uint16_t *queues);

struct rte_flow *
create_gtp_u_encap_flow(uint16_t port);

struct rte_flow *
create_gtp_u_psc_encap_flow(uint16_t port);

int
sync_nic_tx_flows(uint16_t port);

int
sync_all_flows(uint16_t port);

int
hairpin_one_port_setup(uint16_t port, uint64_t nr_hairpin_queue);

int
hairpin_two_ports_setup(uint16_t nr_hairpin_queues);

int
hairpin_two_ports_bind();

int
hairpin_two_ports_unbind();

struct rte_flow *
hairpin_two_ports_flows_create(void);

struct rte_flow *
hairpin_one_port_flows_create(void);

struct rte_flow *
create_flow_with_tag(uint16_t port);

struct rte_flow *
create_flow_with_sampling(uint16_t port);

struct rte_flow *
create_flow_with_mirror(uint16_t port, uint16_t mirror2port, uint16_t fwd2port);

int
create_symmetric_rss_flow(uint16_t port, uint32_t nb_queues, uint16_t *queues);

int
create_meter_policy_profile_meter(uint16_t port_id);

int
create_flow_with_meter_in_transfer(uint16_t port_id);

int
create_flow_with_meter(uint16_t port);

int
get_meter_stats(uint16_t port_id, uint32_t mtr_id);

int
create_gtp_u_qfi_flow(uint16_t port);

int
create_flow_with_age(uint16_t port);

int
register_aged_event(uint16_t port);

struct rte_flow *
create_gre_decap_rss_flow(uint16_t port, uint32_t nb_queues, uint16_t *queues);

struct rte_flow *create_gre_encap_flow(uint16_t port);

int create_hairpin_meta_flow(void);
struct rte_flow *
create_nic_flow_with_mirror(uint16_t port_id, uint16_t mirror2queue,
		uint16_t fwd2queue);

struct rte_flow *
create_gtp_u_inner_ip_shared_rss_flow(uint16_t port, uint32_t nb_queues,
			       uint16_t *queues);
int
create_flow_with_counter(uint16_t port);

int
query_counters(uint16_t port);

int
create_modify_gtp_teid_flows(uint16_t port_id);

void
enable_isolate_mode_init();

void 
dpdk_isolate_flows_init();
#ifdef  __cplusplus
}
#endif
#endif
