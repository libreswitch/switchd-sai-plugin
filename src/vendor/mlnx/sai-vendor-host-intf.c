/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#include <util.h>
#include <hmap.h>
#include <hash.h>
#include <list.h>
#include <inttypes.h>
#include <sai-log.h>
#include <sai-handle.h>
#include <sai-api-class.h>
#include <sai-host-intf.h>
#include <sai-port.h>

/* should be included last due to defines conflicts */
#include <sai-vendor-util.h>
#include <sx/sdk/sx_net_lib.h>

#define SAI_TRAP_GROUP_MAX_NAME_LEN 50
#define SAI_TRAP_ID_MAX_COUNT 10

#define MLNX_TRAP_GROUP_UNKNOWN_IP_DEST "mlnx_trap_group_unknown_ip_dest"

#define COMMAND_MAX_SIZE 512

VLOG_DEFINE_THIS_MODULE(mlnx_sai_host_intf);

struct hif_entry {
    struct hmap_node hmap_node;
    char name[IFNAMSIZ];
    enum host_intf_type type;
    handle_t handle;
    struct eth_addr mac;
};

/* rate_max and burst_max are in 10^3 units. */
static const struct ops_sai_trap_group_config mlnx_trap_group_config[] = { {
        .name = MLNX_TRAP_GROUP_UNKNOWN_IP_DEST,
        .trap_ids = {
            SX_TRAP_ID_L3_UC_IP_BASE + SX_TRAP_PRIORITY_BEST_EFFORT,
            SX_TRAP_ID_L3_UC_IP_BASE + SX_TRAP_PRIORITY_LOW,
            SX_TRAP_ID_L3_UC_IP_BASE + SX_TRAP_PRIORITY_MED,
            SX_TRAP_ID_L3_UC_IP_BASE + SX_TRAP_PRIORITY_HIGH,
            SX_TRAP_ID_HOST_MISS_IPV4,
#if SX_TRAP_ID_HOST_MISS_IPV4 != SX_TRAP_ID_HOST_MISS_IPV6
            SX_TRAP_ID_HOST_MISS_IPV6,
#endif
            -1
        },
        .priority = 2,
        .policer_config = {
            .rate_max = 3,
            .burst_max = 3,
        },
        .is_log = false,
        .is_l3 = true,
    },
};

static struct ovs_list mlnx_trap_group_list
    = OVS_LIST_INITIALIZER(&mlnx_trap_group_list);

static struct hmap all_host_intf = HMAP_INITIALIZER(&all_host_intf);

static void __mlnx_traps_bind(const struct ops_sai_trap_group_config *,
                              uint32_t);
static void __mlnx_traps_unbind(const struct ops_sai_trap_group_config *);

static int __mlnx_create_l2_port_netdev(const char *,
                                   const handle_t *,
                                   const struct eth_addr *);
static int __mlnx_remove_netdev(const char *);
static int __mlnx_create_l3_port_netdev(const char *,
                                        const handle_t *,
                                        const struct eth_addr *);
static int __mlnx_remove_l3_port_netdev(const char *);
static int __mlnx_create_l3_vlan_netdev(const char *,
                                        const handle_t *,
                                        const struct eth_addr *);

void __port_transaction_to_l2(uint32_t);
void __port_transaction_to_l3(uint32_t);

static struct hif_entry *__host_intf_entry_hmap_find(struct hmap *,
                                                     const char *);
static void __host_intf_entry_hmap_add(struct hmap *,
                                       const struct hif_entry *);
static void __host_intf_entry_hmap_del(struct hmap *,
                                       const char *);

/**
 * Initialize host interface.
 */
