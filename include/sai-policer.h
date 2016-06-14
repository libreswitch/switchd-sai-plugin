/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#ifndef SAI_POLICER_H
#define SAI_POLICER_H 1

#include <sai-common.h>
#ifdef SAI_VENDOR
#include <sai-vendor-common.h>
#endif /* SAI_VENDOR */

struct ops_sai_policer_config {
    uint32_t burst_max;
    uint32_t rate_max;
};

struct policer_class {
    /**
    * Initialize policers.
    */
    void (*init)(void);
    /**
     * Create policer.
     *
     * @param[out] handle  - pointer to policer object. Will be set
     * to policer object id.
     * @param[in]  config  - pointer to policer configuration.
     *
     * @return 0 on success, sai status converted to errno value.
     */
    int (*create)(handle_t                            *handle,
                  const struct ops_sai_policer_config *config);
    /**
     * Destroy policer.
     *
     * param[in] handle - pointer to policer object.
     *
     * @return 0 on success, sai status converted to errno value.
     */
    int (*remove)(const handle_t *handle);
    /**
     * De-initialize policers.
     */
    void (*deinit)(void);
};

DECLARE_GENERIC_CLASS_GETTER(struct policer_class, policer);

#define ops_sai_policer_class_generic() (CLASS_GENERIC_GETTER(policer)())

#ifndef ops_sai_policer_class
#define ops_sai_policer_class           ops_sai_policer_class_generic
#endif

static inline void ops_sai_policer_init(void)
{
    ovs_assert(ops_sai_policer_class_generic()->init);
    ops_sai_policer_class_generic()->init();
}

static inline int ops_sai_policer_create(handle_t *handle,
                                         const struct ops_sai_policer_config *config)
{
    ovs_assert(ops_sai_policer_class_generic()->create);
    return ops_sai_policer_class_generic()->create(handle, config);
}

static inline int ops_sai_policer_remove(const handle_t *handle)
{
    ovs_assert(ops_sai_policer_class_generic()->remove);
    return ops_sai_policer_class_generic()->remove(handle);
}

static inline void ops_sai_policer_deinit(void)
{
    ovs_assert(ops_sai_policer_class_generic()->deinit);
    ops_sai_policer_class_generic()->deinit();
}

#endif /* sai-policer.h */
