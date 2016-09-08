/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#include <config.h>
#include <errno.h>
#include <linux/ethtool.h>
#include <netinet/ether.h>

#include <vswitch-idl.h>
#include <netdev-provider.h>
#include <openflow/openflow.h>
#include <openswitch-idl.h>
#include <openswitch-dflt.h>
#include <sai-api-class.h>
#include <sai-log.h>
#include <sai-common.h>
#include <sai-port.h>
#include <sai-host-intf.h>
#include <sai-router-intf.h>
#include <sai-ofproto-provider.h>

VLOG_DEFINE_THIS_MODULE(netdev_sai);

/* Protects 'sai_list'. */
static struct ovs_mutex sai_netdev_list_mutex = OVS_MUTEX_INITIALIZER;
static struct ovs_list sai_netdev_list OVS_GUARDED_BY(sai_netdev_list_mutex)
    = OVS_LIST_INITIALIZER(&sai_netdev_list);

struct netdev_sai {
    struct netdev up;
    struct ovs_list list_node OVS_GUARDED_BY(sai_netdev_list_mutex);
    struct ovs_mutex mutex OVS_ACQ_AFTER(sai_netdev_list_mutex);
    uint32_t hw_id;
    bool is_initialized;
    long long int carrier_resets;
    struct ops_sai_port_config default_config;
    struct ops_sai_port_config config;
    struct eth_addr mac_addr;
    bool netdev_internal_admin_state;
    const handle_t *rif_handle;
    struct {
        bool is_splitable;
        bool is_child;
        bool is_hw_lane_active;
        char *parent_name;
    } split_info;

};

static inline bool __is_sai_class(const struct netdev_class *);
static inline struct netdev_sai *__netdev_sai_cast(const struct netdev *);
static struct netdev *__alloc(void);
static int __construct(struct netdev *);
static void __destruct(struct netdev *);
static void __dealloc(struct netdev *);
static int __set_hw_intf_info(struct netdev *, const struct smap *);
static int __set_hw_intf_info_internal(struct netdev *netdev_,
                                                const struct smap *args);
static bool __args_autoneg_get(const struct smap *, bool);
static bool __args_duplex_get(const struct smap *, bool);
static int __args_pause_get(const struct smap *, bool, bool);
static int __set_hw_intf_config(struct netdev *, const struct smap *);
static int __set_hw_intf_config_internal(struct netdev *,
                                                  const struct smap *);
static int __set_etheraddr_full(struct netdev *, const struct eth_addr mac);
static int __set_etheraddr(struct netdev *, const struct eth_addr mac);
static int __get_etheraddr(const struct netdev *, struct eth_addr *mac);
static int __get_mtu(const struct netdev *, int *);
static int __set_mtu(const struct netdev *, int);
static int __get_carrier(const struct netdev *, bool *);
static long long int __get_carrier_resets(const struct netdev *);
static int __get_stats(const struct netdev *, struct netdev_stats *);
static int __get_features(const struct netdev *, enum netdev_features *,
                          enum netdev_features *, enum netdev_features *,
                          enum netdev_features *);
static int __update_flags(struct netdev *, enum netdev_flags,
                          enum netdev_flags, enum netdev_flags *);
static int __update_flags_internal(struct netdev *,
                                   enum netdev_flags,
                                   enum netdev_flags,
                                   enum netdev_flags *);
static int __update_flags_loopback(struct netdev *,
                                   enum netdev_flags,
                                   enum netdev_flags,
                                   enum netdev_flags *);
static struct netdev_sai *__netdev_sai_from_name(const char *);
static int __update_split_config(struct netdev_sai *);
static int __split(struct netdev_sai *, uint32_t);
static int __unsplit(struct netdev_sai *, uint32_t);
static int __enable_neighbor_netdev_config(struct netdev_sai *,
                                           enum ops_sai_port_split);
static int __disable_neighbor_netdev_config(struct netdev_sai *,
                                            enum ops_sai_port_split);

