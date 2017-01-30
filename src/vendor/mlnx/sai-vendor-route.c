/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#include <mlnx_sai.h>

#include <sai-common.h>
#include <sai-log.h>
#include <sai-route.h>

#include <sai-vendor-util.h>

VLOG_DEFINE_THIS_MODULE(mlnx_sai_route);

/*
 * Initializes route.
 */
static void
__route_init(void)
{
    VLOG_INFO("Initializing route");
}

static int
__route_remote_action(uint64_t           vrid,
                      const char        *prefix,
                      uint32_t           next_hop_count,
                      char *const *const next_hops,
                      sx_access_cmd_t    action)
{
    sx_status_t        status = SX_STATUS_SUCCESS;
    sx_ip_prefix_t     sx_prefix = { };
    sx_uc_route_data_t route_data = { };
    sx_ip_addr_t      *sx_next_hops = route_data.next_hop_list_p;

    if (0 != ops_sai_common_ip_prefix_to_sx_ip_prefix(prefix, &sx_prefix)) {
        status = SX_STATUS_PARAM_ERROR;
        SX_ERROR_LOG_EXIT(status, "Invalid prefix"
                          "(prefix: %s)", prefix);
    }

    for (uint32_t index = 0; index < next_hop_count; index++) {
        if (0 != ops_sai_common_ip_to_sx_ip(next_hops[index],
                                            &sx_next_hops[index])) {
            status = SX_STATUS_PARAM_ERROR;
            SX_ERROR_LOG_EXIT(status, "Invalid next hop"
                              "(index: %u, next hop: %s)",
                              index, next_hops[index]);
        }
    }

    route_data.action = SX_ROUTER_ACTION_FORWARD;
    route_data.type = SX_UC_ROUTE_TYPE_NEXT_HOP;
    route_data.uc_route_param.ecmp_id = SX_ROUTER_ECMP_ID_INVALID;
    route_data.next_hop_cnt = next_hop_count;

    status = sx_api_router_uc_route_set(gh_sdk,
                                        action,
                                        (sx_router_id_t)vrid,
                                        &sx_prefix,
                                        &route_data);
exit:
    return status;
}

/*
 *  Function for adding IP to me route.
 *  Means tell hardware to trap packets with specified prefix to CPU.
 *  Used while assigning IP address(s) to routing interface.
 *
 * @param[in] vrid    - virtual router ID
 * @param[in] prefix  - IP prefix
 *
 * @return 0  if operation completed successfully.
 * @return -1 if operation failed.*/
static int
__route_ip_to_me_add(const handle_t *vrid, const char *prefix)
{
    sx_status_t        status = SX_STATUS_SUCCESS;
    sx_ip_prefix_t     sx_prefix = { };
    sx_uc_route_data_t route_data = { };

    VLOG_INFO("Adding IP2ME route (prefix: %s)", prefix);

    if (0 != ops_sai_common_ip_prefix_to_sx_ip_prefix(prefix, &sx_prefix)) {
        status = SX_STATUS_PARAM_ERROR;
        SX_ERROR_LOG_EXIT(status, "Invalid prefix (prefix: %s)", prefix);
    }

    route_data.action = SX_ROUTER_ACTION_TRAP;
    route_data.type = SX_UC_ROUTE_TYPE_IP2ME;
    route_data.uc_route_param.ecmp_id = SX_ROUTER_ECMP_ID_INVALID;
    route_data.next_hop_cnt = 0;

    status = sx_api_router_uc_route_set(gh_sdk,
                                        SX_ACCESS_CMD_ADD,
                                        (sx_router_id_t)vrid->data,
                                        &sx_prefix,
                                        &route_data);
    SX_ERROR_LOG_EXIT(status,
                      "Failed to create IP2ME route (prefix: %s, error: %s)",
                      prefix, SX_STATUS_MSG(status));

exit:
    return SX_ERROR_2_ERRNO(status);
}

/*
 *  Function for adding local route.
 *  Means while creating new routing interface and assigning IP address to it
 *  'tells' the hardware that new subnet is accessible through specified
 *  router interface.
 *
 * @param[in] vrid    - virtual router ID
 * @param[in] prefix  - IP prefix
 * @param[in] rifid   - router interface ID
 *
 * @return 0  if operation completed successfully.
 * @return -1 if operation failed.*/
