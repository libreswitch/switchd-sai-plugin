/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#include <inttypes.h>

#include <util.h>
#include <hmap.h>
#include <hash.h>
#include <list.h>
#include <sai-log.h>
#include <sai-handle.h>

#include <sai-host-intf.h>
#include <sai-policer.h>
#include <sai-api-class.h>
#include <sai-port.h>

#define SAI_TRAP_GROUP_ARP "sai_trap_group_arp"
#define SAI_TRAP_GROUP_BGP "sai_trap_group_bgp"
#define SAI_TRAP_GROUP_DHCP "sai_trap_group_dhcp"
#define SAI_TRAP_GROUP_DHCPV6 "sai_trap_group_dhcpv6"
#define SAI_TRAP_GROUP_LACP "sai_trap_group_lacp"
#define SAI_TRAP_GROUP_LLDP "sai_trap_group_lldp"
#define SAI_TRAP_GROUP_IP2ME "sai_trap_group_ip2me"
#define SAI_TRAP_GROUP_OSFP "sai_trap_group_osfp"
#define SAI_TRAP_GROUP_S_FLOW "sai_trap_group_s_flow"
#define SAI_TRAP_GROUP_STP "sai_trap_group_stp"

VLOG_DEFINE_THIS_MODULE(sai_host_intf);

static const struct ops_sai_trap_group_config trap_group_config_table[] = { {
        .name = SAI_TRAP_GROUP_ARP,
        .trap_ids = {
            SAI_HOSTIF_TRAP_ID_ARP_REQUEST,
            SAI_HOSTIF_TRAP_ID_ARP_RESPONSE,
            SAI_HOSTIF_TRAP_ID_IPV6_NEIGHBOR_DISCOVERY,
            -1
        },
        .priority = 2,
        .policer_config = {
            .rate_max = 1000,
            .burst_max = 1000,
        },
        .is_log = true,
        .is_l3 = true,
    }, {
        .name = SAI_TRAP_GROUP_DHCP,
        .trap_ids = {
            SAI_HOSTIF_TRAP_ID_DHCP,
            -1
        },
        .priority = 3,
        .policer_config = {
            .rate_max = 1000,
            .burst_max = 1000,
        },
        .is_log = true,
        .is_l3 = true,
    }, {
        .name = SAI_TRAP_GROUP_DHCPV6,
        .trap_ids = {
            SAI_HOSTIF_TRAP_ID_DHCPV6,
            -1
        },
        .priority = 3,
        .policer_config = {
            .rate_max = 1000,
            .burst_max = 1000,
        },
        .is_log = true,
        .is_l3 = true,
    }, {
        .name = SAI_TRAP_GROUP_LACP,
        .trap_ids = {
            SAI_HOSTIF_TRAP_ID_LACP,
            -1
        },
        .priority = 5,
        .policer_config = {
            .rate_max = 1000,
            .burst_max = 1000,
        },
        .is_log = false,
        .is_l3 = false,
    }, {
        .name = SAI_TRAP_GROUP_LLDP,
        .trap_ids = {
            SAI_HOSTIF_TRAP_ID_LLDP,
            -1
        },
        .priority = 5,
        .policer_config = {
            .rate_max = 1000,
            .burst_max = 1000,
        },
        .is_log = false,
        .is_l3 = false,
    }, {
        .name = SAI_TRAP_GROUP_IP2ME,
        .trap_ids = {
            SAI_HOSTIF_TRAP_ID_IP2ME,
            -1
        },
        .priority = 4,
        .policer_config = {
            .rate_max = 5000,
            .burst_max = 5000,
        },
        .is_log = false,
        .is_l3 = true,
        }, {
        .name = SAI_TRAP_GROUP_OSFP,
        .trap_ids = {
            SAI_HOSTIF_TRAP_ID_OSPF,
            SAI_HOSTIF_TRAP_ID_OSPFV6,
            -1
        },
        .priority = 4,
        .policer_config = {
            .rate_max = 5000,
            .burst_max = 5000,
        },
        .is_log = false,
        .is_l3 = true,
    }, {
        .name = SAI_TRAP_GROUP_S_FLOW,
        .trap_ids = {
            SAI_HOSTIF_TRAP_ID_SAMPLEPACKET,
            -1
        },
        .priority = 0,
        .policer_config = {
            .rate_max = 2000,
            .burst_max = 2000,
        },
        .is_log = false,
        .is_l3 = true,
    }, {
        .name = SAI_TRAP_GROUP_STP,
        .trap_ids = {
            SAI_HOSTIF_TRAP_ID_STP,
            -1
        },
        .priority = 5,
        .policer_config = {
            .rate_max = 1000,
            .burst_max = 1000,
        },
        .is_log = false,
        .is_l3 = false,
    },
};