#define NETDEV_SAI_CLASS(TYPE, CONSTRUCT, DESCRUCT, INTF_INFO, INTF_CONFIG, \
                         UPDATE_FLAGS, GET_MTU, SET_MTU) \
{ \
    PROVIDER_INIT_GENERIC(type,                 TYPE) \
    PROVIDER_INIT_GENERIC(init,                 NULL) \
    PROVIDER_INIT_GENERIC(run,                  NULL) \
    PROVIDER_INIT_GENERIC(wait,                 NULL) \
    PROVIDER_INIT_GENERIC(alloc,                __alloc) \
    PROVIDER_INIT_GENERIC(construct,            CONSTRUCT) \
    PROVIDER_INIT_GENERIC(destruct,             DESCRUCT) \
    PROVIDER_INIT_GENERIC(dealloc,              __dealloc) \
    PROVIDER_INIT_GENERIC(get_config,           NULL) \
    PROVIDER_INIT_GENERIC(set_config,           NULL) \
    PROVIDER_INIT_OPS_SPECIFIC(set_hw_intf_info, INTF_INFO) \
    PROVIDER_INIT_OPS_SPECIFIC(set_hw_intf_config, INTF_CONFIG) \
    PROVIDER_INIT_GENERIC(get_tunnel_config,    NULL) \
    PROVIDER_INIT_GENERIC(build_header,         NULL) \
    PROVIDER_INIT_GENERIC(push_header,          NULL) \
    PROVIDER_INIT_GENERIC(pop_header,           NULL) \
    PROVIDER_INIT_GENERIC(get_numa_id,          NULL) \
    PROVIDER_INIT_GENERIC(set_multiq,           NULL) \
    PROVIDER_INIT_GENERIC(send,                 NULL) \
    PROVIDER_INIT_GENERIC(send_wait,            NULL) \
    PROVIDER_INIT_GENERIC(set_etheraddr,        __set_etheraddr) \
    PROVIDER_INIT_GENERIC(get_etheraddr,        __get_etheraddr) \
    PROVIDER_INIT_GENERIC(get_mtu,              GET_MTU) \
    PROVIDER_INIT_GENERIC(set_mtu,              SET_MTU) \
    PROVIDER_INIT_GENERIC(get_ifindex,          NULL) \
    PROVIDER_INIT_GENERIC(get_carrier,          __get_carrier) \
    PROVIDER_INIT_GENERIC(get_carrier_resets,   __get_carrier_resets) \
    PROVIDER_INIT_GENERIC(set_miimon_interval,  NULL) \
    PROVIDER_INIT_GENERIC(get_stats,            __get_stats) \
    PROVIDER_INIT_GENERIC(get_features,         __get_features) \
    PROVIDER_INIT_GENERIC(set_advertisements,   NULL) \
    PROVIDER_INIT_GENERIC(set_policing,         NULL) \
    PROVIDER_INIT_GENERIC(get_qos_types,        NULL) \
    PROVIDER_INIT_GENERIC(get_qos_capabilities, NULL) \
    PROVIDER_INIT_GENERIC(get_qos,              NULL) \
    PROVIDER_INIT_GENERIC(set_qos,              NULL) \
    PROVIDER_INIT_GENERIC(get_queue,            NULL) \
    PROVIDER_INIT_GENERIC(set_queue,            NULL) \
    PROVIDER_INIT_GENERIC(delete_queue,         NULL) \
    PROVIDER_INIT_GENERIC(get_queue_stats,      NULL) \
    PROVIDER_INIT_GENERIC(queue_dump_start,     NULL) \
    PROVIDER_INIT_GENERIC(queue_dump_next,      NULL) \
    PROVIDER_INIT_GENERIC(queue_dump_done,      NULL) \
    PROVIDER_INIT_GENERIC(dump_queue_stats,     NULL) \
    PROVIDER_INIT_GENERIC(get_in4,              NULL) \
    PROVIDER_INIT_GENERIC(set_in4,              NULL) \
    PROVIDER_INIT_GENERIC(get_in6,              NULL) \
    PROVIDER_INIT_GENERIC(add_router,           NULL) \
    PROVIDER_INIT_GENERIC(get_next_hop,         NULL) \
    PROVIDER_INIT_GENERIC(get_status,           NULL) \
    PROVIDER_INIT_GENERIC(arp_lookup,           NULL) \
    PROVIDER_INIT_GENERIC(update_flags,         UPDATE_FLAGS) \
    PROVIDER_INIT_GENERIC(rxq_alloc,            NULL) \
    PROVIDER_INIT_GENERIC(rxq_construct,        NULL) \
    PROVIDER_INIT_GENERIC(rxq_destruct,         NULL) \
    PROVIDER_INIT_GENERIC(rxq_dealloc,          NULL) \
    PROVIDER_INIT_GENERIC(rxq_recv,             NULL) \
    PROVIDER_INIT_GENERIC(rxq_wait,             NULL) \
    PROVIDER_INIT_GENERIC(rxq_drain,            NULL) \
} \

static const struct netdev_class netdev_sai_class = NETDEV_SAI_CLASS(
        "system",
        __construct,
        __destruct,
        __set_hw_intf_info,
        __set_hw_intf_config,
        __update_flags,
        __get_mtu,
        __set_mtu);

static const struct netdev_class netdev_sai_internal_class = NETDEV_SAI_CLASS(
        "internal",
        __construct,
        __destruct,
        __set_hw_intf_info_internal,
        __set_hw_intf_config_internal,
        __update_flags_internal,
        NULL,
        NULL);

static const struct netdev_class netdev_sai_vlansubint_class = NETDEV_SAI_CLASS(
        "vlansubint",
        __construct,
        __destruct,
        NULL,
        NULL,
        __update_flags_internal,
        NULL,
        NULL);

static const struct netdev_class netdev_sai_loopback_class = NETDEV_SAI_CLASS(
        "loopback",
        __construct,
        __destruct,
        NULL,
        NULL,
        __update_flags_loopback,
        NULL,
        NULL);

/**
 * Register netdev classes - system and internal.
 */
void
netdev_sai_register(void)
{
    netdev_register_provider(&netdev_sai_class);
    netdev_register_provider(&netdev_sai_internal_class);
    netdev_register_provider(&netdev_sai_vlansubint_class);
    netdev_register_provider(&netdev_sai_loopback_class);
}

/**
 * Get port label ID from netdev.
 * @param[in] netdev_ - pointer to netdev.
 * @return uint32_t value of port label ID.
 */
uint32_t
netdev_sai_hw_id_get(struct netdev *netdev_)
{
    struct netdev_sai *netdev = __netdev_sai_cast(netdev_);

    return netdev->hw_id;
}

