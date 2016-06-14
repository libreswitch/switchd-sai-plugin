/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#include <mlnx_sai.h>

#include <hmap.h>
#include <hash.h>
#include <netdev.h>

#include <sai-log.h>
#include <sai-api-class.h>
#include <sai-router-intf.h>
#include <sai-port.h>

#include <sai-vendor-util.h>

VLOG_DEFINE_THIS_MODULE(mlnx_sai_router_intf);

static struct hmap all_router_intf = HMAP_INITIALIZER(&all_router_intf);

struct rif_entry {
    struct hmap_node rif_hmap_node;
    sx_router_interface_t rif_id;
    sx_router_counter_id_t counter_id;
    enum router_intf_type type;
    handle_t handle;
};

/*
 * Find router interface entry in hash map.
 *
 * @param[in] rif_hmap        - Hash map.
 * @param[in] rif_handle      - Router interface handle used as map key.
 */
static struct rif_entry*
__router_intf_entry_hmap_find(struct hmap *rif_hmap, const handle_t *rif_handle)
{
    struct rif_entry* rif_entry = NULL;

    ovs_assert(rif_handle);

    HMAP_FOR_EACH_WITH_HASH(rif_entry, rif_hmap_node,
                            hash_uint64(rif_handle->data), rif_hmap) {
        if (rif_entry->rif_id == rif_handle->data) {
            return rif_entry;
        }
    }

    return NULL;
}

/*
 * Add router interface entry to hash map.
 *
 * @param[in] rif_hmap        - Hash map.
 * @param[in] rif_handle      - Router interface handle used as map key.
 * @param[in] rif_entry       - Router interface entry.
 */
static void
__router_intf_entry_hmap_add(struct hmap *rif_hmap,
                             const handle_t *rif_handle,
                             const struct rif_entry* rif_entry)
{
    struct rif_entry *rif_entry_int = NULL;

    ovs_assert(!__router_intf_entry_hmap_find(rif_hmap, rif_handle));

    rif_entry_int = xzalloc(sizeof(*rif_entry_int));
    memcpy(rif_entry_int, rif_entry, sizeof(*rif_entry_int));

    hmap_insert(rif_hmap, &rif_entry_int->rif_hmap_node,
                hash_uint64(rif_handle->data));
}

/*
 * Delete router interface entry from hash map.
 *
 * @param[in] rif_hmap        - Hash map.
 * @param[in] rif_handle      - Router interface handle used as map key.
 */
static void
__router_intf_entry_hmap_del(struct hmap *rif_hmap, const handle_t *rif_handle)
{
    struct rif_entry* rif_entry = __router_intf_entry_hmap_find(rif_hmap,
                                                                rif_handle);
    if (rif_entry) {
        hmap_remove(rif_hmap, &rif_entry->rif_hmap_node);
        free(rif_entry);
    }
}

/*
 * Port transaction callback. To be called when port type changed from L3 ot L2.
 *
 * @param[in] hw_id port label id.
 *
 * @return 0, sai status converted to errno otherwise.
 */
void __mlnx_port_transaction_to_l2(uint32_t hw_id)
{
    sx_status_t status = SX_STATUS_SUCCESS;
    sai_status_t sai_status = SAI_STATUS_SUCCESS;
    uint32_t obj_data = 0;
    sx_mstp_inst_port_state_t port_state = 0;

    VLOG_INFO("Starting port transaction to L2 (port_id: %u)",
              hw_id);

    sai_status = mlnx_object_to_type(ops_sai_api_hw_id2port_id(hw_id),
                                     SAI_OBJECT_TYPE_PORT,
                                     &obj_data,
                                     NULL);
    SAI_ERROR_LOG_ABORT(sai_status, "Failed to get port id (port_id: %u)",
                        hw_id);

    /*
     * SDK issue workaround:
     * Set STP state to DISCARDING before FORWARDING to update DB correctly
     * */
    status = sx_api_rstp_port_state_get(gh_sdk,(sx_port_log_id_t)obj_data, &port_state);
    SX_ERROR_LOG_EXIT(status,
                      "Failed to get STP state (port_id: %u, error: %s)",
                      hw_id,
                      SX_STATUS_MSG(status));

    status = sx_api_rstp_port_state_set(gh_sdk,
                                        (sx_port_log_id_t)obj_data,
                                        SX_MSTP_INST_PORT_STATE_DISCARDING);
    SX_ERROR_LOG_EXIT(status,
                      "Failed to set STP state (port_id: %u, error: %s)",
                      hw_id,
                      SX_STATUS_MSG(status));

    status = sx_api_rstp_port_state_set(gh_sdk,
                                        (sx_port_log_id_t)obj_data,
                                        SX_MSTP_INST_PORT_STATE_FORWARDING);
    SX_ERROR_LOG_EXIT(status,
                      "Failed to set STP state (port_id: %u, error: %s)",
                      hw_id,
                      SX_STATUS_MSG(status));

exit:
    return;
}