void
__host_intf_init()
{
    int err = 0;
    sx_status_t status = SX_STATUS_SUCCESS;

    ops_sai_host_intf_class_generic()->init();

    /* Move interface created during SDK initialization into namespace.
     * This is temporary fix. Behavior may be changed
     * after response from arch */
    err = system("ln -sf /proc/1/ns/net /var/run/netns/default");
    ERRNO_LOG_ABORT(err, "Failed to link default namespace");

    err = system("ip netns exec default "
                 "ip link set dev swid0_eth netns swns");
    ERRNO_LOG_ABORT(err, "Failed to move swid0_eth device to swns namespace");

    err = system("ip link set dev swid0_eth up");
    ERRNO_LOG_ABORT(err, "Failed to move swid0_eth device to swns namespace");

    err = ops_sai_port_transaction_register_callback(__port_transaction_to_l2,
                                                     OPS_SAI_PORT_TRANSACTION_TO_L2);
    ERRNO_LOG_ABORT(err, "Failed to register port transaction to L2 callback ");

    err = ops_sai_port_transaction_register_callback(__port_transaction_to_l3,
                                                     OPS_SAI_PORT_TRANSACTION_TO_L3);
    ERRNO_LOG_ABORT(err, "Failed to register port transaction to L2 callback ");

    status = sx_net_init(NULL, SX_VERBOSITY_LEVEL_INFO, true);
    SX_ERROR_LOG_ABORT(status, "Failed to initialize SX net lib (error: %s)",
                       SX_STATUS_MSG(status));
}

/**
 * Initialize host interface.
 */
void
__host_intf_deinit()
{
    VLOG_INFO("De-initializing host interface");

    ops_sai_host_intf_class_generic()->deinit();
}

/**
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
int
__host_intf_netdev_create(const char *name,
                                enum host_intf_type type,
                                const handle_t *handle,
                                const struct eth_addr *mac)
{
    int err = 0;
    struct hif_entry hif = { };

    switch (type) {
    case HOST_INTF_TYPE_L2_PORT_NETDEV:
        err = __mlnx_create_l2_port_netdev(name, handle, mac);
        ERRNO_EXIT(err);
        break;
    case HOST_INTF_TYPE_L3_PORT_NETDEV:
        err = __mlnx_create_l3_port_netdev(name, handle, mac);
        ERRNO_EXIT(err);
        break;
    case HOST_INTF_TYPE_L3_VLAN_NETDEV:
        err = __mlnx_create_l3_vlan_netdev(name, handle, mac);
        ERRNO_EXIT(err);
        break;
    default:
        ovs_assert(false);
        break;
    }

    strncpy(hif.name, name, sizeof(hif.name));
    hif.type = type;
    memcpy(&hif.handle, handle, sizeof(hif.handle));
    memcpy(&hif.mac, mac, sizeof(hif.mac));

    __host_intf_entry_hmap_add(&all_host_intf, &hif);

exit:
    return err;
}

/**
 * Removes Linux netdev for specified interface.
 *
 * @param[in] name - netdev name
 *
 * @return 0 operation completed successfully
 * @return errno operation failed
 */
int
__host_intf_netdev_remove(const char *name)
{
    int err = 0;
    struct hif_entry *hif = NULL;

    NULL_PARAM_LOG_ABORT(name);

    hif = __host_intf_entry_hmap_find(&all_host_intf, name);
    ovs_assert(hif);

    switch (hif->type) {
    case HOST_INTF_TYPE_L2_PORT_NETDEV:
    case HOST_INTF_TYPE_L3_VLAN_NETDEV:
        err = __mlnx_remove_netdev(name);
        ERRNO_EXIT(err);
        break;
    case HOST_INTF_TYPE_L3_PORT_NETDEV:
        err = __mlnx_remove_l3_port_netdev(name);
        ERRNO_EXIT(err);
        break;
    default:
        ovs_assert(false);
        break;
    }

    __host_intf_entry_hmap_del(&all_host_intf, name);

exit:
    return err;
}

/**
 * Registers traps for packets.
 */