/**
 * Notifies openswitch when port state changes.
 * @param[in] oid - port object id.
 * @param[in] link_status - port operational state.
 */
void
netdev_sai_port_oper_state_changed(sai_object_id_t oid, int link_status)
{
    struct netdev_sai *dev = NULL, *next_dev = NULL;

    LIST_FOR_EACH_SAFE(dev, next_dev, list_node, &sai_netdev_list) {
        if (dev->is_initialized &&
                dev->split_info.is_hw_lane_active &&
                ops_sai_api_port_map_get_oid(dev->hw_id) == oid) {
            break;
        }
    }

    if (NULL == dev) {
        return;
    }

    if (link_status) {
        dev->carrier_resets++;
    }

    netdev_change_seq_changed(&(dev->up));
    seq_change(connectivity_seq_get());
}

/**
 * Notifies openswitch when port lane state chenges.
 * @param[in] oid - port object id.
 * @param[in] lane_status - port lane state.
 */
void
netdev_sai_port_lane_state_changed(sai_object_id_t oid, int lane_status)
{
    struct netdev_sai *dev = NULL, *next_dev = NULL;

    LIST_FOR_EACH_SAFE(dev, next_dev, list_node, &sai_netdev_list) {
        if (dev->is_initialized
            && ops_sai_api_port_map_get_oid(dev->hw_id) == oid) {
            break;
        }
    }

    if (NULL == dev || !dev->is_initialized) {
        return;
    }

    dev->split_info.is_hw_lane_active = !!lane_status;

    netdev_change_seq_changed(&(dev->up));
    seq_change(connectivity_seq_get());
}

/**
 * Attach/detach router interface handle to netdev.
 *
 * @param[in] netdev_     - Pointer to netdev object.
 * @param[in] rif_handle  - Router interface handle.
 *
 * @note If NULL value is specified for rif_handle this means that
 *       router interface handle should be detached from netdev
 *
 * @return 0 operation completed successfully
 * @return errno operation failed
 */
int
netdev_sai_set_router_intf_handle(struct netdev *netdev_,
                                  const handle_t *rif_handle)
{
    struct netdev_sai *netdev = __netdev_sai_cast(netdev_);

    VLOG_INFO("Set rif handle for netdev (netdev: %s, rif_handle: %p)",
              netdev_get_name(netdev_), rif_handle);

    ovs_mutex_lock(&netdev->mutex);

    ovs_assert(netdev->is_initialized);

    netdev->rif_handle = rif_handle;

    ovs_mutex_unlock(&netdev->mutex);

    return 0;
}

/**
 * Get netdev HW lane state.
 *
 * @param[in] netdev_     - Pointer to netdev object.
 * @param[out] state      - Netdev HW lane state.
 *
 * @return 0 operation completed successfully
 * @return errno operation failed
 */
int
netdev_sai_get_lane_state(struct netdev *netdev_, bool *state)
{
    struct netdev_sai *netdev = __netdev_sai_cast(netdev_);

    ovs_mutex_lock(&netdev->mutex);

    ovs_assert(netdev->is_initialized);

    *state = netdev->split_info.is_hw_lane_active;

    ovs_mutex_unlock(&netdev->mutex);

    return 0;
}

/*
 * Check if netdev is of type sai_netdev.
 */
static inline bool
__is_sai_class(const struct netdev_class *class)
{
    return class->construct == __construct;
}

/*
 * Cast openswitch netdev to sai_netdev.
 */
static inline struct netdev_sai *
__netdev_sai_cast(const struct netdev *netdev)
{
    NULL_PARAM_LOG_ABORT(netdev);

    ovs_assert(__is_sai_class(netdev_get_class(netdev)));
    return CONTAINER_OF(netdev, struct netdev_sai, up);
}

static struct netdev *
__alloc(void)
{
    struct netdev_sai *netdev = xzalloc(sizeof *netdev);

    SAI_API_TRACE_FN();

    return &(netdev->up);
}

static int
__construct(struct netdev *netdev_)
{
    struct netdev_sai *netdev = __netdev_sai_cast(netdev_);

    SAI_API_TRACE_FN();

    ovs_mutex_init(&netdev->mutex);
    ovs_mutex_lock(&sai_netdev_list_mutex);
    list_push_back(&sai_netdev_list, &netdev->list_node);
    ovs_mutex_unlock(&sai_netdev_list_mutex);

    return 0;
}

static void
__destruct(struct netdev *netdev_)
{
    struct netdev_sai *netdev = __netdev_sai_cast(netdev_);

    SAI_API_TRACE_FN();

    ovs_mutex_lock(&sai_netdev_list_mutex);

    if (netdev->is_initialized) {
        ops_sai_host_intf_netdev_remove(netdev_get_name(netdev_));
    }

    if (netdev->split_info.is_child) {
        free(netdev->split_info.parent_name);
    }

    list_remove(&netdev->list_node);
    ovs_mutex_unlock(&sai_netdev_list_mutex);
    ovs_mutex_destroy(&netdev->mutex);
}

static void
__dealloc(struct netdev *netdev_)
{
    struct netdev_sai *netdev = __netdev_sai_cast(netdev_);

    SAI_API_TRACE_FN();

    free(netdev);
}