static int
__route_local_add(const handle_t *vrid,
                  const char     *prefix,
                  const handle_t *rifid)
{
    sx_status_t        status = SX_STATUS_SUCCESS;
    sx_ip_prefix_t     sx_prefix = { };
    sx_uc_route_data_t route_data = { };

    VLOG_INFO("Adding local route (prefix: %s, rif_handle: %lu)",
              prefix,
              rifid->data);

    if (0 != ops_sai_common_ip_prefix_to_sx_ip_prefix(prefix, &sx_prefix)) {
        status = SX_STATUS_PARAM_ERROR;
        SX_ERROR_LOG_EXIT(status, "Invalid prefix (prefix: %s)", prefix);
    }

    route_data.action = SX_ROUTER_ACTION_FORWARD;
    route_data.type = SX_UC_ROUTE_TYPE_LOCAL;
    route_data.uc_route_param.local_egress_rif =
        (sx_router_interface_t)rifid->data;

    status = sx_api_router_uc_route_set(gh_sdk,
                                        SX_ACCESS_CMD_ADD,
                                        (sx_router_id_t)vrid->data,
                                        &sx_prefix,
                                        &route_data);
    SX_ERROR_LOG_EXIT(status,
                      "Failed to create IP2ME route (prefix: %s, error: %s)",
                      prefix, SX_STATUS_MSG(status));

exit:
    return SX_ERROR_2_ERRNO(status);
}

/*
 *  Function for adding next hops(list of remote routes) which are accessible
 *  over specified IP prefix
 *
 * @param[in] vrid           - virtual router ID
 * @param[in] prefix         - IP prefix
 * @param[in] next_hop_count - count of next hops
 * @param[in] next_hops      - list of next hops
 *
 * @return 0  if operation completed successfully.
 * @return -1 if operation failed.*/
static int
__route_remote_add(handle_t           vrid,
                   const char        *prefix,
                   uint32_t           next_hop_count,
                   char *const *const next_hops)
{
    sx_status_t status = SX_STATUS_SUCCESS;

    VLOG_INFO("Adding next hop(s) for remote route"
              "(prefix: %s, next hop count %u)", prefix, next_hop_count);

    ovs_assert(prefix);
    ovs_assert(next_hops);
    ovs_assert(next_hop_count);
    ovs_assert(next_hop_count <= RM_API_ROUTER_NEXT_HOP_MAX);

    status = __route_remote_action(vrid.data, prefix, next_hop_count,
                                   next_hops, SX_ACCESS_CMD_ADD);

    SX_ERROR_LOG_EXIT(status, "Failed to add remote route"
                      "(prefix: %s, next hop count %u, error: %s)",
                      prefix, next_hop_count, SX_STATUS_MSG(status));

exit:
    return SX_ERROR_2_ERRNO(status);
}

/*
 *  Function for deleting next hops(list of remote routes) which now are not
 *  accessible over specified IP prefix
 *
 * @param[in] vrid           - virtual router ID
 * @param[in] prefix         - IP prefix
 * @param[in] next_hop_count - count of next hops
 * @param[in] next_hops      - list of next hops
 *
 * @return 0  if operation completed successfully.
 * @return -1 if operation failed.*/
static int
__route_remote_nh_remove(handle_t           vrid,
                         const char        *prefix,
                         uint32_t           next_hop_count,
                         char *const *const next_hops)
{
    sx_status_t status = SX_STATUS_SUCCESS;

    VLOG_INFO("Removing next hop(s) for remote route"
              "(prefix: %s, next hop count: %u)", prefix, next_hop_count);

    ovs_assert(prefix);

    status = __route_remote_action(vrid.data, prefix, next_hop_count,
                                   next_hops, SX_ACCESS_CMD_DELETE);

    SX_ERROR_LOG_EXIT(status, "Failed to remove next hop for remote route"
                      "(prefix: %s, next hop count: %u, error: %s)",
                      prefix, next_hop_count, SX_STATUS_MSG(status));

exit:
    return SX_ERROR_2_ERRNO(status);
}

/*
 *  Function for deleting remote route
 *
 * @param[in] vrid           - virtual router ID
 * @param[in] prefix         - IP prefix over which next hops can be accessed
 *
 * @notes if next hops were already configured earlier for this route then
 *        delete all next-hops of a route as well as the route itself.
 *
 * @return 0  if operation completed successfully.
 * @return -1 if operation failed.*/
static int
__route_remove(const handle_t *vrid, const char     *prefix)
{
    sx_status_t status = SX_STATUS_SUCCESS;

    VLOG_INFO("Removing route (prefix: %s)", prefix);

    status = __route_remote_action(vrid->data, prefix, 0, 0,
                                   SX_ACCESS_CMD_DELETE);

    SX_ERROR_LOG_EXIT(status, "Failed to remove remote route"
                      "(prefix: %s, error: %s)", prefix,
                      SX_STATUS_MSG(status));

exit:
    return SX_ERROR_2_ERRNO(status);
}

/*
 * De-initializes route.
 */
static void
__route_deinit(void)
{
    VLOG_INFO("De-initializing route");
}

DEFINE_VENDOR_CLASS(struct route_class, route) = {
    .init = __route_init,
    .ip_to_me_add = __route_ip_to_me_add,
    .local_add = __route_local_add,
    .remote_add = __route_remote_add,
    .remote_nh_remove = __route_remote_nh_remove,
    .remove = __route_remove,
    .deinit = __route_deinit,
};

DEFINE_VENDOR_CLASS_GETTER(struct route_class, route);
