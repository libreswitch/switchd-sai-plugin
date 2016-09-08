/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#include <packets.h>

#include <sai-log.h>
#include <sai-neighbor.h>

/* should be included last due to defines conflicts */
#include <sai-vendor-util.h>

VLOG_DEFINE_THIS_MODULE(mlnx_sai_neighbor);

/*
 * Initializes neighbor.
 */
static void
__neighbor_init(void)
{
    VLOG_INFO("Initializing neighbor");
}

static int
__neighbor_action(bool        is_ipv6_addr,
                  const char *ip_addr,
                  const char *mac_addr,
                  uint64_t    rifid,
                  int         action)
{
    sx_status_t     status = SX_STATUS_SUCCESS;
    sx_ip_addr_t    sx_ipaddr = { };
    sx_neigh_data_t neigh_data = { };

    ovs_assert(ip_addr);

    sx_ipaddr.version = ((true == is_ipv6_addr) ? SX_IP_VERSION_IPV6 :
                         SX_IP_VERSION_IPV4);

    if (NULL != mac_addr) {
        status = eth_addr_from_string(mac_addr,
                                      (struct eth_addr*)&neigh_data.mac_addr);
        if (!status) {
            status = SX_STATUS_PARAM_ERROR;
            SX_ERROR_LOG_EXIT(status, "Invalid MAC address: %s",
                              mac_addr);
        }
    }
    neigh_data.action = SX_ROUTER_ACTION_FORWARD;
    neigh_data.rif = (sx_router_interface_t)rifid;
    neigh_data.trap_attr.prio = SX_TRAP_PRIORITY_MED;

    status = ops_sai_common_ip_to_sx_ip(ip_addr, &sx_ipaddr);
    if (0 != status) {
        status = SX_STATUS_PARAM_ERROR;
        SX_ERROR_LOG_EXIT(status, "Invalid IP address: %s", ip_addr);
    }

    status = sx_api_router_neigh_set(gh_sdk,
                                     (sx_access_cmd_t)action,
                                     (sx_router_interface_t)rifid,
                                     &sx_ipaddr,
                                     &neigh_data);

exit:
    return status;
}

/*
 *  This function adds a neighbour information.
 *
 * @param[in] is_ipv6_addr - is address IPv6 or not
 * @param[in] ip_addr      - neighbor IP address
 * @param[in] mac_addr     - neighbor MAC address
 * @param[in] rif          - router Interface ID
 *
 * @return 0  if operation completed successfully.
 * @return -1 if operation failed.*/
static int
__neighbor_create(bool           is_ipv6_addr,
                  const char     *ip_addr,
                  const char     *mac_addr,
                  const handle_t *rifid)
{
    sx_status_t status = SX_STATUS_SUCCESS;

    VLOG_INFO("Creating neighbor (isIPv6: %u, ip: %s, mac: %s, rif: %lu)",
              is_ipv6_addr, ip_addr, mac_addr, rifid->data);

    status = __neighbor_action(is_ipv6_addr,
                               ip_addr,
                               mac_addr,
                               rifid->data,
                               SX_ACCESS_CMD_ADD);

    SX_ERROR_LOG_EXIT(status, "Failed to create neighbor entry"
                      "(ip: %s, mac: %s, rif: %lu, error: %s)",
                      ip_addr, mac_addr, rifid->data, SX_STATUS_MSG(status));

exit:
    return SX_ERROR_2_ERRNO(status);
}

/*
 *  This function deletes a neighbour information.
 *
 * @param[in] is_ipv6_addr - is address IPv6 or not
 * @param[in] ip_addr      - neighbor IP address
 * @param[in] rif          - router Interface ID
 *
 * @return 0  if operation completed successfully.
 * @return -1 if operation failed.*/
static int
__neighbor_remove(bool is_ipv6_addr, const char *ip_addr, const handle_t *rifid)
{
    sx_status_t status = SX_STATUS_SUCCESS;

    VLOG_INFO("Removing neighbor(ip: %s, rif: %lu)", ip_addr, rifid->data);

    status = __neighbor_action(is_ipv6_addr,
                               ip_addr,
                               NULL,
                               rifid->data,
                               SX_ACCESS_CMD_DELETE);

    SX_ERROR_LOG_EXIT(status, "Failed to remove neighbor entry"
                      "(ip: %s, rif: %lu, error: %s)",
                      ip_addr, rifid->data, SX_STATUS_MSG(status));

exit:
    return SX_ERROR_2_ERRNO(status);
}

/*
 *  This function reads the neighbor's activity information.
 *
 * @param[in] is_ipv6_addr - is address IPv6 or not
 * @param[in] ip_addr      - neighbor IP address
 * @param[in] rif          - router Interface ID
 * @param[out] activity_p  - activity
 *
 * @return 0  if operation completed successfully.
 * @return -1 if operation failed.*/
static int
__neighbor_activity_get(bool           is_ipv6_addr,
                        const char     *ip_addr,
                        const handle_t *rifid,
                        bool           *activity)
{
    sx_status_t  status = SX_STATUS_SUCCESS;
    sx_ip_addr_t sx_ipaddr;
    boolean_t    bool_val = false;

    ovs_assert(ip_addr || activity);

    VLOG_INFO("Getting neighbor activity (ip address: %s, rif: %lu)",
              ip_addr, rifid->data);

    memset(&sx_ipaddr,  0, sizeof(sx_ipaddr));
    sx_ipaddr.version = ((true == is_ipv6_addr) ? SX_IP_VERSION_IPV6 :
                         SX_IP_VERSION_IPV4);

    status = ops_sai_common_ip_to_sx_ip(ip_addr, &sx_ipaddr);
    if (0 != status) {
        status = SX_STATUS_PARAM_ERROR;
        SX_ERROR_LOG_EXIT(status, "Invalid IP address: %s", ip_addr);
    }

    status = sx_api_router_neigh_activity_get(gh_sdk,
                                              SX_ACCESS_CMD_READ,
                                              (sx_router_interface_t)
                                              rifid->data,
                                              &sx_ipaddr,
                                              &bool_val);
    *activity = (bool)bool_val;

    SX_ERROR_LOG_EXIT(status, "Failed to get neighbor activity"
                      "(ip address: %s, rif: %lu, error: %s)",
                      ip_addr, rifid->data, SX_STATUS_MSG(status));

    VLOG_INFO("Neighbor activity is %u (ip address: %s, rif: %lu)",
              *activity, ip_addr, rifid->data);

exit:
    return SX_ERROR_2_ERRNO(status);
}

/*
 * De-initializes neighbor.
 */
static void
__neighbor_deinit(void)
{
    VLOG_INFO("De-initializing neighbor");
}

DEFINE_VENDOR_CLASS(struct neighbor_class, neighbor) = {
    .init = __neighbor_init,
    .create = __neighbor_create,
    .remove = __neighbor_remove,
    .activity_get = __neighbor_activity_get,
    .deinit = __neighbor_deinit
};

DEFINE_VENDOR_CLASS_GETTER(struct neighbor_class, neighbor);