static int
__set_hw_intf_info(struct netdev *netdev_, const struct smap *args)
{
    int status = 0;
    handle_t hw_id_handle;
    struct netdev_sai *netdev = __netdev_sai_cast(netdev_);
    const int hw_id = smap_get_int(args,
                                   INTERFACE_HW_INTF_INFO_MAP_SWITCH_INTF_ID,
                                   -1);
    const char *split_parent = smap_get(args,
                                       INTERFACE_HW_INTF_INFO_SPLIT_PARENT);
    bool is_splitable = smap_get_bool(args,
                                      INTERFACE_HW_INTF_INFO_MAP_SPLIT_4,
                                      false);
    int max_speed = smap_get_int(args,
                                 INTERFACE_HW_INTF_INFO_MAP_MAX_SPEED, -1);

    SAI_API_TRACE_FN();

    NULL_PARAM_LOG_ABORT(args);
    ovs_assert(max_speed != -1);

    ovs_mutex_lock(&netdev->mutex);

    if (STR_EQ(netdev_->name, DEFAULT_BRIDGE_NAME)) {
        goto exit;
    }

    if (netdev->is_initialized) {
        status = __update_split_config(netdev);
        goto exit;
    }

    netdev->hw_id = hw_id;

    status = ops_sai_api_base_mac_get(&netdev->mac_addr);
    ERRNO_EXIT(status);

    if (split_parent) {
        netdev->split_info.is_child = true;
        netdev->split_info.is_hw_lane_active = false;
        netdev->split_info.parent_name = xstrdup(split_parent);
    } else {
        netdev->split_info.is_splitable = is_splitable;
        netdev->split_info.is_hw_lane_active = true;

        status = ops_sai_port_config_get(hw_id, &netdev->default_config);
        ERRNO_LOG_EXIT(status, "Failed to read default config on port: %d",
                       hw_id);

        hw_id_handle.data = hw_id;
        status = ops_sai_host_intf_netdev_create(netdev_get_name(netdev_),
                                                 HOST_INTF_TYPE_L2_PORT_NETDEV,
                                                 &hw_id_handle,
                                                 &netdev->mac_addr);
        ERRNO_LOG_EXIT(status,
                       "Failed to create port interface (name: %s)",
                       netdev_get_name(netdev_));
    }

    netdev->default_config.max_speed = max_speed;
    netdev->is_initialized = true;

exit:
    ovs_mutex_unlock(&netdev->mutex);
    return status;
}

static int
__set_hw_intf_info_internal(struct netdev *netdev_,
                                     const struct smap *args)
{
    int status = 0;
    int vlanid = 0;
    handle_t handle;
    struct eth_addr mac = { };
    struct netdev_sai *netdev = __netdev_sai_cast(netdev_);
    bool is_bridge_intf = smap_get_bool(args,
                                        INTERFACE_HW_INTF_INFO_MAP_BRIDGE,
                                        DFLT_INTERFACE_HW_INTF_INFO_MAP_BRIDGE);

    SAI_API_TRACE_FN();

    ovs_mutex_lock(&netdev->mutex);

    if (netdev->is_initialized) {
        goto exit;
    }

    if (!is_bridge_intf) {
        vlanid = strtol(netdev_get_name(netdev_) + strlen(VLAN_INTF_PREFIX),
                        NULL, 0);
        ovs_assert(vlanid >= VLAN_ID_MIN && vlanid <= VLAN_ID_MAX);

        handle.data = vlanid;

        status = ops_sai_api_base_mac_get(&mac);
        ERRNO_EXIT(status);

        memcpy(&netdev->mac_addr, &mac, sizeof(netdev->mac_addr));

        status = ops_sai_host_intf_netdev_create(netdev_get_name(netdev_),
                                                 HOST_INTF_TYPE_L3_VLAN_NETDEV,
                                                 &handle, &mac);
        ERRNO_LOG_EXIT(status,
                       "Failed to create port interface (name: %s)",
                       netdev_get_name(netdev_));
    }

    netdev->is_initialized = true;
    /* HW lane of L3 netdevs is always active */
    netdev->split_info.is_hw_lane_active = true;

exit:
    ovs_mutex_unlock(&netdev->mutex);
    return status;
}


/*
 * Read autoneg value from smap arguments.
 */
static bool
__args_autoneg_get(const struct smap *args, bool def)
{
    const char *autoneg = smap_get(args, INTERFACE_HW_INTF_CONFIG_MAP_AUTONEG);

    if (autoneg == NULL) {
        return def;
    }

    return !strcmp(autoneg, INTERFACE_USER_CONFIG_MAP_AUTONEG_ON);
}

/*
 * Read duplex value from smap arguments.
 */
static bool
__args_duplex_get(const struct smap *args, bool def)
{
    const char *duplex = smap_get(args, INTERFACE_HW_INTF_CONFIG_MAP_DUPLEX);

    if (duplex == NULL) {
        return def;
    }

    return !strcmp(duplex, INTERFACE_HW_INTF_CONFIG_MAP_DUPLEX_FULL);
}

/*
 * Read pause value from smap arguments.
 */
