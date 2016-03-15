/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.  
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#include <config.h>
#include <errno.h>
#include <linux/ethtool.h>
#include <netinet/ether.h>

#include <netdev-provider.h>
#include <openvswitch/vlog.h>
#include <openflow/openflow.h>
#include <openswitch-idl.h>
#include <openswitch-dflt.h>
#include <sai-api-class.h>
#include <sai-log.h>

VLOG_DEFINE_THIS_MODULE(netdev_sai);

#define SAI_CONFIG_DEFAULT_HW_ENABLE false
#define SAI_CONFIG_DEFAULT_MTU 1500

/* Protects 'sai_list'. */
static struct ovs_mutex sai_netdev_list_mutex = OVS_MUTEX_INITIALIZER;
static struct ovs_list sai_netdev_list OVS_GUARDED_BY(sai_netdev_list_mutex)
    = OVS_LIST_INITIALIZER(&sai_netdev_list);

struct netdev_sai_config {
    bool hw_enable;
    bool autoneg;
    int mtu;
    int speed;
};

struct netdev_sai {
    struct netdev up;
    struct ovs_list list_node OVS_GUARDED_BY(sai_netdev_list_mutex);
    struct ovs_mutex mutex OVS_ACQ_AFTER(sai_netdev_list_mutex);
    uint32_t hw_id;
    bool is_port_initialized;
    long long int carrier_resets;
    struct netdev_sai_config config;
    int max_speed;
    struct eth_addr mac_addr;
};

static inline bool is_sai_class(const struct netdev_class *);
static inline struct netdev_sai *netdev_sai_cast(const struct netdev *);
static struct netdev *netdev_sai_alloc(void);
static int netdev_sai_construct(struct netdev *);
static void netdev_sai_destruct(struct netdev *);
static void netdev_sai_dealloc(struct netdev *);
static void netdev_sai_mac_read(struct eth_addr *, const char *);
static int netdev_sai_set_hw_intf_info(struct netdev *, const struct smap *);
static bool netdev_sai_hw_intf_config_changed(const struct netdev_sai_config *,
                                              const struct netdev_sai_config *);
static sai_status_t netdev_sai_set_hw_intf_config_full(struct netdev_sai *,
                                                       const struct netdev_sai_config *);
static sai_status_t netdev_sai_set_hw_intf_config__(struct netdev_sai *,
                                                    const struct netdev_sai_config *);
static bool netdev_sai_args_autoneg_get(const struct smap *);
static int netdev_sai_set_hw_intf_config(struct netdev *, const struct smap *);
static sai_status_t netdev_sai_set_etheraddr__(struct netdev *,
                                               const struct eth_addr mac);
static int netdev_sai_set_etheraddr(struct netdev *, const struct eth_addr mac);
static int netdev_sai_get_etheraddr(const struct netdev *, struct eth_addr *mac);
static int netdev_sai_get_mtu(const struct netdev *, int *);
static sai_status_t netdev_sai_set_mtu__(const struct netdev *, int);
static int netdev_sai_set_mtu(const struct netdev *, int);
static int netdev_sai_get_carrier(const struct netdev *, bool *);
static long long int netdev_sai_get_carrier_resets(const struct netdev *);
static int netdev_sai_get_stats(const struct netdev *, struct netdev_stats *);
static int netdev_sai_get_features(const struct netdev *,
                                   enum netdev_features *,
                                   enum netdev_features *,
                                   enum netdev_features *,
                                   enum netdev_features *);
static int netdev_sai_update_flags(struct netdev *, enum netdev_flags,
                                   enum netdev_flags, enum netdev_flags *);