void
__host_intf_traps_register(void)
{
    int i = 0;
    uint32_t group_id = 0;
    sx_status_t status = SX_STATUS_SUCCESS;
    struct ops_sai_trap_group_entry *group_entry = NULL;
    sx_policer_attributes_t sx_policer_attribs = {
        .ir_units = SX_POLICER_IR_UNITS_10_POWER_3_E,
        .is_host_ifc_policer = true,
        .meter_type = SX_POLICER_METER_PACKETS,
        .rate_type = SX_POLICER_RATE_TYPE_SINGLE_RATE_E,
        .yellow_action = SX_POLICER_ACTION_FORWARD_SET_YELLOW_COLOR,
        .red_action = SX_POLICER_ACTION_DISCARD,
    };
    sx_trap_group_attributes_t trap_group_attributes = {
        .truncate_mode = SX_TRUNCATE_MODE_DISABLE,
        .truncate_size = 0,
    };

    ops_sai_host_intf_class_generic()->traps_register();

    VLOG_INFO("Registering missing traps via SX SDK");

    /* Find group ID not in use. We cannot override groups used by SAI, so we
     * need to find those that are not in use. */
    for (group_id = 0; group_id < MAX_TRAP_GROUPS; group_id++) {
        if (!g_sai_db_ptr->trap_group_valid[group_id]) {
            break;
        }
    }

    for (i = 0; i < ARRAY_SIZE(mlnx_trap_group_config); ++i) {
        ovs_assert(group_id < MAX_TRAP_GROUPS);

        /* create entry */
        group_entry = xzalloc(sizeof(*group_entry));

        trap_group_attributes.prio = mlnx_trap_group_config[i].priority;
        status = sx_api_host_ifc_trap_group_set(gh_sdk, DEFAULT_ETH_SWID,
                                                group_id,
                                                &trap_group_attributes);
        SX_ERROR_LOG_ABORT(status, "Failed to set trap group %s",
                           mlnx_trap_group_config[i].name);

        /* Create policer. */
        sx_policer_attribs.cbs =
            mlnx_trap_group_config[i].policer_config.burst_max;
        sx_policer_attribs.cir =
            mlnx_trap_group_config[i].policer_config.rate_max;
        sx_policer_attribs.ebs =
            mlnx_trap_group_config[i].policer_config.burst_max;
        sx_policer_attribs.eir =
            mlnx_trap_group_config[i].policer_config.rate_max;

        status = sx_api_policer_set(gh_sdk, SX_ACCESS_CMD_CREATE,
                                    &sx_policer_attribs,
                                    &group_entry->policer.data);
        SX_ERROR_LOG_ABORT(status, "Failed to set policer for group %s",
                           mlnx_trap_group_config[i].name);

        /* Bind policer. */
        status = sx_api_host_ifc_policer_bind_set(gh_sdk, SX_ACCESS_CMD_BIND,
                                                  DEFAULT_ETH_SWID, group_id,
                                                  (sx_policer_id_t)group_entry->policer.data);
        SX_ERROR_LOG_ABORT(status, "Failed to bind policer to group %s",
                           mlnx_trap_group_config[i].name);

        /* Bind traps. */
        __mlnx_traps_bind(&(mlnx_trap_group_config[i]), group_id);

        strncpy(group_entry->name, mlnx_trap_group_config[i].name,
                sizeof(group_entry->name));
        list_push_back(&mlnx_trap_group_list, &group_entry->list_node);
        group_entry->trap_group.data = group_id;
        group_id++;
    }
}

/**
 * Unregisters traps for packets.
 */
void
__hostint_traps_unregister(void)
{
    int i = 0;
    sx_status_t status = SX_STATUS_SUCCESS;
    sx_policer_attributes_t sx_policer_attribs = { };
    struct ops_sai_trap_group_entry *entry = NULL, *next_entry = NULL;

    ops_sai_host_intf_class_generic()->traps_unregister();

    VLOG_INFO("Un-registering missing traps via SX SDK");

    /* Unbind traps. */
    for (i = 0; i < ARRAY_SIZE(mlnx_trap_group_config); ++i) {
        __mlnx_traps_unbind(&(mlnx_trap_group_config[i]));
    }

    /* Destroy policers. */
    LIST_FOR_EACH_SAFE(entry, next_entry, list_node, &mlnx_trap_group_list) {
        status = sx_api_host_ifc_policer_bind_set(gh_sdk, SX_ACCESS_CMD_UNBIND,
                                                  DEFAULT_ETH_SWID,
                                                  entry->trap_group.data,
                                                  (sx_policer_id_t)entry->policer.data);
        SX_ERROR_LOG_ABORT(status, "Failed to unbind policer from group %s"
                           "trap (error: %s)", entry->name,
                           SX_STATUS_MSG(status));

        status = sx_api_policer_set(gh_sdk, SX_ACCESS_CMD_DESTROY,
                                    &sx_policer_attribs,
                                    &entry->policer.data);
        SX_ERROR_LOG_ABORT(status, "Failed to destroy policer, group %s"
                           "trap (error: %s)", entry->name,
                           SX_STATUS_MSG(status));

        free(entry);
    }
}

