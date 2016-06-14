/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#include <sai-log.h>
#include <sai-router.h>

VLOG_DEFINE_THIS_MODULE(sai_router);

/*
 * Initializes virtual router.
 */
static void
__router_init(void)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
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
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
    return 0;
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
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
    return 0;
}

/*
 * De-initializes virtual router.
 */
static void
__router_deinit(void)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
}

DEFINE_GENERIC_CLASS(struct router_class, router) = {
    .init = __router_init,
    .create = __router_create,
    .remove = __router_remove,
    .deinit = __router_deinit
};

DEFINE_GENERIC_CLASS_GETTER(struct router_class, router);