static const struct netdev_class netdev_sai_class = {
    "system",
    NULL,                       /* init */
    NULL,                       /* run */
    NULL,                       /* wait */

    netdev_sai_alloc,
    netdev_sai_construct,
    netdev_sai_destruct,
    netdev_sai_dealloc,
    NULL,                       /* get_config */
    NULL,                       /* set_config */
    netdev_sai_set_hw_intf_info,
    netdev_sai_set_hw_intf_config,
    NULL,                       /* get_tunnel_config */
    NULL,                       /* build header */
    NULL,                       /* push header */
    NULL,                       /* pop header */
    NULL,                       /* get_numa_id */
    NULL,                       /* set_multiq */

    NULL,                       /* send */
    NULL,                       /* send_wait */

    netdev_sai_set_etheraddr,
    netdev_sai_get_etheraddr,
    netdev_sai_get_mtu,
    netdev_sai_set_mtu,
    NULL,                       /* get_ifindex */
    netdev_sai_get_carrier,
    netdev_sai_get_carrier_resets,
    NULL,                       /* set_miimon_interval */
    netdev_sai_get_stats,

    netdev_sai_get_features,
    NULL,                       /* set_advertisements */

    NULL,                       /* set_policing */
    NULL,                       /* get_qos_types */
    NULL,                       /* get_qos_capabilities */
    NULL,                       /* get_qos */
    NULL,                       /* set_qos */
    NULL,                       /* get_queue */
    NULL,                       /* set_queue */
    NULL,                       /* delete_queue */
    NULL,                       /* get_queue_stats */
    NULL,                       /* queue_dump_start */
    NULL,                       /* queue_dump_next */
    NULL,                       /* queue_dump_done */
    NULL,                       /* dump_queue_stats */

    NULL,                       /* get_in4 */
    NULL,                       /* set_in4 */
    NULL,                       /* get_in6 */
    NULL,                       /* add_router */
    NULL,                       /* get_next_hop */
    NULL,                       /* get_status */
    NULL,                       /* arp_lookup */

    netdev_sai_update_flags,

    NULL,                       /* rxq_alloc */
    NULL,                       /* rxq_construct */
    NULL,                       /* rxq_destruct */
    NULL,                       /* rxq_dealloc */
    NULL,                       /* rxq_recv */
    NULL,                       /* rxq_wait */
    NULL,                       /* rxq_drain */
};

static const struct netdev_class netdev_sai_internal_class = {
    "internal",
    NULL,                       /* init */
    NULL,                       /* run */
    NULL,                       /* wait */

    netdev_sai_alloc,
    netdev_sai_construct,
    netdev_sai_destruct,
    netdev_sai_dealloc,
    NULL,                       /* get_config */
    NULL,                       /* set_config */
    netdev_sai_set_hw_intf_info,
    netdev_sai_set_hw_intf_config,
    NULL,                       /* get_tunnel_config */
    NULL,                       /* build header */
    NULL,                       /* push header */
    NULL,                       /* pop header */
    NULL,                       /* get_numa_id */
    NULL,                       /* set_multiq */

    NULL,                       /* send */
    NULL,                       /* send_wait */

    netdev_sai_set_etheraddr,
    netdev_sai_get_etheraddr,
    netdev_sai_get_mtu,
    netdev_sai_set_mtu,
    NULL,                       /* get_ifindex */
    netdev_sai_get_carrier,
    netdev_sai_get_carrier_resets,
    NULL,                       /* set_miimon_interval */
    netdev_sai_get_stats,

    netdev_sai_get_features,
    NULL,                       /* set_advertisements */

    NULL,                       /* set_policing */
    NULL,                       /* get_qos_types */
    NULL,                       /* get_qos_capabilities */
    NULL,                       /* get_qos */
    NULL,                       /* set_qos */
    NULL,                       /* get_queue */
    NULL,                       /* set_queue */
    NULL,                       /* delete_queue */
    NULL,                       /* get_queue_stats */
    NULL,                       /* queue_dump_start */
    NULL,                       /* queue_dump_next */
    NULL,                       /* queue_dump_done */
    NULL,                       /* dump_queue_stats */

    NULL,                       /* get_in4 */
    NULL,                       /* set_in4 */
    NULL,                       /* get_in6 */
    NULL,                       /* add_router */
    NULL,                       /* get_next_hop */
    NULL,                       /* get_status */
    NULL,                       /* arp_lookup */

    netdev_sai_update_flags,

    NULL,                       /* rxq_alloc */
    NULL,                       /* rxq_construct */
    NULL,                       /* rxq_destruct */
    NULL,                       /* rxq_dealloc */
    NULL,                       /* rxq_recv */
    NULL,                       /* rxq_wait */
    NULL,                       /* rxq_drain */
};

