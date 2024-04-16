#include <stdlib.h>

#include <rte_net.h>
#include <rte_ethdev.h>
#include <rte_flow.h>

#include "vnf_examples.h"


static void
enable_isolate_mode(uint16_t port_id, int enable)
{
    struct rte_flow_error error;
    if (rte_flow_isolate(port_id, enable, &error)) {
        printf("Failed to %s isolate mode on port %u for %s\n", 
                   enable ? "enable" : "disable", port_id, error.message);
        rte_exit(EXIT_FAILURE, "Failed to %s isolate mode on port %u for %s\n", 
                   enable ? "enable" : "disable", port_id, error.message);
    } else {
        printf("Succeed to %s isolate mode on port %u\n",
                  enable ? "enable" : "disable", port_id);
    }
}

void
enable_isolate_mode_init()
{
    uint16_t port_id;

	RTE_ETH_FOREACH_DEV(port_id) {
		enable_isolate_mode(port_id, 1);
    }
}

static struct rte_flow *
dpdk_create_isolate_jump_flow(uint16_t port_id, bool transfer) {
    struct rte_flow_error error;
    struct rte_flow_attr attr;
    struct rte_flow_item pattern[3];
    struct rte_flow_action action[2];
    struct rte_flow_action_jump jump;
    struct rte_flow *flow = NULL;
    struct rte_flow_item_eth eth_spec;
    struct rte_flow_item_eth eth_mask;

    /* 1. flow attribute */
    memset(&attr, 0, sizeof(struct rte_flow_attr));
    attr.priority = 1;
    if (transfer) {
        attr.transfer = 1;
    } else {
        attr.ingress = 1;
    }

    /* 2. flow patterns */
    memset(pattern, 0, sizeof(pattern));
    memset(&eth_spec, 0, sizeof(struct rte_flow_item_eth));
    memset(&eth_mask, 0, sizeof(struct rte_flow_item_eth));
    eth_spec.type = RTE_BE16(RTE_ETHER_TYPE_IPV4);
    eth_mask.type = 0xffff;
    pattern[0].type = RTE_FLOW_ITEM_TYPE_ETH;
    pattern[0].spec = &eth_spec;
    pattern[0].mask = &eth_mask;
    pattern[0].last = NULL;
    pattern[1].type = RTE_FLOW_ITEM_TYPE_IPV4;
    pattern[2].type = RTE_FLOW_ITEM_TYPE_END;

    /* 3. flow actions */
    memset(&jump, 0, sizeof(struct rte_flow_action_jump));
    jump.group = FIRST_TABLE;
    memset(action, 0, sizeof(action));
    action[0].type = RTE_FLOW_ACTION_TYPE_JUMP;
    action[0].conf = &jump;
    action[1].type = RTE_FLOW_ACTION_TYPE_END;

    /* 4. create isolate jump flow */
    int ret = 0;
    ret = rte_flow_validate(port_id, &attr, pattern, action, &error);
    if (!ret)
        flow = rte_flow_create(port_id, &attr, pattern, action, &error);
    if (flow == NULL) {
        rte_exit(EXIT_FAILURE, "create isolate jump flow failed:port_id:%u, transfer:%s, error:%s\n", port_id, transfer ? "true" : "false", error.message);
    } 

    return flow;
}

static struct rte_flow *
dpdk_create_isolate_gre_jump_flow(uint16_t port_id, bool transfer) {
    struct rte_flow_error error;
    struct rte_flow_attr attr;
    struct rte_flow_item pattern[4];
    struct rte_flow_action action[2];
    struct rte_flow_action_jump jump;
    struct rte_flow_item_eth eth_spec;
    struct rte_flow_item_eth eth_mask;
    struct rte_flow_item_ipv4 ip_spec;
    struct rte_flow_item_ipv4 ip_mask;
    struct rte_flow_item_gre gre_spec;
    struct rte_flow_item_gre gre_mask;
    struct rte_flow *flow = NULL;

    /* 1. flow attribute */
    memset(&attr, 0, sizeof(struct rte_flow_attr));
    attr.priority = 1;
    if (transfer) {
        attr.transfer = 1;
    } else {
        attr.ingress = 1;
    }

    /* 2. flow patterns to match gre pkt */
    memset(&eth_spec, 0, sizeof(struct rte_flow_item_eth));
    memset(&eth_mask, 0, sizeof(struct rte_flow_item_eth));
    eth_spec.type = RTE_BE16(RTE_ETHER_TYPE_IPV4);
    eth_mask.type = 0xffff;
    pattern[0].type = RTE_FLOW_ITEM_TYPE_ETH;
    pattern[0].spec = &eth_spec;
    pattern[0].mask = &eth_mask;
    pattern[0].last = NULL;

    memset(&ip_spec, 0, sizeof(struct rte_flow_item_ipv4));
    memset(&ip_mask, 0, sizeof(struct rte_flow_item_ipv4));
    ip_spec.hdr.next_proto_id = IPPROTO_GRE;
    ip_mask.hdr.next_proto_id = 0xff;
    pattern[1].type = RTE_FLOW_ITEM_TYPE_IPV4;
    pattern[1].spec = &ip_spec;
    pattern[1].mask = &ip_mask;
    pattern[1].last = NULL;

    memset(&gre_spec, 0, sizeof(struct rte_flow_item_gre));
    memset(&gre_mask, 0, sizeof(struct rte_flow_item_gre));
    gre_spec.protocol = RTE_BE16(RTE_ETHER_TYPE_TEB);
    gre_mask.protocol = 0xffff;
    pattern[2].type = RTE_FLOW_ITEM_TYPE_GRE;
    pattern[2].spec = &gre_spec;
    pattern[2].mask = &gre_mask;
    pattern[2].last = NULL;

    /* the final level must be always type end */
    pattern[3].type = RTE_FLOW_ITEM_TYPE_END;

    /* 3. flow actions */
    memset(&jump, 0, sizeof(struct rte_flow_action_jump));
    jump.group = FIRST_TABLE;
    memset(action, 0, sizeof(action));
    action[0].type = RTE_FLOW_ACTION_TYPE_JUMP;
    action[0].conf = &jump;
    action[1].type = RTE_FLOW_ACTION_TYPE_END;

    /* 4. create isolate jump flow */
    int ret = 0;
    ret = rte_flow_validate(port_id, &attr, pattern, action, &error);
    if (!ret)
        flow = rte_flow_create(port_id, &attr, pattern, action, &error);
    if (flow == NULL) {
        rte_exit(EXIT_FAILURE, "create isolate gre jump flow failed: port_id:%u, transfer:%s, error:%s\n", port_id, transfer ? "true" : "false", error.message);
    }

    return flow;
}

void 
dpdk_isolate_flows_init() {
    uint16_t port_id = 0;

	RTE_ETH_FOREACH_DEV(port_id) {
        if (0 == port_id) {
            dpdk_create_isolate_gre_jump_flow(port_id, true);
        } else {
            dpdk_create_isolate_jump_flow(port_id, true);
        }
    }

    RTE_ETH_FOREACH_DEV(port_id) {
        if (0 == port_id) {
            dpdk_create_isolate_gre_jump_flow(port_id, false);
        } else {
            dpdk_create_isolate_jump_flow(port_id, false);
        }
    }
}