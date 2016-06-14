/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#ifndef SAI_NEIGHBOR_H
#define SAI_NEIGHBOR_H 1

#include <sai-common.h>
#include <sai-vendor-common.h>

struct neighbor_class {
    /**
    * Initializes neighbor.
    */
    void (*init)(void);
    /**
     *  This function adds a neighbour information.
     *
     * @param[in] is_ipv6_addr - is address IPv6 or not
     * @param[in] ip_addr      - neighbor IP address
     * @param[in] mac_addr     - neighbor MAC address
     * @param[in] rif          - router Interface ID
     *
     * @return 0     if operation completed successfully.
     * @return errno if operation failed.*/
    int  (*create)(bool           is_ipv6_addr,
                   const char     *ip_addr,
                   const char     *mac_addr,
                   const handle_t *rif);
    /**
     *  This function deletes a neighbour information.
     *
     * @param[in] is_ipv6_addr - is address IPv6 or not
     * @param[in] ip_addr      - neighbor IP address
     * @param[in] rif          - router Interface ID
     *
     * @return 0     if operation completed successfully.
     * @return errno if operation failed.*/
    int  (*remove)(bool           is_ipv6_addr,
                   const char     *ip_addr,
                   const handle_t *rif);
    /**
     *  This function reads the neighbor's activity information.
     *
     * @param[in]  is_ipv6_addr - is address IPv6 or not
     * @param[in]  ip_addr      - neighbor IP address
     * @param[in]  rif          - router Interface ID
     * @param[out] activity_p   - activity
     *
     * @return 0     if operation completed successfully.
     * @return errno if operation failed.*/
    int  (*activity_get)(bool           is_ipv6_addr,
                         const char     *ip_addr,
                         const handle_t *rif,
                         bool           *activity_p);
    /**
     * De-initializes neighbor.
     */
    void (*deinit)(void);
};

DECLARE_GENERIC_CLASS_GETTER(struct neighbor_class, neighbor);

#define ops_sai_neighbor_class_generic() (CLASS_GENERIC_GETTER(neighbor)())

#ifndef ops_sai_neighbor_class
#define ops_sai_neighbor_class ops_sai_neighbor_class_generic
#endif

static inline void
ops_sai_neighbor_init(void)
{
    ovs_assert(ops_sai_neighbor_class()->init);
    ops_sai_neighbor_class()->init();
}

static inline int
ops_sai_neighbor_create(bool           is_ipv6_addr,
                        const char     *ip_addr,
                        const char     *mac_addr,
                        const handle_t *rifid)
{
    ovs_assert(ops_sai_neighbor_class()->create);
    return ops_sai_neighbor_class()->create(is_ipv6_addr,
                                            ip_addr,
                                            mac_addr,
                                            rifid);
}

static inline int
ops_sai_neighbor_remove(bool is_ipv6_addr, const char *ip_addr,
                        const handle_t *rifid)
{
    ovs_assert(ops_sai_neighbor_class()->remove);
    return ops_sai_neighbor_class()->remove(is_ipv6_addr, ip_addr, rifid);
}

static inline int
ops_sai_neighbor_activity_get(bool           is_ipv6_addr,
                              const char     *ip_addr,
                              const handle_t *rifid,
                              bool           *activity)
{
    ovs_assert(ops_sai_neighbor_class()->activity_get);
    return ops_sai_neighbor_class()->activity_get(is_ipv6_addr,
                                                  ip_addr,
                                                  rifid,
                                                  activity);
}

static inline void
ops_sai_neighbor_deinit(void)
{
    ovs_assert(ops_sai_neighbor_class()->deinit);
    ops_sai_neighbor_class()->deinit();
}

#endif /* sai-neighbor.h */
