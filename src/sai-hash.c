/*
 * * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#include <sai-log.h>
#include <sai-common.h>
#include <sai-hash.h>
#include <sai-api-class.h>

VLOG_DEFINE_THIS_MODULE(sai_hash);

/*
 * Initialize hashing. Set default hash fields.
 *
 * @return 0 operation completed successfully
 * @return errno operation failed
 */
static void
__ecmp_hash_init(void)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
}

/*
 * Set hash fields.
 *
 * @param[in] fields_to_set - bitmap of hash fields.
 * @param[in] enable        - specifies if hash fields should be enabled or disabled.
 *
 * @return 0 operation completed successfully
 * @return errno operation failed
 */
static int
__ecmp_hash_set(uint64_t fields_to_set, bool enable)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
    return 0;
}

/*
 * De-initialize hashing.
 *
 * @return 0 operation completed successfully
 * @return errno operation failed
 */
static void
__ecmp_hash_deinit(void)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
}

DEFINE_GENERIC_CLASS(struct hash_class, hash) = {
    .init = __ecmp_hash_init,
    .ecmp_hash_set = __ecmp_hash_set,
    .deinit = __ecmp_hash_deinit
};

DEFINE_GENERIC_CLASS_GETTER(struct hash_class, hash);