/*
 * Binds traps to trap groups and corresponding channels.
 *
 * @param[in] config - pointer to entry in mlnx configuration table.
 * @param[in] group_id - sx_sdg group id for binding.
 */
static void
__mlnx_traps_bind(const struct ops_sai_trap_group_config *config,
                  uint32_t group_id)
{
    int i = 0;
    sx_user_channel_t user_channel = { };
    sx_status_t status = SX_STATUS_SUCCESS;

    NULL_PARAM_LOG_ABORT(config);

    for (i = 0; config->trap_ids[i] != -1; i++) {
        user_channel.type = config->is_l3 ? SX_USER_CHANNEL_TYPE_L3_NETDEV
                                          : SX_USER_CHANNEL_TYPE_L2_NETDEV;
        status = sx_api_host_ifc_trap_id_register_set(gh_sdk,
                                                      SX_ACCESS_CMD_REGISTER,
                                                      DEFAULT_ETH_SWID,
                                                      config->trap_ids[i],
                                                      &user_channel);
        SX_ERROR_LOG_ABORT(status, "Failed to set channel type for trap group"
                           "%s (trap: 0x%x, error: %s)", config->name,
                           config->trap_ids[i], SX_STATUS_MSG(status));

        status = sx_api_host_ifc_trap_id_set(gh_sdk, DEFAULT_ETH_SWID,
                                             config->trap_ids[i],
                                             group_id,
                                             config->is_log ?
                                             SX_TRAP_ACTION_MIRROR_2_CPU :
                                             SX_TRAP_ACTION_TRAP_2_CPU);
        SX_ERROR_LOG_ABORT(status, "Failed to set trap action for trap group"
                           "%s (trap: 0x%x, error: %s)", config->name,
                           config->trap_ids[i], SX_STATUS_MSG(status));
    }
}

/*
 * Unbinds traps from trap groups.
 *
 * @param[in] config - pointer to entry in mlnx configuration table.
 */
static void
__mlnx_traps_unbind(const struct ops_sai_trap_group_config *config)
{
    int i = 0;
    sx_user_channel_t user_channel = { };
    sx_status_t status = SX_STATUS_SUCCESS;

    NULL_PARAM_LOG_ABORT(config);

    for (i = 0; config->trap_ids[i] != -1; i++) {
        user_channel.type = config->is_l3 ? SX_USER_CHANNEL_TYPE_L3_NETDEV
                                          : SX_USER_CHANNEL_TYPE_L2_NETDEV;
        status = sx_api_host_ifc_trap_id_register_set(gh_sdk,
                                                      SX_ACCESS_CMD_DEREGISTER,
                                                      DEFAULT_ETH_SWID,
                                                      config->trap_ids[i],
                                                      &user_channel);
        SX_ERROR_LOG_ABORT(status,
                           "Failed to unset channel type for trap %d group %s "
                           "trap (error: %s)", config->trap_ids[i],
                           config->name, SX_STATUS_MSG(status));
    }
}

/*
 * Create L2 port netdev.
 *
 * @param[in] name    - netdev name.
 * @param[in] handle  - netdev handle (port lable id).
 * @param[in] addr    - netdev MAC address.
 *
 * @return 0 operation completed successfully
 * @return errno operation failed
 */