static int
__args_pause_get(const struct smap *args, bool is_tx, bool def)
{
    const char *pause = smap_get(args, INTERFACE_HW_INTF_CONFIG_MAP_PAUSE);
    const char *requested_pause = is_tx ? INTERFACE_HW_INTF_CONFIG_MAP_PAUSE_TX
        : INTERFACE_HW_INTF_CONFIG_MAP_PAUSE_RX;

    if (pause == NULL) {
        return def;
    }

    return !strcmp(pause, requested_pause) ||
           !strcmp(pause, INTERFACE_HW_INTF_CONFIG_MAP_PAUSE_RXTX);
}

static int
__set_hw_intf_config(struct netdev *netdev_, const struct smap *args)
{
    int status = 0;
    struct ops_sai_port_config config = { };
    struct netdev_sai *netdev = __netdev_sai_cast(netdev_);
    struct netdev_sai *parent_netdev = NULL;
    const struct ops_sai_port_config *def = &netdev->default_config;

    if (!netdev->is_initialized) {
        status = 0;
        goto exit;
    }

    if (netdev->split_info.is_child) {
        /* Get default config from parent */
        parent_netdev = __netdev_sai_from_name(netdev->split_info.parent_name);
        ovs_assert(parent_netdev);

        def = &parent_netdev->default_config;
    }

    /* Max speed must be always present (in yaml config file). */
    config.hw_enable = smap_get_bool(args, INTERFACE_HW_INTF_CONFIG_MAP_ENABLE,
                                     def->hw_enable);
    config.autoneg = __args_autoneg_get(args, def->autoneg);
    config.mtu = smap_get_int(args, INTERFACE_HW_INTF_CONFIG_MAP_MTU,
                              def->mtu);
    config.speed = smap_get_int(args, INTERFACE_HW_INTF_CONFIG_MAP_SPEEDS,
                                def->speed);
    config.full_duplex = __args_duplex_get(args, def->full_duplex);
    config.pause_tx = __args_pause_get(args, true, def->pause_tx);
    config.pause_rx = __args_pause_get(args, false, def->pause_rx);

    ovs_mutex_lock(&netdev->mutex);

    if (config.hw_enable) {
        __update_split_config(netdev);
    }

    if (netdev->split_info.is_hw_lane_active) {
        status = ops_sai_port_config_set(netdev->hw_id, &config, &netdev->config);
        ERRNO_LOG_EXIT(status, "Failed to set hw interface config");
    }

    netdev_change_seq_changed(netdev_);
    seq_change(connectivity_seq_get());

exit:
    ovs_mutex_unlock(&netdev->mutex);
    return status;
}

static int
__set_hw_intf_config_internal(struct netdev *netdev_, const struct smap *args)
{
    struct netdev_sai *netdev = __netdev_sai_cast(netdev_);
    const char *enable = smap_get(args, INTERFACE_HW_INTF_CONFIG_MAP_ENABLE);

    SAI_API_TRACE_FN();

    if (enable) {
        netdev->netdev_internal_admin_state =
                STR_EQ(enable, INTERFACE_HW_INTF_CONFIG_MAP_ENABLE_TRUE);
    }

    return 0;
}


/*
 * Cache MAC address.
 */
static int
__set_etheraddr_full(struct netdev *netdev,
                           const struct eth_addr mac)
{
    struct netdev_sai *dev = __netdev_sai_cast(netdev);

    if (!dev->is_initialized || !dev->split_info.is_hw_lane_active) {
        goto exit;
    }

    /* Not supported by SAI. */

    memcpy(&dev->mac_addr, &mac, sizeof (dev->mac_addr));

exit:
    return 0;
}

static int
__set_etheraddr(struct netdev *netdev,
                         const struct eth_addr mac)
{
    int status = 0;
    struct netdev_sai *dev = __netdev_sai_cast(netdev);

    SAI_API_TRACE_FN();

    ovs_mutex_lock(&dev->mutex);
    status = __set_etheraddr_full(netdev, mac);
    ovs_mutex_unlock(&dev->mutex);

    return status;
}

static int
__get_etheraddr(const struct netdev *netdev,
                         struct eth_addr *mac)
{
    struct netdev_sai *dev = __netdev_sai_cast(netdev);

    SAI_API_TRACE_FN();

    ovs_mutex_lock(&dev->mutex);
    if (!dev->is_initialized || !dev->split_info.is_hw_lane_active) {
        goto exit;
    }

    memcpy(mac, &dev->mac_addr, sizeof (*mac));

exit:
    ovs_mutex_unlock(&dev->mutex);
    return 0;
}

static int
__get_mtu(const struct netdev *netdev_, int *mtup)
{
    int status = 0;
    struct netdev_sai *netdev = __netdev_sai_cast(netdev_);

    SAI_API_TRACE_FN();

    ovs_mutex_lock(&netdev->mutex);
    if (netdev->is_initialized && netdev->split_info.is_hw_lane_active) {
        status = ops_sai_port_mtu_get(netdev->hw_id, mtup);
    }
    ovs_mutex_unlock(&netdev->mutex);

    return status;
}

static int
__set_mtu(const struct netdev *netdev_, int mtu)
{
    int status = 0;
    struct netdev_sai *netdev = __netdev_sai_cast(netdev_);

    SAI_API_TRACE_FN();

    ovs_mutex_lock(&netdev->mutex);
    if (netdev->is_initialized && netdev->split_info.is_hw_lane_active) {
        status = ops_sai_port_mtu_set(netdev->hw_id, mtu);
    }
    ovs_mutex_unlock(&netdev->mutex);

    return status;
}