static struct ovs_list sai_trap_group_list
    = OVS_LIST_INITIALIZER(&sai_trap_group_list);

static void
__traps_bind(const int *, const handle_t *, bool, bool);

/**
 * Returns host interface type string representation.
 *
 * @param[in] type - host interface type.
 *
 * @return pointer to type string representation
 */
const char *
ops_sai_host_intf_type_to_str(enum host_intf_type type)
{
    const char *str = NULL;
    switch(type) {
    case HOST_INTF_TYPE_L2_PORT_NETDEV:
        str = "L2 port netdev";
        break;
    case HOST_INTF_TYPE_L3_PORT_NETDEV:
        str = "L3 port netdev";
        break;
    case HOST_INTF_TYPE_L3_VLAN_NETDEV:
        str = "L3 VLAN netdev";
        break;
    default:
        str = "unknown";
        break;
    }

    return str;
}

/*
 * Initialize host interface.
 */
static void
__host_intf_init(void)
{
    VLOG_INFO("Initializing host interface");
}

/*
 * De-initialize host interface.
 */
static void
__host_intf_deinit(void)
{
    VLOG_INFO("De-initializing host interface");
}

/*
 * Creates Linux netdev for specified interface.
 *
 * @param[in] name   - netdev name.
 * @param[in] type   - netdev type.
 * @param[in] handle - if netdev is L2 specifies port label id else VLAN id.
 * @param[in] mac    - netdev mac address.
 *
 * @return 0 operation completed successfully
 * @return errno operation failed
 */
static int
__host_intf_netdev_create(const char *name,
                          enum host_intf_type type,
                          const handle_t *handle,
                          const struct eth_addr *mac)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
    return 0;
}

/*
 * Removes Linux netdev for specified interface.
 *
 * @param[in] name - netdev name
 *
 * @return 0 operation completed successfully
 * @return errno operation failed
 */
static int
__host_intf_netdev_remove(const char *name)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
    return 0;
}

/*
 * Registers traps for packets.
 */
static void
__host_intf_traps_register(void)
{
    int i = 0;
    sai_attribute_t attr[2] = { };
    sai_status_t status = SAI_STATUS_SUCCESS;
    struct ops_sai_trap_group_entry *group_entry = NULL;
    const struct ops_sai_api_class *sai_api = ops_sai_api_get_instance();

    VLOG_INFO("Registering traps");

    for (i = 0; i < ARRAY_SIZE(trap_group_config_table); ++i) {
        /* create entry */
        group_entry = xzalloc(sizeof *group_entry);

        /* create policer */
        status = ops_sai_policer_create(&group_entry->policer,
                                        &trap_group_config_table[i].policer_config)
                                        ? SAI_STATUS_FAILURE
                                        : SAI_STATUS_SUCCESS;
        SAI_ERROR_LOG_ABORT(status, "Failed to register traps");

        /* create group */
        attr[0].id = SAI_HOSTIF_TRAP_GROUP_ATTR_QUEUE;
        attr[0].value.u32 = trap_group_config_table[i].priority;
        attr[1].id = SAI_HOSTIF_TRAP_GROUP_ATTR_POLICER;
        attr[1].value.oid = (sai_object_id_t)group_entry->policer.data;

        status = sai_api->host_interface_api->create_hostif_trap_group(&group_entry->trap_group.data,
                                                                       ARRAY_SIZE(attr),
                                                                       attr);
        SAI_ERROR_LOG_ABORT(status, "Failed to create group %s",
                            trap_group_config_table[i].name);

        /* register traps */
        __traps_bind(trap_group_config_table[i].trap_ids,
                     &group_entry->trap_group,
                     trap_group_config_table[i].is_l3,
                     trap_group_config_table[i].is_log);

        strncpy(group_entry->name, trap_group_config_table[i].name,
                sizeof(group_entry->name));
        group_entry->name[strnlen(group_entry->name,
                                  SAI_TRAP_GROUP_MAX_NAME_LEN - 1)] = '\0';
        list_push_back(&sai_trap_group_list, &group_entry->list_node);
    }
}