/*
 * Initializes router interface.
 */
static void __router_intf_init(void)
{
    int err = 0;

    VLOG_INFO("Initializing router interface");

    err = ops_sai_port_transaction_register_callback(__mlnx_port_transaction_to_l2,
                                                     OPS_SAI_PORT_TRANSACTION_TO_L2);
    ERRNO_LOG_ABORT(err, "Failed to register port transaction callback");
}

/*
 * Creates router interface.
 *
 * @param[in] vrid_handle - Virtual router id.
 * @param[in] type        - Router interface type.
 * @param[in] handle      - Router interface handle (Port lable ID or VLAN ID).
 * @param[in] addr        - Router interface MAC address. If not specified
 *                          default value will be used.
 * @param[in] mtu         - Router interface MTU. If not specified default
 *                          value will be used
 * @param[out] rif_handle - Router interface handle.
 *
 * @return 0 operation completed successfully
 * @return errno operation failed
 */
static int __router_intf_create(const handle_t *vr_handle,
                               enum router_intf_type type,
                               const handle_t *handle,
                               const struct ether_addr *addr,
                               uint16_t mtu, handle_t *rif_handle)
{
    sx_status_t                   status = SX_STATUS_SUCCESS;
    sai_status_t                  sai_status = SAI_STATUS_SUCCESS;
    sx_router_interface_t         sdk_rif_id = 0;
    uint32_t                      obj_data = 0;
    sx_interface_attributes_t     intf_attribs = { };
    sx_router_interface_param_t   intf_params = { };
    sx_router_counter_id_t        counter_id = 0;
    struct rif_entry              router_intf = {};

    ovs_assert(vr_handle);
    ovs_assert(handle);
    ovs_assert(rif_handle);
    ovs_assert(ROUTER_INTF_TYPE_PORT == type ||
               ROUTER_INTF_TYPE_VLAN == type);

    VLOG_INFO("Creating router interface (vrid: %lu, type: %s, handle: %lu)",
              vr_handle->data, ops_sai_router_intf_type_to_str(type), handle->data);

    if (ROUTER_INTF_TYPE_PORT == type) {
        sai_status = mlnx_object_to_type(ops_sai_api_hw_id2port_id(handle->data),
                                         SAI_OBJECT_TYPE_PORT,
                                         &obj_data,
                                         NULL);
        SAI_ERROR_LOG_ABORT(sai_status, "Failed to get port id (handle: %lu)",
                            handle->data);

        intf_params.type = SX_L2_INTERFACE_TYPE_PORT_VLAN;
        intf_params.ifc.port_vlan.port = (sx_port_log_id_t) obj_data;
        intf_params.ifc.port_vlan.vlan = 0;
    } else {
        intf_params.type = SX_L2_INTERFACE_TYPE_VLAN;
        intf_params.ifc.vlan.swid = DEFAULT_ETH_SWID;
        intf_params.ifc.vlan.vlan = (sx_vlan_id_t) handle->data;
    }

    intf_attribs.multicast_ttl_threshold = DEFAULT_MULTICAST_TTL_THRESHOLD;
    intf_attribs.qos_mode = SX_ROUTER_QOS_MODE_NOP;

    if (mtu) {
        intf_attribs.mtu = mtu;
    } else {
        intf_attribs.mtu = DEFAULT_RIF_MTU;
    }

    if (addr) {
        memcpy(&intf_attribs.mac_addr, addr, sizeof(intf_attribs.mac_addr));
    } else {
        /* Get default mac from switch object. Use switch first port,
         * and zero down lower 6 bits port part (64 ports) */
        status = sx_api_port_phys_addr_get(gh_sdk, FIRST_PORT,
                                           &intf_attribs.mac_addr);
        SX_ERROR_LOG_EXIT(status, "Failed to get port address (error: %s)",
                          SX_STATUS_MSG(status));
        intf_attribs.mac_addr.ether_addr_octet[5] &= PORT_MAC_BITMASK;
    }

    status = sx_api_router_interface_set(gh_sdk, SX_ACCESS_CMD_ADD,
                                         (sx_router_id_t) vr_handle->data,
                                         &intf_params, &intf_attribs,
                                         &sdk_rif_id);
    SX_ERROR_LOG_EXIT(status, "Failed to create router interface (error: %s)",
                      SX_STATUS_MSG(status));

    status = sx_api_router_counter_set(gh_sdk,
                                       SX_ACCESS_CMD_CREATE,
                                       &counter_id);
    SX_ERROR_LOG_EXIT(status,
                      "Failed to create router interface counter "
                      "(rif_id: %u, error: %s)",
                      sdk_rif_id,
                      SX_STATUS_MSG(status));

    status = sx_api_router_interface_counter_bind_set(gh_sdk,
                                                      SX_ACCESS_CMD_BIND,
                                                      counter_id,
                                                      sdk_rif_id);
    SX_ERROR_LOG_EXIT(status,
                      "Failed to bind router interface counter "
                      "(rif_id: %u, coundter_id: %u, error: %s)",
                      sdk_rif_id,
                      counter_id,
                      SX_STATUS_MSG(status));

    rif_handle->data = sdk_rif_id;

    router_intf.rif_id = sdk_rif_id;
    router_intf.counter_id = counter_id;
    router_intf.type = type;
    memcpy(&router_intf.handle, handle, sizeof(router_intf.handle));

    if (ROUTER_INTF_TYPE_PORT == type) {
        ops_sai_port_transaction(handle->data, OPS_SAI_PORT_TRANSACTION_TO_L3);
    }

    __router_intf_entry_hmap_add(&all_router_intf, rif_handle, &router_intf);

exit:
    return SX_ERROR_2_ERRNO(status);
}