static int
__get_carrier(const struct netdev *netdev_, bool * carrier)
{
    int status = 0;
    struct netdev_sai *netdev = __netdev_sai_cast(netdev_);

    SAI_API_TRACE_FN();

    ovs_mutex_lock(&netdev->mutex);
    if (netdev->is_initialized) {
        if (STR_EQ(netdev_get_type(netdev_), OVSREC_INTERFACE_TYPE_SYSTEM)) {
            if (netdev->split_info.is_hw_lane_active) {
                status = ops_sai_port_carrier_get(netdev->hw_id, carrier);
            } else {
                status = 0;
                *carrier = false;
            }
        } else {
            /* TODO: Waiting for implementation via netlink */
            *carrier = true;
        }
    }
    ovs_mutex_unlock(&netdev->mutex);

    return status;
}

static long long int
__get_carrier_resets(const struct netdev *netdev_)
{
    struct netdev_sai *netdev = __netdev_sai_cast(netdev_);

    SAI_API_TRACE_FN();

    return netdev->carrier_resets;
}

static int
__get_stats(const struct netdev *netdev_, struct netdev_stats *stats)
{
    int status = 0;
    struct netdev_sai *netdev = __netdev_sai_cast(netdev_);

    SAI_API_TRACE_FN();

    ovs_mutex_lock(&netdev->mutex);
    if (!netdev->is_initialized || !netdev->split_info.is_hw_lane_active) {
        goto exit;
    }

    if (STR_EQ(netdev_get_type(netdev_), OVSREC_INTERFACE_TYPE_SYSTEM)) {
        status = ops_sai_port_stats_get(netdev->hw_id, stats);
        ERRNO_EXIT(status);
    }

    if (netdev->rif_handle) {
        status = ops_sai_router_intf_get_stats(netdev->rif_handle, stats);
        ERRNO_EXIT(status);
    }

exit:
    ovs_mutex_unlock(&netdev->mutex);

    return status;
}

static int
__get_features(const struct netdev *netdev_, enum netdev_features *current,
               enum netdev_features *advertised,
               enum netdev_features *supported, enum netdev_features *peer)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return 0;
}

static int
__update_flags(struct netdev *netdev_, enum netdev_flags off,
               enum netdev_flags on, enum netdev_flags *old_flagsp)
{
    int status = 0;
    struct netdev_sai *netdev = __netdev_sai_cast(netdev_);

    SAI_API_TRACE_FN();

    ovs_mutex_lock(&netdev->mutex);
    if (netdev->is_initialized && netdev->split_info.is_hw_lane_active) {
        status = ops_sai_port_flags_update(netdev->hw_id, off, on, old_flagsp);
    } else {
        *old_flagsp = 0;
    }
    ovs_mutex_unlock(&netdev->mutex);

    return status;
}


static int
__update_flags_internal(struct netdev *netdev_,
                        enum netdev_flags off,
                        enum netdev_flags on, enum netdev_flags *old_flagsp)
{
    int status = 0;
    struct netdev_sai *netdev = __netdev_sai_cast(netdev_);

    SAI_API_TRACE_FN();

    ovs_mutex_lock(&netdev->mutex);

    if (netdev->is_initialized && netdev->split_info.is_hw_lane_active) {
        if (netdev->netdev_internal_admin_state) {
            *old_flagsp = NETDEV_UP;
        }

        if (on & NETDEV_UP) {
            netdev->netdev_internal_admin_state = true;
        } else if (off & NETDEV_UP) {
            netdev->netdev_internal_admin_state = false;
        }
    } else {
        *old_flagsp = 0;
    }

    ovs_mutex_unlock(&netdev->mutex);

    return status;
}

static int
__update_flags_loopback(struct netdev *netdev_,
                        enum netdev_flags off,
                        enum netdev_flags on, enum netdev_flags *old_flagsp)
{
    SAI_API_TRACE_FN();

    if ((off | on) & ~NETDEV_UP) {
        return EOPNOTSUPP;
    }

    *old_flagsp = NETDEV_UP | NETDEV_LOOPBACK;

    return 0;
}

/*
 * Find netdev_sai structure by name.
 */
static struct netdev_sai *
__netdev_sai_from_name(const char *name)
{
    bool found = false;
    struct netdev_sai *netdev = NULL;
    LIST_FOR_EACH(netdev, list_node, &sai_netdev_list) {
        if (STR_EQ(netdev_get_name(&netdev->up), name)) {
            found = true;
            break;
        }
    }

    return found ? netdev : NULL;
}

/*
 * Update port split configuration.
 */
static int
__update_split_config(struct netdev_sai *netdev)
{
    int status = 0;
    struct netdev_sai *parent_netdev = NULL;

    if (netdev->split_info.is_child) {
        parent_netdev = __netdev_sai_from_name(netdev->split_info.parent_name);
        ovs_assert(parent_netdev);
        status = __split(parent_netdev, netdev->default_config.max_speed);
        ERRNO_EXIT(status);
    } else if (netdev->split_info.is_splitable) {
        status = __unsplit(netdev, netdev->config.speed);
        ERRNO_EXIT(status);
    }

exit:
    return 0;
}