/*
 * Unregisters traps for packets.
 */
static void
__hostint_traps_unregister(void)
{
    sai_status_t status = SAI_STATUS_SUCCESS;
    struct ops_sai_trap_group_entry *entry = NULL, *next_entry = NULL;
    const struct ops_sai_api_class *sai_api = ops_sai_api_get_instance();

    LIST_FOR_EACH_SAFE(entry, next_entry, list_node, &sai_trap_group_list) {
        list_remove(&entry->list_node);

        status = ops_sai_policer_remove(&entry->policer) ? SAI_STATUS_SUCCESS
                                                         : SAI_STATUS_FAILURE;
        SAI_ERROR_LOG_ABORT(status, "Failed to uninitialize traps %s",
                            entry->name);

        status = sai_api->host_interface_api->remove_hostif_trap_group(entry->trap_group.data);
        SAI_ERROR_LOG_ABORT(status, "Failed to remove trap group %s",
                            entry->name);

        free(entry);
    }
}

/*
 * Binds trap ids to trap groups.
 *
 * @param[in] trap_ids - list of trap ids, -1 terminated.
 * @param[in] group    - pointer to trap group handle.
 * @param[in] is_l3    - boolean indicating if trap channel is L3 netdev.
 * @param[in] is_log   - boolean indicating if packet should be forwarded.
 *
 * @return 0 operation completed successfully
 * @return errno operation failed
 */
static void
__traps_bind(const int *trap_ids, const handle_t *group, bool is_l3,
             bool is_log)
{
    int i = 0;
    sai_attribute_t attr = { };
    sai_status_t status = SAI_STATUS_SUCCESS;
    const struct ops_sai_api_class *sai_api = ops_sai_api_get_instance();

    NULL_PARAM_LOG_ABORT(group);
    NULL_PARAM_LOG_ABORT(trap_ids);

    for (i = 0; trap_ids[i] != -1; i++) {
        attr.id = SAI_HOSTIF_TRAP_ATTR_PACKET_ACTION;
        attr.value.u32 = is_log ? SAI_PACKET_ACTION_LOG
                                : SAI_PACKET_ACTION_TRAP;
        status = sai_api->host_interface_api->set_trap_attribute(trap_ids[i],
                                                                 &attr);
        SAI_ERROR_LOG_ABORT(status, "Failed to set trap packet action, id %d",
                            trap_ids[i]);

        attr.id = SAI_HOSTIF_TRAP_ATTR_TRAP_CHANNEL;
        attr.value.u32 = is_l3 ? SAI_HOSTIF_TRAP_CHANNEL_NETDEV
#ifdef MLNX_SAI
                               : SAI_HOSTIF_TRAP_CHANNEL_L2_NETDEV;
#else
                               : SAI_HOSTIF_TRAP_CHANNEL_NETDEV;
#endif
        status = sai_api->host_interface_api->set_trap_attribute(trap_ids[i],
                                                                 &attr);
        SAI_ERROR_LOG_ABORT(status, "Failed to set trap channel, id %d",
                            trap_ids[i]);

        attr.id = SAI_HOSTIF_TRAP_ATTR_TRAP_GROUP;
        attr.value.oid = (sai_object_id_t)group->data;
        status = sai_api->host_interface_api->set_trap_attribute(trap_ids[i],
                                                                 &attr);
        SAI_ERROR_LOG_ABORT(status, "Failed to bind trap to group, id %d",
                            trap_ids[i]);
    }
}

DEFINE_GENERIC_CLASS(struct host_intf_class, host_intf) = {
        .init = __host_intf_init,
        .create = __host_intf_netdev_create,
        .remove = __host_intf_netdev_remove,
        .traps_register = __host_intf_traps_register,
        .traps_unregister = __hostint_traps_unregister,
        .deinit = __host_intf_deinit
};

DEFINE_GENERIC_CLASS_GETTER(struct host_intf_class, host_intf);