/*
 * Removes router interface.
 *
 * @param[in] rif_handle - Router interface handle.
 *
 * @return 0 operation completed successfully
 * @return errno operation failed
 */
static int __router_intf_remove(handle_t *rifid_handle)
{
    sx_status_t status = SX_STATUS_SUCCESS;
    sx_router_id_t vrid = 0;

    sx_interface_attributes_t intf_attribs = { };
    sx_router_interface_param_t intf_params = { };
    struct rif_entry *router_intf = NULL;

    ovs_assert(rifid_handle);

    router_intf = __router_intf_entry_hmap_find(&all_router_intf, rifid_handle);
    ovs_assert(router_intf);

    VLOG_INFO("Removing router interface (rifid: %u)", router_intf->rif_id);

    status = sx_api_router_interface_counter_bind_set(gh_sdk,
                                                      SX_ACCESS_CMD_UNBIND,
                                                      router_intf->counter_id,
                                                      router_intf->rif_id);
    SX_ERROR_LOG_EXIT(status,
                      "Failed to unbind router interface counter "
                      "(rif_id: %u, coundter_id: %u, error: %s)",
                      router_intf->rif_id,
                      router_intf->counter_id,
                      SX_STATUS_MSG(status));

    status = sx_api_router_counter_set(gh_sdk,
                                       SX_ACCESS_CMD_DESTROY,
                                       &router_intf->counter_id);
    SX_ERROR_LOG_EXIT(status,
                      "Failed to delete router interface counter "
                      "(rif_id: %u, counter_id: %u, error: %s)",
                      router_intf->rif_id,
                      router_intf->counter_id,
                      SX_STATUS_MSG(status));

    status = sx_api_router_interface_get(gh_sdk,
                                         router_intf->rif_id,
                                         &vrid, &intf_params,
                                         &intf_attribs);
    SX_ERROR_LOG_EXIT(status,
                      "Failed to get router interface attributes "
                      "(rif_id: %u error: %s)",
                      router_intf->rif_id,
                      SX_STATUS_MSG(status));

    status = sx_api_router_interface_set(gh_sdk,
                                         SX_ACCESS_CMD_DELETE,
                                         vrid,
                                         &intf_params,
                                         &intf_attribs,
                                         &router_intf->rif_id);
    SX_ERROR_LOG_EXIT(status,
                      "Failed to delete router interface "
                      "(rif_id: %u, error: %s)",
                      router_intf->rif_id,
                      SX_STATUS_MSG(status));

    if (router_intf->type == ROUTER_INTF_TYPE_PORT) {
        ops_sai_port_transaction(router_intf->handle.data,
                                 OPS_SAI_PORT_TRANSACTION_TO_L2);
    }

    __router_intf_entry_hmap_del(&all_router_intf, rifid_handle);

exit:
    return SX_ERROR_2_ERRNO(status);
}