/*
 * Split netdev using the following algorithm:
 *
 * 1. If netdev is already split return.
 * 2. Remove netdev host interface.
 * 3. Split.
 * 4. For each child create host interface.
 *
 * @param[in] netdev parent netdev to be split.
 * @param[in] speed parent netdev port speed.
 *
 * @return 0, sai status converted to errno otherwise.
 */
static int
__split(struct netdev_sai *netdev, uint32_t speed)
{
    int status = 0;
    handle_t hw_id_handle = HANDLE_INITIALIZAER;
    struct netdev_sai *child_netdev = NULL;
    uint32_t child_netdev_cnt = 0;
    uint32_t hw_lanes[SAI_MAX_LANES] = { };
    const enum ops_sai_port_split split_mode = OPS_SAI_PORT_SPLIT_TO_4;

    NULL_PARAM_LOG_ABORT(netdev);

    if (!netdev->split_info.is_hw_lane_active) {
        goto exit;
    }

    VLOG_INFO("Splitting netdev (netdev: %s)", netdev_get_name(&netdev->up));

    status = __disable_neighbor_netdev_config(netdev, split_mode);
    ERRNO_LOG_EXIT(status, "Failed to disable neighbor netdev config "
                   "(netdev: %s)", netdev_get_name(&netdev->up));

    status = ops_sai_host_intf_netdev_remove(netdev_get_name(&netdev->up));
    ERRNO_LOG_EXIT(status,
                   "Failed to remove host interface (name: %s)",
                   netdev_get_name(&netdev->up));

    child_netdev_cnt = 0;
    LIST_FOR_EACH(child_netdev, list_node, &sai_netdev_list) {
        if (!child_netdev->split_info.is_child ||
                !STR_EQ(child_netdev->split_info.parent_name,
                        netdev_get_name(&netdev->up))) {
            continue;
        }

        hw_lanes[child_netdev_cnt++] = child_netdev->hw_id;
    }

    status = ops_sai_port_split(netdev->hw_id,
                                split_mode,
                                speed,
                                child_netdev_cnt,
                                hw_lanes);
    if (status) {
        VLOG_ERR("Failed to split port (name: %s)",
                 netdev_get_name(&netdev->up));

        hw_id_handle.data = netdev->hw_id;
        status = ops_sai_host_intf_netdev_create(netdev_get_name(&netdev->up),
                                                 HOST_INTF_TYPE_L2_PORT_NETDEV,
                                                 &hw_id_handle,
                                                 &netdev->mac_addr);
        ERRNO_LOG_EXIT(status,
                       "Failed to remove host interface (name: %s)",
                       netdev_get_name(&netdev->up));

        status = -1;
        goto exit;
    }

    netdev->split_info.is_hw_lane_active = false;

    LIST_FOR_EACH(child_netdev, list_node, &sai_netdev_list) {
        if (!child_netdev->split_info.is_child ||
                !STR_EQ(child_netdev->split_info.parent_name,
                        netdev_get_name(&netdev->up))) {
            continue;
        }

        child_netdev->split_info.is_hw_lane_active = true;

        hw_id_handle.data = child_netdev->hw_id;
        status = ops_sai_host_intf_netdev_create(netdev_get_name(&child_netdev->up),
                                                 HOST_INTF_TYPE_L2_PORT_NETDEV,
                                                 &hw_id_handle,
                                                 &child_netdev->mac_addr);
        ERRNO_LOG_EXIT(status,
                       "Failed to create port interface (name: %s)",
                       netdev_get_name(&child_netdev->up));
    }

exit:
    return status;
}

/*
 * Unsplit netdev using the following algorithm:
 *
 * 1. If netdev is already unsplit return.
 * 1. For each child remove netdev host interface.
 * 3. Unsplit.
 * 4. Create host interface for parent.
 *
 * @param[in] netdev parent netdev to be unsplit.
 * @param[in] speed parent netdev port speed.
 *
 * @return 0, sai status converted to errno otherwise.
 */
