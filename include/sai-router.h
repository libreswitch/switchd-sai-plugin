/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#ifndef SAI_ROUTER_H
#define SAI_ROUTER_H 1

#include <sai-common.h>
#ifdef SAI_VENDOR
#include <sai-vendor-common.h>
#endif /* SAI_VENDOR */

struct router_class {
    /**
     * Initializes virtual router.
     */
    void (*init)(void);
    /**
     * Creates virtual router.
     *
     * @param[out] vrid - Virtual router id.
     *
     * @return 0     operation completed successfully
     * @return errno operation failed
     */
    int  (*create)(handle_t *vrid);
    /**
     * Removes virtual router.
     *
     * @param[in] vrid - Virtual router id.
     *
     * @return 0     operation completed successfully
     * @return errno operation failed
     */
    int  (*remove)(const handle_t *vrid);
    /**
     * De-initializes virtual router.
     */
    void (*deinit)(void);
};

DECLARE_GENERIC_CLASS_GETTER(struct router_class, router);

#define ops_sai_router_class_generic() (CLASS_GENERIC_GETTER(router)())

#ifndef ops_sai_router_class
#define ops_sai_router_class ops_sai_router_class_generic
#endif

static inline void
ops_sai_router_init(void)
{
    ovs_assert(ops_sai_router_class()->init);
    ops_sai_router_class()->init();
}

static inline int
ops_sai_router_create(handle_t *handle)
{
    ovs_assert(ops_sai_router_class()->create);
    return ops_sai_router_class()->create(handle);
}

static inline int
ops_sai_router_remove(const handle_t *handle)
{
    ovs_assert(ops_sai_router_class()->remove);
    return ops_sai_router_class()->remove(handle);
}

static inline void
ops_sai_router_deinit(void)
{
    ovs_assert(ops_sai_router_class()->deinit);
    ops_sai_router_class()->deinit();
}

#endif /* SAI_ROUTER_H */
