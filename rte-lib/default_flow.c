#include <rte_net.h>
#include <rte_ethdev.h>
#include <rte_flow.h>

#include "vnf_examples.h"

enum layer_name {
	L2,
	L3,
	L4,
	TUNNEL,
	L2_INNER,
	L3_INNER,
	L4_INNER,
	END
};

static struct rte_flow_item pattern[] = {
	[L2] = { /* ETH type is set since we always start from ETH. */
		.type = RTE_FLOW_ITEM_TYPE_ETH,
		.spec = NULL,
		.mask = NULL,
		.last = NULL },
	[L3] = {
		.type = RTE_FLOW_ITEM_TYPE_VOID,
		.spec = NULL,
		.mask = NULL,
		.last = NULL },
	[L4] = {
		.type = RTE_FLOW_ITEM_TYPE_VOID,
		.spec = NULL,
		.mask = NULL,
		.last = NULL },
	[TUNNEL] = {
		.type = RTE_FLOW_ITEM_TYPE_VOID,
		.spec = NULL,
		.mask = NULL,
		.last = NULL },
	[L2_INNER] = {
		.type = RTE_FLOW_ITEM_TYPE_VOID,
		.spec = NULL,
		.mask = NULL,
		.last = NULL },
	[L3_INNER] = {
		.type = RTE_FLOW_ITEM_TYPE_VOID,
		.spec = NULL,
		.mask = NULL,
		.last = NULL },
	[L4_INNER] = {
		.type = RTE_FLOW_ITEM_TYPE_VOID,
		.spec = NULL,
		.mask = NULL,
		.last = NULL },
	[END] = {
		.type = RTE_FLOW_ITEM_TYPE_END,
		.spec = NULL,
		.mask = NULL,
		.last = NULL },
};

static int
create_jump_to_group_first(uint16_t port_id)
{
    struct rte_flow *flow;
	struct rte_flow_error error;
	struct rte_flow_attr attr = { /* holds the flow attributes. */
				.group = 0, /* set the rule on the main group. */
				.transfer = 1,/* rx flow. */
				.priority = MIN_FLOW_PRIORITY, }; /* add priority to rule
				to give the decap rule higher priority since
				it is more specific */

	struct rte_flow_action_jump jump = {.group = FIRST_TABLE};
	struct rte_flow_action root_actions[] = {
		[0] = {
			.type = RTE_FLOW_ACTION_TYPE_JUMP,
			.conf = &jump,
		},
		[1] = {
			.type = RTE_FLOW_ACTION_TYPE_END,
		},
	};

	pattern[L2].type = RTE_FLOW_ITEM_TYPE_ETH;
	pattern[L3].type = RTE_FLOW_ITEM_TYPE_END;
	flow = rte_flow_create(port_id, &attr, pattern, root_actions, &error);
	if (!flow) {
		printf("can't create default transfer jump flow on root table,port id:%u, error: %s\n", port_id, error.message);
		return -1;
	}
	attr.ingress = 1;
	attr.transfer = 0;
	flow = rte_flow_create(port_id, &attr, pattern, root_actions, &error);
	if (!flow) {
		printf("can't create default transfer jump flow on root table,port id:%u, error: %s\n", port_id, error.message);
		return -1;
	}
    return 0;
}

static int
create_miss_table(uint16_t port_id)
{
    struct rte_flow *flow;
	struct rte_flow_error error;
	struct rte_flow_attr attr = { /* holds the flow attributes. */
				.group = 1, /* set the rule on the main group. */
				.transfer = 1,/* rx flow. */
				.priority = MAX_FLOW_PRIORITY, }; /* add priority to rule
				to give the decap rule higher priority since
				it is more specific */

	struct rte_flow_action_jump jump = {.group = MISS_TABLE_ID};
	struct rte_flow_action root_actions[] = {
		[0] = {
			.type = RTE_FLOW_ACTION_TYPE_JUMP,
			.conf = &jump,
		},
		[1] = {
			.type = RTE_FLOW_ACTION_TYPE_END,
		},
	};

	pattern[L2].type = RTE_FLOW_ITEM_TYPE_ETH;
	pattern[L3].type = RTE_FLOW_ITEM_TYPE_END;
	flow = rte_flow_create(port_id, &attr, pattern, root_actions, &error);
	if (!flow) {
		printf("can't create default miss flow on first table,port id:%u, error: %s\n", port_id, error.message);
		return -1;
	}
    return 0;
}

static int
create_hairpin_flow_table(uint16_t port_id)
{
    int ret;
    struct rte_eth_dev_info dev_info = { 0 };
    ret = rte_eth_dev_info_get(port_id, &dev_info);
	if (ret)
		rte_exit(EXIT_FAILURE, "Error: can't get device info, port id:"
				" %u\n", port_id);
    struct rte_flow *flow;
	struct rte_flow_error error;
	struct rte_flow_attr attr = { /* holds the flow attributes. */
				.group = 1, /* set the rule on the main group. */
				.ingress = 1,/* rx flow. */
				.priority = MIN_FLOW_PRIORITY, }; /* add priority to rule
				to give the decap rule higher priority since
				it is more specific */

    struct rte_flow_action_queue queue = {.index = dev_info.nb_rx_queues - 1};
	struct rte_flow_action root_actions[] = {
		[0] = {
			.type = RTE_FLOW_ACTION_TYPE_QUEUE,
			.conf = &queue,
		},
		[1] = {
			.type = RTE_FLOW_ACTION_TYPE_END,
		},
	};
    struct rte_flow_item_mark hp_mark;

	pattern[L2].type = RTE_FLOW_ITEM_TYPE_MARK;
    pattern[L2].spec = &hp_mark;
	pattern[L3].type = RTE_FLOW_ITEM_TYPE_END;
    hp_mark.id = HAIRPIN_FLOW_MARK;
	flow = rte_flow_create(port_id, &attr, pattern, root_actions, &error);
	if (!flow) {
		printf("can't create default hairpin flow on root table,port id:%u, error: %s\n", port_id, error.message);
		return -1;
	}
    return 0;
}

int
create_default_flow()
{
    uint16_t port_id;

#ifdef ISOLATE_ISOLATE_MODE_DEF
	dpdk_isolate_flows_init();
#else
	RTE_ETH_FOREACH_DEV(port_id) {
        if(create_jump_to_group_first(port_id)) {
            return -1;
        }
    }
#endif

    RTE_ETH_FOREACH_DEV(port_id) {
        if(create_miss_table(port_id)) {
            return -1;
        }
    }

    RTE_ETH_FOREACH_DEV(port_id) {
        if(create_hairpin_flow_table(port_id)) {
            return -1;
        }
    }
    
    return 0;
}