static int
__mlnx_create_l2_port_netdev(const char *name,
                             const handle_t *handle,
                             const struct eth_addr *addr)
{
    int err = 0;
    sai_status_t sai_status = SAI_STATUS_SUCCESS;
    sx_port_log_id_t port_id = 0;
    uint32_t obj_data = 0;
    char command[COMMAND_MAX_SIZE] = { };

    NULL_PARAM_LOG_ABORT(name);
    NULL_PARAM_LOG_ABORT(handle);
    NULL_PARAM_LOG_ABORT(addr);

    VLOG_INFO("Creating host interface (name: %s, type: L2 port, handle: %lu)",
              name,
              handle->data);

    sai_status = mlnx_object_to_type(ops_sai_api_port_map_get_oid(handle->data),
                                     SAI_OBJECT_TYPE_PORT,
                                     &obj_data,
                                     NULL);
    SAI_ERROR_LOG_ABORT(sai_status, "Failed to get port id (handle: %lu)",
                        handle->data);

    port_id = (sx_port_log_id_t) obj_data;

    snprintf(command, sizeof(command),
             "ip link add %s type sx_netdev swid %u port 0x%x",
             name, DEFAULT_ETH_SWID, port_id);

    VLOG_DBG("Executing command (command: %s)", command);
    err = system(command);
    ERRNO_LOG_EXIT(WEXITSTATUS(err), "Failed to execute command (command: %s)",
                   command);

    snprintf(command, sizeof(command),
            "ip link set dev %s address %02x:%02x:%02x:%02x:%02x:%02x",
             name, addr->ea[0], addr->ea[1],
             addr->ea[2], addr->ea[3],
             addr->ea[4], addr->ea[5]);


    VLOG_DBG("Executing command (command: %s)", command);
    err = system(command);
    ERRNO_LOG_EXIT(WEXITSTATUS(err), "Failed to execute command (command: %s)",
                   command);

exit:
    return err;
}

/*
 * Remove L2 port netdev.
 *
 * @param[in] name    - netdev name.
 *
 * @return 0 operation completed successfully
 * @return errno operation failed
 */
static int
__mlnx_remove_netdev(const char *name )
{
    int err = 0;
    char command[COMMAND_MAX_SIZE] = { };

    NULL_PARAM_LOG_ABORT(name);

    VLOG_INFO("Removing host interface (name: %s, type: L2 port)",
              name);

    snprintf(command, sizeof(command), "ip link del dev %s", name);

    VLOG_DBG("Executing command (command: %s)", command);
    /* Error code is returned in the least significant byte */
    err = 0xff & system(command);
    ERRNO_LOG_EXIT(err , "Failed to execute command (command: %s)",
                   command);

exit:
    return err;
}

/*
 * Create L3 port netdev.
 *
 * @param[in] name    - netdev name.
 * @param[in] handle  - netdev handle (port lable id).
 * @param[in] addr    - netdev MAC address.
 *
 * @return 0 operation completed successfully
 * @return errno operation failed
 */
static int
__mlnx_create_l3_port_netdev(const char *name,
                             const handle_t *handle,
                             const struct eth_addr *addr)
{
    int err = 0;
    sai_status_t sai_status = SAI_STATUS_SUCCESS;
    sx_port_log_id_t port_id = 0;
    uint32_t obj_data = 0;
    char command[COMMAND_MAX_SIZE] = { };
    sx_status_t status = SX_STATUS_SUCCESS;
    sx_net_interface_attributes_t intf_attrs = { };

    NULL_PARAM_LOG_ABORT(name);
    NULL_PARAM_LOG_ABORT(handle);
    NULL_PARAM_LOG_ABORT(addr);

    VLOG_INFO("Creating host interface (name: %s, type: L3 port, handle: %lu)",
              name,
              handle->data);

    sai_status = mlnx_object_to_type(ops_sai_api_port_map_get_oid(handle->data),
                                     SAI_OBJECT_TYPE_PORT,
                                     &obj_data,
                                     NULL);
    SAI_ERROR_LOG_ABORT(sai_status, "Failed to get port id (handle: %lu)",
                        handle->data);

    port_id = (sx_port_log_id_t) obj_data;

    intf_attrs.type = SX_L2_INTERFACE_TYPE_VPORT;
    strcpy(intf_attrs.name, name);

    intf_attrs.data.port.swid = DEFAULT_ETH_SWID;
    intf_attrs.data.port.port = port_id;

    status = sx_net_interface_set(SX_ACCESS_CMD_CREATE, handle->data, &intf_attrs);
    err = SX_ERROR_2_ERRNO(status);
    ERRNO_LOG_EXIT(status,
                   "Failed to create host interface (name: %s, handle: %lu)",
                   name, handle->data);

    snprintf(command, sizeof(command),
            "ip link set dev %s address %02x:%02x:%02x:%02x:%02x:%02x",
             name, addr->ea[0], addr->ea[1], addr->ea[2], addr->ea[3],
             addr->ea[4], addr->ea[5]);

    VLOG_DBG("Executing command (command: %s)", command);
    err = system(command);
    ERRNO_LOG_EXIT(WEXITSTATUS(err), "Failed to execute command (command: %s)",
                   command);

exit:
    return err;
}

