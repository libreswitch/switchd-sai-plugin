/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#ifndef SAI_HASH_H
#define SAI_HASH_H 1

#include <sai-common.h>
#include <sai-vendor-common.h>

struct hash_class {
    /**
     * Initialize hashing. Set default hash fields.
     */
    void (*init)(void);
    /**
     * Set hash fields.
     *
     * @param[in] hash   - bitmap of hash fields.
     * @param[in] enable - specifies if hash fields should be enabled or disabled.
     *
     * @return 0 operation completed successfully
     * @return errno operation failed
     */
    int  (*ecmp_hash_set)(uint64_t hash, bool enable);
    /**
     * De-initialize hashing.
     */
    void (*deinit)(void);
};

DECLARE_GENERIC_CLASS_GETTER(struct hash_class, hash);

#define ops_sai_hash_class_generic() (CLASS_GENERIC_GETTER(hash)())

#ifndef ops_sai_hash_class
#define ops_sai_hash_class ops_sai_hash_class_generic
#endif

static inline void
ops_sai_ecmp_hash_init(void)
{
    ovs_assert(ops_sai_hash_class()->init);
    return ops_sai_hash_class()->init();
}

static inline int
ops_sai_ecmp_hash_set(uint64_t fields_to_set, bool enable)
{
    ovs_assert(ops_sai_hash_class()->ecmp_hash_set);
    return ops_sai_hash_class()->ecmp_hash_set(fields_to_set, enable);
}

static inline void
ops_sai_ecmp_hash_deinit(void)
{
    ovs_assert(ops_sai_hash_class()->deinit);
    return ops_sai_hash_class()->deinit();
}

#endif /* SAI_HASH_H */