static int
__unsplit(struct netdev_sai *netdev, uint32_t speed)
{
    int status = 0;
    handle_t hw_id_handle;
    struct netdev_sai *child_netdev = NULL;
    uint32_t child_netdev_cnt = 0;
    uint32_t hw_lanes[SAI_MAX_LANES] = { };
    const enum ops_sai_port_split split_mode = OPS_SAI_PORT_SPLIT_TO_4;

    if (netdev->split_info.is_hw_lane_active) {
        goto exit;
    }

    VLOG_INFO("Un-splitting netdev (netdev: %s)", netdev_get_name(&netdev->up));

    /* For each sub-interface remove Linux netdev */
    child_netdev_cnt = 0;
    LIST_FOR_EACH(child_netdev, list_node, &sai_netdev_list) {
        if (!child_netdev->split_info.is_child ||
                !STR_EQ(child_netdev->split_info.parent_name,
                        netdev_get_name(&netdev->up))) {
            continue;
        }

        hw_lanes[child_netdev_cnt++] = child_netdev->hw_id;

        status = ops_sai_host_intf_netdev_remove(netdev_get_name(&child_netdev->up));
        ERRNO_LOG_EXIT(status,
                       "Failed to remove host interface (name: %s)",
                       netdev->split_info.parent_name);
    }

    /* Unsplit sub-interfaces */
    status = ops_sai_port_split(netdev->hw_id,
                                OPS_SAI_PORT_SPLIT_UNSPLIT,
                                speed,
                                child_netdev_cnt,
                                hw_lanes);
    if (status) {
        VLOG_ERR( "Failed to unsplit port. "
                "Rollback to split for all sub-interfaces (name: %s)",
                  netdev->split_info.parent_name);

        LIST_FOR_EACH(child_netdev, list_node, &sai_netdev_list) {
            if (!child_netdev->split_info.is_child ||
                    !STR_EQ(child_netdev->split_info.parent_name,
                            netdev_get_name(&netdev->up))) {
                continue;
            }

            status = ops_sai_host_intf_netdev_create(netdev_get_name(&child_netdev->up),
                                                     HOST_INTF_TYPE_L2_PORT_NETDEV,
                                                     &hw_id_handle,
                                                     &child_netdev->mac_addr);
            ERRNO_LOG_EXIT(status,
                           "Failed to create host interface (name: %s)",
                           netdev_get_name(&child_netdev->up));
        }

        status = -1;
        goto exit;
    }

    LIST_FOR_EACH(child_netdev, list_node, &sai_netdev_list) {
        if (!child_netdev->split_info.is_child ||
                !STR_EQ(child_netdev->split_info.parent_name,
                        netdev_get_name(&netdev->up))) {
            continue;
        }

        child_netdev->split_info.is_hw_lane_active = false;
    }

    netdev->split_info.is_hw_lane_active = true;

    /* Create parent Linux netdev */
    hw_id_handle.data = netdev->hw_id;
    status = ops_sai_host_intf_netdev_create(netdev_get_name(&netdev->up),
                                             HOST_INTF_TYPE_L2_PORT_NETDEV,
                                             &hw_id_handle,
                                             &netdev->mac_addr);
    ERRNO_LOG_EXIT(status,
                   "Failed to create host interface (name: %s)",
                   netdev_get_name(&netdev->up));

    status = __enable_neighbor_netdev_config(netdev, split_mode);
    ERRNO_LOG_EXIT(status, "Failed to enable neighbor netdev config "
                   "(netdev: %s)", netdev_get_name(&netdev->up));

exit:
    return status;
}

/*
 * Enable neighbor netdev config if neighbor netdev was disabled during split.
 *
 * @param[in] netdev parent netdev.
 * @param[in] split_mode split mode to be set during split.
 *
 * @return 0, sai status converted to errno otherwise.
 */
static int
__enable_neighbor_netdev_config(struct netdev_sai *netdev,
                                enum ops_sai_port_split split_mode)
{
    int status = 0;
    struct split_info split_info = { };
    struct netdev_sai *neighbor_netdev = NULL;

    status = ops_sai_port_split_info_get(netdev->hw_id,
                                         split_mode,
                                         &split_info);
    ERRNO_LOG_EXIT(status, "Failed to get netdev split info (netdev: %s)",
                   netdev_get_name(&netdev->up));

    if (!split_info.disable_neighbor) {
        goto exit;
    }

    LIST_FOR_EACH(neighbor_netdev, list_node, &sai_netdev_list) {
        if (neighbor_netdev->hw_id == split_info.neighbor_hw_id) {
            neighbor_netdev->split_info.is_hw_lane_active = true;

            status = ops_sai_port_config_set(neighbor_netdev->hw_id,
                                             &netdev->config, &netdev->default_config);
            ERRNO_LOG_EXIT(status, "Failed to set hw interface config");

            status = ofproto_sai_bundle_enable(netdev_get_name(&neighbor_netdev->up));
            ERRNO_LOG_EXIT(status, "Failed to enable neighbor port "
                           "(hw_id: %u, neighbor_hw_id: %u)",
                           netdev->hw_id, split_info.neighbor_hw_id);
        }
    }

exit:
    return status;
}

/*
 * Disable neighbor netdev config.
 *
 * @param[in] netdev parent netdev.
 * @param[in] split_mode split mode to be used for split.
 *
 * @return 0, sai status converted to errno otherwise.
 */
static int
__disable_neighbor_netdev_config(struct netdev_sai *netdev,
                                 enum ops_sai_port_split split_mode)
{
    int status = 0;
    struct split_info split_info = { };
    struct netdev_sai *neighbor_netdev = NULL;

    status = ops_sai_port_split_info_get(netdev->hw_id,
                                         split_mode,
                                         &split_info);
    ERRNO_LOG_EXIT(status, "Failed to get netdev split info (netdev: %s)",
                   netdev_get_name(&netdev->up));

    if (!split_info.disable_neighbor) {
        goto exit;
    }

    LIST_FOR_EACH(neighbor_netdev, list_node, &sai_netdev_list) {
        if (neighbor_netdev->hw_id == split_info.neighbor_hw_id) {
            status = ofproto_sai_bundle_disable(netdev_get_name(&neighbor_netdev->up));
            ERRNO_LOG_EXIT(status, "Failed to disable neighbor port "
                           "(hw_id: %u, neighbor_hw_id: %u)",
                           netdev->hw_id, split_info.neighbor_hw_id);

            neighbor_netdev->split_info.is_hw_lane_active = false;
        }
    }

exit:
    return status;
}