/*
 * Remove L3 port netdev.
 *
 * @param[in] name    - netdev name.
 *
 * @return 0 operation completed successfully
 * @return errno operation failed
 */
static int
__mlnx_remove_l3_port_netdev(const char *name)
{
    struct hif_entry *hif = NULL;
    sx_status_t status = SX_STATUS_SUCCESS;

    NULL_PARAM_LOG_ABORT(name);

    hif = __host_intf_entry_hmap_find(&all_host_intf, name);
    if (!hif) {
        goto exit;
    }

    status = sx_net_interface_set(SX_ACCESS_CMD_DESTROY,
                                  hif->handle.data,
                                  NULL);
    SX_ERROR_LOG_EXIT(status,
                      "Failed to delete host interface "
                      "(name: %s, handle: %lu)",
                      name, hif->handle.data);

exit:
    return SX_ERROR_2_ERRNO(status);
}

/*
 * Create L3 VLAN netdev.
 *
 * @param[in] name    - netdev name.
 * @param[in] handle  - netdev handle (port lable id).
 * @param[in] addr    - netdev MAC address.
 *
 * @return 0 operation completed successfully
 * @return errno operation failed
 */
static int
__mlnx_create_l3_vlan_netdev(const char *name,
                             const handle_t *handle,
                             const struct eth_addr *addr)
{
    int err = 0;
    char command[COMMAND_MAX_SIZE];

    NULL_PARAM_LOG_ABORT(name);
    NULL_PARAM_LOG_ABORT(handle);
    NULL_PARAM_LOG_ABORT(addr);

    VLOG_INFO("Creating host interface (name: %s, type: L3 VLAN, handle: %lu)",
              name,
              handle->data);

    snprintf(command, sizeof(command),
            "ip link add link swid%u_eth name %s type vlan id %lu",
             DEFAULT_ETH_SWID, name, handle->data);

    VLOG_DBG("Executing command (command: %s)", command);
    err = system(command);
    ERRNO_LOG_EXIT(WEXITSTATUS(err), "Failed to execute command (command: %s)",
                   command);

exit:
    return err;
}

/*
 * Port transaction to L2 callback.
 *
 * @param[in] hw_id    - port lable id.
 */
void __port_transaction_to_l2(uint32_t hw_id)
{
    int err = 0;
    struct hif_entry *hif = NULL;
    bool found = false;
    HMAP_FOR_EACH(hif, hmap_node, &all_host_intf) {
        if ((hif->type == HOST_INTF_TYPE_L2_PORT_NETDEV ||
                hif->type == HOST_INTF_TYPE_L3_PORT_NETDEV)
                && hif->handle.data == hw_id) {
            found = true;
            break;
        }
    }

    /* Host interface with hw id should always exists */
    ovs_assert(found);

    /* Host interface is already L2 */
    if (hif->type == HOST_INTF_TYPE_L2_PORT_NETDEV) {
        goto exit;
    }

    err = __mlnx_remove_l3_port_netdev(hif->name);
    ERRNO_EXIT(err);

    err = __mlnx_create_l2_port_netdev(hif->name, &hif->handle, &hif->mac);
    ERRNO_EXIT(err);

    /* Update host interface type after transaction */
    hif->type = HOST_INTF_TYPE_L2_PORT_NETDEV;

exit:
    return;
}

