/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#ifndef SAI_ROUTE_H
#define SAI_ROUTE_H 1

#include <sai-common.h>
#include <sai-vendor-common.h>

struct route_class {
    /**
    * Initializes route.
    */
    void (*init)(void);
    /**
     *  Function for adding IP to me route.
     *  Means tell hardware to trap packets with specified prefix to CPU.
     *  Used while assigning IP address(s) to routing interface.
     *
     * @param[in] vrid    - virtual router ID
     * @param[in] prefix  - IP prefix
     *
     * @return 0     if operation completed successfully.
     * @return errno if operation failed.*/
    int  (*ip_to_me_add)(const handle_t *vrid, const char *prefix);
    /**
     *  Function for adding local route.
     *  Means while creating new routing interface and assigning IP address to it
     *  'tells' the hardware that new subnet is accessible through specified
     *  router interface.
     *
     * @param[in] vrid    - virtual router ID
     * @param[in] prefix  - IP prefix
     * @param[in] rifid   - router interface ID
     *
     * @return 0     if operation completed successfully.
     * @return errno if operation failed.*/
    int  (*local_add)(const handle_t *vrid,
                      const char     *prefix,
                      const handle_t *rifid);
    /**
     *  Function for adding next hops(list of remote routes) which are accessible
     *  over specified IP prefix
     *
     * @param[in] vrid           - virtual router ID
     * @param[in] prefix         - IP prefix
     * @param[in] next_hop_count - count of next hops
     * @param[in] next_hops      - list of next hops
     *
     * @return 0     if operation completed successfully.
     * @return errno if operation failed.*/
    int  (*remote_add)(handle_t           vrid,
                       const char        *prefix,
                       uint32_t           next_hop_count,
                       char *const *const next_hops);
    /**
     *  Function for deleting next hops(list of remote routes) which now are not
     *  accessible over specified IP prefix
     *
     * @param[in] vrid           - virtual router ID
     * @param[in] prefix         - IP prefix
     * @param[in] next_hop_count - count of next hops
     * @param[in] next_hops      - list of next hops
     *
     * @return 0     if operation completed successfully.
     * @return errno if operation failed.*/
    int  (*remote_nh_remove)(handle_t            vrid,
                             const char         *prefix,
                             uint32_t            next_hop_count,
                             char *const *const  next_hops);
    /**
     *  Function for deleting remote route
     *
     * @param[in] vrid   - virtual router ID
     * @param[in] prefix - IP prefix over which next hops can be accessed
     *
     * @notes if next hops were already configured earlier for this route then
     *        delete all next-hops of a route as well as the route itself.
     *
     * @return 0     if operation completed successfully.
     * @return errno if operation failed.*/
    int  (*remove)(const handle_t *vrid, const char *prefix);
    /**
     * De-initializes route.
     */
    void (*deinit)(void);
};

DECLARE_GENERIC_CLASS_GETTER(struct route_class, route);

#define ops_sai_route_class_generic() (CLASS_GENERIC_GETTER(route)())

#ifndef ops_sai_route_class
#define ops_sai_route_class ops_sai_route_class_generic
#endif

static inline void
ops_sai_route_init(void)
{
    ovs_assert(ops_sai_route_class()->init);
    ops_sai_route_class()->init();
}

static inline int
ops_sai_route_ip_to_me_add(const handle_t *vrid, const char *prefix)
{
    ovs_assert(ops_sai_route_class()->ip_to_me_add);
    return ops_sai_route_class()->ip_to_me_add(vrid, prefix);
}

static inline int
ops_sai_route_local_add(const handle_t *vrid,
                        const char     *prefix,
                        const handle_t *rifid)
{
    ovs_assert(ops_sai_route_class()->local_add);
    return ops_sai_route_class()->local_add(vrid, prefix, rifid);
}

static inline int
ops_sai_route_remote_add(handle_t           vrid,
                         const char        *prefix,
                         uint32_t           next_hop_count,
                         char *const *const next_hops)
{
    ovs_assert(ops_sai_route_class()->remote_add);
    return ops_sai_route_class()->remote_add(vrid, prefix, next_hop_count,
                                             next_hops);
}

static inline int
ops_sai_route_remote_nh_remove(handle_t           vrid,
                               const char        *prefix,
                               uint32_t           next_hop_count,
                               char *const *const next_hops)
{
    ovs_assert(ops_sai_route_class()->remote_nh_remove);
    return ops_sai_route_class()->remote_nh_remove(vrid, prefix,
                                                   next_hop_count,
                                                   next_hops);
}

static inline int
ops_sai_route_remove(const handle_t *vrid, const char     *prefix)
{
    ovs_assert(ops_sai_route_class()->remove);
    return ops_sai_route_class()->remove(vrid, prefix);
}

static inline void
ops_sai_route_deinit(void)
{
    ovs_assert(ops_sai_route_class()->deinit);
    ops_sai_route_class()->deinit();
}

#endif /* sai-route.h */
