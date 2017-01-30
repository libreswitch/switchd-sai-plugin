/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#include <mlnx_sai.h>

#include <sai-log.h>
#include <sai-common.h>
#include <sai-router.h>

#include <sai-vendor-util.h>

VLOG_DEFINE_THIS_MODULE(mlnx_sai_router);

/*
 * Initializes virtual router.
 */
static void
__router_init(void)
{
    VLOG_INFO("Initializing virtual router");
}

/*
 * Creates virtual router.
 *
 * @param[out] vrid - Virtual router id.
 *
 * @return 0 operation completed successfully
 * @return errno operation failed
 */
static int
__router_create(handle_t *handle)
{
    sx_status_t            status = SX_STATUS_SUCCESS;
    sx_router_id_t         sx_vrid = 0;
    sx_router_attributes_t router_attr = { };

    VLOG_INFO("Creating virtual router");

    ovs_assert(handle);

    router_attr.ipv4_enable = 1;
    router_attr.ipv6_enable = 1;
    router_attr.ipv4_mc_enable = 0;
    router_attr.ipv6_mc_enable = 0;
    router_attr.uc_default_rule_action = SX_ROUTER_ACTION_DROP;
    router_attr.mc_default_rule_action = SX_ROUTER_ACTION_DROP;

    status = sx_api_router_set(gh_sdk,
                               SX_ACCESS_CMD_ADD,
                               &router_attr,
                               &sx_vrid);
    SX_ERROR_LOG_EXIT(status, "Failed to add create router (error: %s)",
                      SX_STATUS_MSG(status));

    handle->data = sx_vrid;

exit:
    return SX_ERROR_2_ERRNO(status);
}

/*
 * Removes virtual router.
 *
 * @param[out] vrid - Virtual router id.
 *
 * @return 0 operation completed successfully
 * @return errno operation failed
 */
static int
__router_remove(const handle_t *handle)
{
    sx_status_t    status = SX_STATUS_SUCCESS;
    sx_router_id_t sx_vrid = (sx_router_id_t)handle->data;

    ovs_assert(handle);

    VLOG_INFO("Removing virtual router (vrid: %lu)", handle->data);

    status = sx_api_router_set(gh_sdk, SX_ACCESS_CMD_DELETE, NULL, &sx_vrid);
    SX_ERROR_LOG_EXIT(status, "Failed to add delete router (error: %s)",
                      SX_STATUS_MSG(status));

exit:
    return SX_ERROR_2_ERRNO(status);
}

/*
 * De-initializes virtual router.
 */
static void
__router_deinit(void)
{
    VLOG_INFO("De-initializing virtual router");
}

DEFINE_VENDOR_CLASS(struct router_class, router) = {
    .init = __router_init,
    .create = __router_create,
    .remove = __router_remove,
    .deinit = __router_deinit
};

DEFINE_VENDOR_CLASS_GETTER(struct router_class, router);