void
netdev_sai_register(void)
{
    netdev_register_provider(&netdev_sai_class);
    netdev_register_provider(&netdev_sai_internal_class);
}

sai_object_id_t
netdev_sai_oid_get(struct netdev *netdev_)
{
    struct netdev_sai *netdev = netdev_sai_cast(netdev_);

    return sai_api_hw_id2port_id(netdev->hw_id);
}

void
netdev_sai_port_oper_state_changed(sai_object_id_t oid, int link_status)
{
    struct netdev_sai *dev = NULL, *next_dev = NULL;

    LIST_FOR_EACH_SAFE(dev, next_dev, list_node, &sai_netdev_list) {
        if (dev->is_port_initialized
            && sai_api_hw_id2port_id(dev->hw_id) == oid) {
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

static inline bool
is_sai_class(const struct netdev_class *class)
{
    return class->construct == netdev_sai_construct;
}

static inline struct netdev_sai *
netdev_sai_cast(const struct netdev *netdev)
{
    ovs_assert(is_sai_class(netdev_get_class(netdev)));
    return CONTAINER_OF(netdev, struct netdev_sai, up);
}

static struct netdev *
netdev_sai_alloc(void)
{
    struct netdev_sai *netdev = xzalloc(sizeof *netdev);

    SAI_API_TRACE_FN();

    return &(netdev->up);
}

static int
netdev_sai_construct(struct netdev *netdev_)
{
    struct netdev_sai *netdev = netdev_sai_cast(netdev_);

    SAI_API_TRACE_FN();

    ovs_mutex_init(&netdev->mutex);
    ovs_mutex_lock(&sai_netdev_list_mutex);
    list_push_back(&sai_netdev_list, &netdev->list_node);
    ovs_mutex_unlock(&sai_netdev_list_mutex);

    return 0;
}

static void
netdev_sai_destruct(struct netdev *netdev_)
{
    struct netdev_sai *netdev = netdev_sai_cast(netdev_);

    SAI_API_TRACE_FN();

    ovs_mutex_lock(&sai_netdev_list_mutex);
    list_remove(&netdev->list_node);
    ovs_mutex_unlock(&sai_netdev_list_mutex);
    ovs_mutex_destroy(&netdev->mutex);
}

static void
netdev_sai_dealloc(struct netdev *netdev_)
{
    struct netdev_sai *netdev = netdev_sai_cast(netdev_);

    SAI_API_TRACE_FN();

    free(netdev);
}

static void
netdev_sai_mac_read(struct eth_addr *eth_addr, const char *mac_str)
{
    uint8_t *mac = eth_addr->ea;
    sscanf(mac_str, "%hhux:%hhux:%hhux:%hhux:%hhux:%hhux:",
          &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
}

static int
netdev_sai_set_hw_intf_info(struct netdev *netdev_, const struct smap *args)
{
    struct eth_addr mac;
    sai_attribute_t hostif_attrib[3] = { };
    sai_status_t status = SAI_STATUS_SUCCESS;
    sai_object_id_t hif_id_port = SAI_NULL_OBJECT_ID;
    struct netdev_sai *netdev = netdev_sai_cast(netdev_);
    const struct sai_api_class *sai_api = sai_api_get_instance();
    const int hw_id =
        smap_get_int(args, INTERFACE_HW_INTF_INFO_MAP_SWITCH_INTF_ID, -1);
    int max_speed = smap_get_int(args, INTERFACE_HW_INTF_INFO_MAP_MAX_SPEED, -1);
    /* Temporary hardcode until reading from EEPROM will be fixed. */
    const char *mac_str = "20:03:04:05:06:00";

    SAI_API_TRACE_FN();

    ovs_mutex_lock(&netdev->mutex);

    if (netdev->is_port_initialized
        || !strcmp(netdev_->name, DEFAULT_BRIDGE_NAME)) {
        goto exit;
    }

    netdev->hw_id = hw_id;
    ovs_assert(max_speed != -1);
    netdev->max_speed = max_speed;
    netdev_sai_mac_read(&mac, mac_str);

    hostif_attrib[0].id = SAI_HOSTIF_ATTR_TYPE;
    hostif_attrib[0].value.s32 = SAI_HOSTIF_TYPE_NETDEV;
    hostif_attrib[1].id = SAI_HOSTIF_ATTR_NAME;
    strcpy(hostif_attrib[1].value.chardata, netdev->up.name);
    hostif_attrib[2].id = SAI_HOSTIF_ATTR_RIF_OR_PORT_ID;
    hostif_attrib[2].value.oid = sai_api_hw_id2port_id(netdev->hw_id);

    status = sai_api->host_interface_api->create_hostif(&hif_id_port, 3,
                                                        hostif_attrib);
    SAI_ERROR_LOG_EXIT(status, "Failed to create port interface hw_id: %d",
                       hw_id);

    status = netdev_sai_set_etheraddr__(netdev_, mac);
    SAI_ERROR_EXIT(status);

    netdev->is_port_initialized = true;

exit:
    ovs_mutex_unlock(&netdev->mutex);
    return SAI_ERROR(status);
}

static bool
netdev_sai_hw_intf_config_changed(const struct netdev_sai_config *old,
                                  const struct netdev_sai_config *new)
{
    if (old->hw_enable != new->hw_enable ||
        old->autoneg != new->autoneg ||
        old->mtu != new->mtu ||
        old->speed != new->speed) {
        return true;
    }

    return false;
}


static sai_status_t
netdev_sai_set_hw_intf_config_full(struct netdev_sai *netdev,
                                   const struct netdev_sai_config *config)
{
    sai_attribute_t attr = { };
    sai_status_t status = SAI_STATUS_SUCCESS;
    const struct sai_api_class *sai_api = sai_api_get_instance();
    sai_object_id_t port_id = sai_api_hw_id2port_id(netdev->hw_id);

    attr.id = SAI_PORT_ATTR_AUTO_NEG_MODE;
    attr.value.booldata = config->autoneg;
    status = sai_api->port_api->set_port_attribute(port_id, &attr);
    SAI_ERROR_LOG_EXIT(status, "Failed to set autoneg %d for port %d",
                       config->autoneg, netdev->hw_id);

    attr.id = SAI_PORT_ATTR_SPEED;
    attr.value.u32 = config->speed;
    status = sai_api->port_api->set_port_attribute(port_id, &attr);
    SAI_ERROR_LOG_EXIT(status, "Failed to set speed %d for port %d",
                       config->speed, netdev->hw_id);

    status = netdev_sai_set_mtu__(&netdev->up, config->mtu);
    SAI_ERROR_EXIT(status);

    /* TODO: Duplex - integrate with SAI. */

    netdev->config.hw_enable = config->hw_enable;
    netdev->config.autoneg = config->autoneg;
    netdev->config.mtu = config->mtu;
    netdev->config.speed = config->speed;

exit:
    return status;
}

static sai_status_t
netdev_sai_set_hw_intf_config__(struct netdev_sai *netdev,
                                const struct netdev_sai_config *config)
{
    sai_attribute_t attr = { };
    sai_status_t status = SAI_STATUS_SUCCESS;
    const struct sai_api_class *sai_api = sai_api_get_instance();
    sai_object_id_t port_id = sai_api_hw_id2port_id(netdev->hw_id);

    if (config->hw_enable) {
        status = netdev_sai_set_hw_intf_config_full(netdev, config);
        SAI_ERROR_EXIT(status);
    }

    attr.id = SAI_PORT_ATTR_ADMIN_STATE;
    attr.value.booldata = config->hw_enable;
    status = sai_api->port_api->set_port_attribute(port_id, &attr);
    SAI_ERROR_LOG_EXIT(status, "Failed to set admin state %s for port %d",
                       config->hw_enable ? "UP" : "DOWN", netdev->hw_id);

exit:
    return status;
}

static bool
netdev_sai_args_autoneg_get(const struct smap *args)
{
    const char *autoneg = smap_get(args,
                                   INTERFACE_HW_INTF_CONFIG_MAP_ENABLE);

    if (NULL != autoneg &&
        !strcmp(autoneg, INTERFACE_USER_CONFIG_MAP_AUTONEG_ON)) {
        return true;
    }

    return false;
}

static int
netdev_sai_set_hw_intf_config(struct netdev *netdev_, const struct smap *args)
{
    struct netdev_sai_config config = { };
    sai_status_t status = SAI_STATUS_SUCCESS;
    struct netdev_sai *netdev = netdev_sai_cast(netdev_);
    /* Max speed must be always present (in yaml config file). */
    config.hw_enable = smap_get_bool(args,
                                     INTERFACE_HW_INTF_CONFIG_MAP_ENABLE,
                                     SAI_CONFIG_DEFAULT_HW_ENABLE);
    config.autoneg = netdev_sai_args_autoneg_get(args);
    config.mtu = smap_get_int(args, INTERFACE_HW_INTF_CONFIG_MAP_MTU,
                              SAI_CONFIG_DEFAULT_MTU);
    config.speed = smap_get_int(args, INTERFACE_HW_INTF_CONFIG_MAP_SPEEDS,
                                netdev->max_speed);

    ovs_mutex_lock(&netdev->mutex);

    if (!netdev_sai_hw_intf_config_changed(&netdev->config, &config)) {
        goto exit;
    }

    status = netdev_sai_set_hw_intf_config__(netdev, &config);
    SAI_ERROR_LOG_EXIT(status, "Failed to set hw interface config");

exit:
    ovs_mutex_unlock(&netdev->mutex);
    return SAI_ERROR(status);
}

static sai_status_t
netdev_sai_set_etheraddr__(struct netdev *netdev,
                           const struct eth_addr mac)
{
    sai_status_t status = SAI_STATUS_SUCCESS;
    struct netdev_sai *dev = netdev_sai_cast(netdev);

    if (!dev->is_port_initialized) {
        goto exit;
    }

    /* Not supported by SAI. */

    memcpy(&dev->mac_addr, &mac, sizeof (dev->mac_addr));

exit:
    return status;
}

static int
netdev_sai_set_etheraddr(struct netdev *netdev,
                         const struct eth_addr mac)
{
    sai_status_t status = SAI_STATUS_SUCCESS;
    struct netdev_sai *dev = netdev_sai_cast(netdev);

    SAI_API_TRACE_FN();

    ovs_mutex_lock(&dev->mutex);
    status = netdev_sai_set_etheraddr__(netdev, mac);
    ovs_mutex_unlock(&dev->mutex);

    return SAI_ERROR(status);
}

static int
netdev_sai_get_etheraddr(const struct netdev *netdev,
                         struct eth_addr *mac)
{
    struct netdev_sai *dev = netdev_sai_cast(netdev);

    SAI_API_TRACE_FN();

    ovs_mutex_lock(&dev->mutex);
    if (!dev->is_port_initialized) {
        goto exit;
    }

    memcpy(mac, &dev->mac_addr, sizeof (*mac));

exit:
    ovs_mutex_unlock(&dev->mutex);
    return 0;
}

static int
netdev_sai_get_mtu(const struct netdev *netdev_, int *mtup)
{
    sai_attribute_t attr = { };
    sai_status_t status = SAI_STATUS_SUCCESS;
    struct netdev_sai *netdev = netdev_sai_cast(netdev_);
    const struct sai_api_class *sai_api = sai_api_get_instance();

    SAI_API_TRACE_FN();

    ovs_mutex_lock(&netdev->mutex);
    if (!netdev->is_port_initialized) {
        goto exit;
    }

    attr.id = SAI_PORT_ATTR_MTU;
    status = sai_api->port_api->get_port_attribute(sai_api_hw_id2port_id(netdev->hw_id),
                                                   1, &attr);
    SAI_ERROR_LOG_EXIT(status, "Failed to get mtu for port %d",
                        netdev->hw_id);

    *mtup = attr.value.u32;

exit:
    ovs_mutex_unlock(&netdev->mutex);
    return SAI_ERROR(status);
}

static sai_status_t
netdev_sai_set_mtu__(const struct netdev *netdev_, int mtu)
{
    sai_attribute_t attr = { };
    sai_status_t status = SAI_STATUS_SUCCESS;
    struct netdev_sai *netdev = netdev_sai_cast(netdev_);
    const struct sai_api_class *sai_api = sai_api_get_instance();

    if (!netdev->is_port_initialized) {
        goto exit;
    }

    attr.id = SAI_PORT_ATTR_MTU;
    attr.value.u32 = mtu;
    status = sai_api->port_api->set_port_attribute(sai_api_hw_id2port_id(netdev->hw_id),
                                                   &attr);
    SAI_ERROR_LOG_EXIT(status, "Failed to set %d mtu for port %d", mtu,
                        netdev->hw_id);

exit:
    return status;
}

static int
netdev_sai_set_mtu(const struct netdev *netdev_, int mtu)
{
    sai_status_t status = SAI_STATUS_SUCCESS;
    struct netdev_sai *netdev = netdev_sai_cast(netdev_);

    SAI_API_TRACE_FN();

    ovs_mutex_lock(&netdev->mutex);
    status = netdev_sai_set_mtu__(netdev_, mtu);
    ovs_mutex_unlock(&netdev->mutex);

    return SAI_ERROR(status);
}

static int
netdev_sai_get_carrier(const struct netdev *netdev_, bool * carrier)
{
    sai_attribute_t attr = { };
    sai_status_t status = SAI_STATUS_SUCCESS;
    struct netdev_sai *netdev = netdev_sai_cast(netdev_);
    const struct sai_api_class *sai_api = sai_api_get_instance();

    SAI_API_TRACE_FN();

    ovs_mutex_lock(&netdev->mutex);
    if (!netdev->is_port_initialized) {
        goto exit;
    }

    attr.id = SAI_PORT_ATTR_OPER_STATUS;
    status = sai_api->port_api->get_port_attribute(sai_api_hw_id2port_id(netdev->hw_id),
            1, &attr);
    SAI_ERROR_LOG_EXIT(status, "Failed to set mtu for port %d",
                        netdev->hw_id);

    *carrier = (sai_port_oper_status_t) attr.value.u32 ==
        SAI_PORT_OPER_STATUS_UP;

exit:
    ovs_mutex_unlock(&netdev->mutex);
    return SAI_ERROR(status);
}

static long long int
netdev_sai_get_carrier_resets(const struct netdev *netdev_)
{
    struct netdev_sai *netdev = netdev_sai_cast(netdev_);

    SAI_API_TRACE_FN();

    return netdev->carrier_resets;
}

static int
netdev_sai_get_stats(const struct netdev *netdev_, struct netdev_stats *stats)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return 0;
}

static int
netdev_sai_get_features(const struct netdev *netdev_,
                        enum netdev_features *current,
                        enum netdev_features *advertised,
                        enum netdev_features *supported,
                        enum netdev_features *peer)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return 0;
}

static int
netdev_sai_update_flags(struct netdev *netdev_,
                        enum netdev_flags off,
                        enum netdev_flags on, enum netdev_flags *old_flagsp)
{
    sai_attribute_t attr = { };
    sai_status_t status = SAI_STATUS_SUCCESS;
    struct netdev_sai *netdev = netdev_sai_cast(netdev_);
    const struct sai_api_class *sai_api = sai_api_get_instance();

    SAI_API_TRACE_FN();

    ovs_mutex_lock(&netdev->mutex);
    if (!netdev->is_port_initialized) {
        *old_flagsp = 0;
        goto exit;
    }

    attr.id = SAI_PORT_ATTR_ADMIN_STATE;
    status = sai_api->port_api->get_port_attribute(sai_api_hw_id2port_id(netdev->hw_id),
                                                   1, &attr);
    SAI_ERROR_LOG_EXIT(status, "Failed to get admin state from port %d",
                        netdev->hw_id);

    if (attr.value.booldata) {
        *old_flagsp |= NETDEV_UP;
    }

    if (on & NETDEV_UP) {
        attr.value.booldata = true;
    } else if (off & NETDEV_UP) {
        attr.value.booldata = false;
    } else {
        goto exit;
    }

    status = sai_api->port_api->set_port_attribute(sai_api_hw_id2port_id(netdev->hw_id),
                                                   &attr);
    SAI_ERROR_LOG_EXIT(status, "Failed to set admin state on port %d",
                        netdev->hw_id);

exit:
    ovs_mutex_unlock(&netdev->mutex);
    return SAI_ERROR(status);
}
