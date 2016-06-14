/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#ifndef SAI_VLAN_H
#define SAI_VLAN_H 1

#include <sai.h>
#include <sai-common.h>
#include <sai-vendor-common.h>

struct vlan_class {
    /**
     * Initialize VLANs.
     */
    void (*init)(void);
    /**
     * Adds port to access vlan.
     *
     * @param[in] vid   - VLAN id.
     * @param[in] hw_id - port label id.
     *
     * @return 0, sai error converted to errno otherwise.
     */
    int (*access_port_add)(sai_vlan_id_t vid, uint32_t hw_id);
    /**
     * Removes port from access vlan and sets PVID to default.
     *
     * @param[in] vid   - VLAN id.
     * @param[in] hw_id - port label id.
     *
     * @return 0, sai error converted to errno otherwise.
     */
    int (*access_port_del)(sai_vlan_id_t vid, uint32_t hw_id);
    /**
     * Adds port to trunks.
     *
     * @param[in] trunks - vlan bitmap.
     * @param[in] hw_id  - port label id.
     *
     * @return 0, sai error converted to errno otherwise.
     */
    int (*trunks_port_add)(const unsigned long *trunks, uint32_t hw_id);
    /**
     * Removes port from trunks.
     *
     * @param[in] hw_id  - port label id.
     * @param[in] trunks - vlan bitmap.
     *
     * @return 0, sai error converted to errno otherwise.
     */
    int (*trunks_port_del)(const unsigned long *hw_id, uint32_t trunks);
    /**
     * Creates or destroys vlan.
     *
     * @param[in] vid - VLAN id.
     * @param[in] add - boolean which says if vlan should be added
     *       or removed.
     *
     * @return 0, sai error converted to errno otherwise.
     */
    int (*set)(int vid, bool add);
    /**
     * De-initialize VLANs.
     */
    void (*deinit)(void);
};

DECLARE_GENERIC_CLASS_GETTER(struct vlan_class, vlan);

#define ops_sai_vlan_class_generic() (CLASS_GENERIC_GETTER(vlan)())

#ifndef ops_sai_vlan_class
#define ops_sai_vlan_class           ops_sai_vlan_class_generic
#endif

static inline void ops_sai_vlan_init(void)
{
    ovs_assert(ops_sai_vlan_class()->init);
    ops_sai_vlan_class()->init();
}

static inline int ops_sai_vlan_access_port_add(sai_vlan_id_t vid,
                                               uint32_t hw_id)
{
    ovs_assert(ops_sai_vlan_class()->access_port_add);
    return ops_sai_vlan_class()->access_port_add(vid, hw_id);
}

static inline int ops_sai_vlan_access_port_del(sai_vlan_id_t vid,
                                               uint32_t hw_id)
{
    ovs_assert(ops_sai_vlan_class()->access_port_del);
    return ops_sai_vlan_class()->access_port_del(vid, hw_id);
}

static inline int ops_sai_vlan_trunks_port_add(const unsigned long * trunks,
                                               uint32_t hw_id)
{
    ovs_assert(ops_sai_vlan_class()->trunks_port_add);
    return ops_sai_vlan_class()->trunks_port_add(trunks, hw_id);
}

static inline int ops_sai_vlan_trunks_port_del(const unsigned long * trunks,
                                               uint32_t hw_id)
{
    ovs_assert(ops_sai_vlan_class()->trunks_port_del);
    return ops_sai_vlan_class()->trunks_port_del(trunks, hw_id);
}

static inline int ops_sai_vlan_set(int vid, bool add)
{
    ovs_assert(ops_sai_vlan_class()->set);
    return ops_sai_vlan_class()->set(vid, add);
}

static inline void ops_sai_vlan_deinit(void)
{
    ovs_assert(ops_sai_vlan_class()->deinit);
    ops_sai_vlan_class()->deinit();
}

#endif /* sai-vlan.h */