/*
 * Port transaction to L3 callback.
 *
 * @param[in] hw_id    - port lable id.
 */
void __port_transaction_to_l3(uint32_t hw_id)
{
    int err = 0;;
    struct hif_entry *hif = NULL;
    bool found = false;
    HMAP_FOR_EACH(hif, hmap_node, &all_host_intf) {
        if ((hif->type == HOST_INTF_TYPE_L2_PORT_NETDEV ||
                hif->type == HOST_INTF_TYPE_L3_PORT_NETDEV)
                && hif->handle.data == hw_id) {
            found = true;
            break;
        }
    }

    /* Host interface with hw id should always exists */
    ovs_assert(found);

    /* Host interface is already L3 */
    if (hif->type == HOST_INTF_TYPE_L3_PORT_NETDEV) {
        goto exit;
    }

    err = __mlnx_remove_netdev(hif->name);
    ERRNO_EXIT(err);

    err = __mlnx_create_l3_port_netdev(hif->name, &hif->handle, &hif->mac);
    ERRNO_EXIT(err);

    /* Update host interface type after transaction */
    hif->type = HOST_INTF_TYPE_L3_PORT_NETDEV;

exit:
    return;
}

/*
 * Find host interface entry in hash map.
 *
 * @param[in] hif_hmap      - Hash map.
 * @param[in] hif_name      - Host interface name used as map key.
 *
 * @return pointer to host interface entry if entry found.
 * @return NULL if entry not found.
 */
static struct hif_entry*
__host_intf_entry_hmap_find(struct hmap *hif_hmap, const char *hif_name)
{
    struct hif_entry* hif_entry = NULL;

    NULL_PARAM_LOG_ABORT(hif_hmap);
    NULL_PARAM_LOG_ABORT(hif_name);

    HMAP_FOR_EACH_WITH_HASH(hif_entry, hmap_node,
                            hash_string(hif_name, 0), hif_hmap) {
        if (strcmp(hif_entry->name, hif_name) == 0) {
            return hif_entry;
        }
    }

    return NULL;
}

/*
 * Add host interface entry to hash map.
 *
 * @param[in] hif_hmap        - Hash map.
 * @param[in] hif_entry       - Host interface entry.
 */
static void
__host_intf_entry_hmap_add(struct hmap *hif_hmap,
                           const struct hif_entry* hif_entry)
{
    struct hif_entry *hif_entry_int = NULL;

    NULL_PARAM_LOG_ABORT(hif_hmap);
    NULL_PARAM_LOG_ABORT(hif_entry);

    ovs_assert(!__host_intf_entry_hmap_find(hif_hmap, hif_entry->name));

    hif_entry_int = xzalloc(sizeof(*hif_entry_int));
    memcpy(hif_entry_int, hif_entry, sizeof(*hif_entry_int));

    hmap_insert(hif_hmap, &hif_entry_int->hmap_node,
                hash_string(hif_entry->name, 0));
}

/*
 * Delete host interface entry from hash map.
 *
 * @param[in] hif_hmap        - Hash map.
 * @param[in] name            - Host interface name used as map key.
 */
static void
__host_intf_entry_hmap_del(struct hmap *hif_hmap, const char *name)
{
    struct hif_entry* hif_entry = __host_intf_entry_hmap_find(hif_hmap,
                                                              name);
    if (hif_entry) {
        hmap_remove(hif_hmap, &hif_entry->hmap_node);
        free(hif_entry);
    }
}

DEFINE_VENDOR_CLASS(struct host_intf_class, host_intf) = {
        .init = __host_intf_init,
        .create = __host_intf_netdev_create,
        .remove = __host_intf_netdev_remove,
        .traps_register = __host_intf_traps_register,
        .traps_unregister = __hostint_traps_unregister,
        .deinit = __host_intf_deinit
};

DEFINE_VENDOR_CLASS_GETTER(struct host_intf_class, host_intf);