/*
 * Set router interface admin state.
 *
 * @param[in] rif_handle - Router interface handle.
 * @param[in] state      - Router interface state.
 *
 * @return 0 operation completed successfully
 * @return errno operation failed
 */
static int __router_intf_set_state(const handle_t *rif_handle, bool state)
{
    sx_status_t status = SX_STATUS_SUCCESS;
    sx_router_interface_state_t rif_state = { };
    const struct rif_entry *router_intf = NULL;

    ovs_assert(rif_handle);

    router_intf = __router_intf_entry_hmap_find(&all_router_intf, rif_handle);
    ovs_assert(router_intf);

    VLOG_INFO("Setting router interface state (rifid: %u, state: %d)",
              router_intf->rif_id,
              state);

    rif_state.ipv4_enable = state;
    rif_state.ipv6_enable = state;

    status = sx_api_router_interface_state_set(gh_sdk,
                                               router_intf->rif_id,
                                               &rif_state);
    SX_ERROR_LOG_EXIT(status,
                      "Failed to set router interface state "
                      "(rif_id: %u, state: %d, error: %s)",
                      router_intf->rif_id,
                      state,
                      SX_STATUS_MSG(status));

exit:
    return SX_ERROR_2_ERRNO(status);
}

/*
 * Get router interface statistics.
 *
 * @param[in] rif_handle - Router interface handle.
 * @param[out] stats     - Router interface statisctics.
 *
 * @return 0 operation completed successfully
 * @return errno operation failed
 */
static int __router_intf_get_stats(const handle_t *rif_handle,
                                  struct netdev_stats *stats)
{
    sx_status_t status = SX_STATUS_SUCCESS;
    sx_router_counter_set_t cntr_set = { };
    const struct rif_entry *router_intf = NULL;
    /* Rate limiter for statistics info messages.
     * Allow to show max 10 messages in a minute */
    static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(10, 10);

    ovs_assert(rif_handle);

    router_intf = __router_intf_entry_hmap_find(&all_router_intf, rif_handle);
    if (!router_intf) {
        goto exit;
    }

    VLOG_INFO_RL(&rl, "Getting router interface statistics (rifid: %u)",
                 router_intf->rif_id);

    status = sx_api_router_counter_get(gh_sdk,
                                       SX_ACCESS_CMD_READ,
                                       router_intf->counter_id,
                                       &cntr_set);
    SX_ERROR_LOG_EXIT(status,
                      "Failed to get router interface statistics "
                      "(rif_id: %u, error: %s)",
                      router_intf->rif_id,
                      SX_STATUS_MSG(status));

    stats->l3_uc_tx_packets = cntr_set.router_egress_good_unicast_packets;
    stats->l3_uc_tx_bytes = cntr_set.router_egress_good_unicast_bytes;
    stats->l3_uc_rx_packets = cntr_set.router_ingress_good_unicast_packets;
    stats->l3_uc_rx_bytes = cntr_set.router_ingress_good_unicast_bytes;

    stats->l3_mc_tx_packets = cntr_set.router_egress_good_multicast_packets;
    stats->l3_mc_tx_bytes = cntr_set.router_egress_good_multicast_bytes;
    stats->l3_mc_rx_packets = cntr_set.router_ingress_good_multicast_packets;
    stats->l3_mc_rx_bytes = cntr_set.router_ingress_good_multicast_bytes;

exit:
    return SX_ERROR_2_ERRNO(status);
}

/*
 * De-initializes router interface.
 */
static void __router_intf_deinit(void)
{
    VLOG_INFO("De-initializing router interface");
}

DEFINE_VENDOR_CLASS(struct router_intf_class, router_intf) = {
        .init = __router_intf_init,
        .create = __router_intf_create,
        .remove = __router_intf_remove,
        .set_state = __router_intf_set_state,
        .get_stats = __router_intf_get_stats,
        .deinit = __router_intf_deinit
};

DEFINE_VENDOR_CLASS_GETTER(struct router_intf_class, router_intf);
