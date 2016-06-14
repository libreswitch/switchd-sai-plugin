/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#include <sai-log.h>
#include <sai-api-class.h>
#include <sai-router-intf.h>
#include <sai-port.h>
#include <netdev.h>

VLOG_DEFINE_THIS_MODULE(sai_router_intf);

/*
 * Returns string representation of router interface type.
 *
 * @param[in] type - Router interface type.
 *
 * @return 0 operation completed successfully
 * @return errno operation failed
 */
const char *ops_sai_router_intf_type_to_str(enum router_intf_type type)
{
    const char *str = NULL;

    switch(type) {
    case ROUTER_INTF_TYPE_PORT:
        str = "port";
        break;
    case ROUTER_INTF_TYPE_VLAN:
        str = "vlan";
        break;
    default:
        str = "unknown";
        break;
    }

    return str;
}

/*
 * Initializes router interface.
 */
static void __router_intf_init(void)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
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
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
    return 0;
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
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
    return 0;
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
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
    return 0;
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
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
    return 0;
}

/*
 * De-initializes router interface.
 */
static void __router_intf_deinit(void)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
}

DEFINE_GENERIC_CLASS(struct router_intf_class, router_intf) = {
        .init = __router_intf_init,
        .create = __router_intf_create,
        .remove = __router_intf_remove,
        .set_state = __router_intf_set_state,
        .get_stats = __router_intf_get_stats,
        .deinit = __router_intf_deinit
};

DEFINE_GENERIC_CLASS_GETTER(struct router_intf_class, router_intf);