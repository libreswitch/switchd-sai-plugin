/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#ifndef SAI_ROUTER_INTF_H
#define SAI_ROUTER_INTF_H 1

#include <sai-common.h>
#ifdef SAI_VENDOR
#include <sai-vendor-common.h>
#endif /* SAI_VENDOR */

struct netdev_stats;

enum router_intf_type {
    ROUTER_INTF_TYPE_PORT,
    ROUTER_INTF_TYPE_VLAN
};

struct router_intf_class {
    /**
     * Initializes router interface.
     */
    void (*init)(void);
    /**
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
     * @return 0     operation completed successfully
     * @return errno operation failed
     */
    int (*create)(const handle_t          *vrid_handle,
                  enum router_intf_type    type,
                  const handle_t          *handle,
                  const struct ether_addr *addr,
                  uint16_t                 mtu,
                  handle_t                *rif_handle);
    /**
     * Removes router interface.
     *
     * @param[in] rif_handle - Router interface handle.
     *
     * @return 0     operation completed successfully
     * @return errno operation failed
     */
    int (*remove)(handle_t *rif_handle);
    /**
     * Set router interface admin state.
     *
     * @param[in] rif_handle - Router interface handle.
     * @param[in] state      - Router interface state.
     *
     * @return 0     operation completed successfully
     * @return errno operation failed
     */
    int (*set_state)(const handle_t *rif_handle, bool state);
    /**
     * Get router interface statistics.
     *
     * @param[in]  rif_handle - Router interface handle.
     * @param[out] stats      - Router interface statisctics.
     *
     * @return 0     operation completed successfully
     * @return errno operation failed
     */
    int (*get_stats)(const handle_t *rif_handle, struct netdev_stats *stats);
    /**
     * De-initializes router interface.
     */
    void (*deinit)(void);
};

DECLARE_GENERIC_CLASS_GETTER(struct router_intf_class, router_intf);

#define ops_sai_router_intf_class_generic() (CLASS_GENERIC_GETTER(router_intf)())

#ifndef ops_sai_router_intf_class
#define ops_sai_router_intf_class           ops_sai_router_intf_class_generic
#endif

static inline void ops_sai_router_intf_init(void)
{
    ovs_assert(ops_sai_router_intf_class()->init);
    ops_sai_router_intf_class()->init();
}

static inline int ops_sai_router_intf_create(const handle_t *vr_handle,
                               enum router_intf_type type,
                               const handle_t *handle,
                               const struct ether_addr *addr,
                               uint16_t mtu, handle_t *rif_handle)
{
    ovs_assert(ops_sai_router_intf_class()->create);
    return ops_sai_router_intf_class()->create(vr_handle, type, handle,
                                               addr, mtu, rif_handle);
}

static inline int ops_sai_router_intf_remove(handle_t *rifid_handle)
{
    ovs_assert(ops_sai_router_intf_class()->remove);
    return ops_sai_router_intf_class()->remove(rifid_handle);
}

static inline int ops_sai_router_intf_set_state(const handle_t *rif_handle, bool state)
{
    ovs_assert(ops_sai_router_intf_class()->set_state);
    return ops_sai_router_intf_class()->set_state(rif_handle, state);
}

static inline int ops_sai_router_intf_get_stats(const handle_t *rif_handle,
                                  struct netdev_stats *stats)
{
    ovs_assert(ops_sai_router_intf_class()->get_stats);
    return ops_sai_router_intf_class()->get_stats(rif_handle, stats);
}

static inline void ops_sai_router_intf_deinit(void)
{
    ovs_assert(ops_sai_router_intf_class()->deinit);
    ops_sai_router_intf_class()->deinit();
}

const char *ops_sai_router_intf_type_to_str(enum router_intf_type type);

#endif /* sai-router-intf.h */
