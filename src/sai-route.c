/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#include <sai-log.h>
#include <sai-route.h>

VLOG_DEFINE_THIS_MODULE(sai_route);

/*
 * Initializes route.
 */
static void
__route_init(void)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
}

/*
 *  Function for adding IP to me route.
 *  Means tell hardware to trap packets with specified prefix to CPU.
 *  Used while assigning IP address(s) to routing interface.
 *
 * @param[in] vrid    - virtual router ID
 * @param[in] prefix  - IP prefix
 *
 * @return 0  if operation completed successfully.
 * @return -1 if operation failed.*/
static int
__route_ip_to_me_add(const handle_t *vrid, const char *prefix)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
    return 0;
}

/*
 *  Function for adding local route.
 *  Means while creating new routing interface and assigning IP address to it
 *  'tells' the hardware that new subnet is accessible through specified
 *  router interface.
 *
 * @param[in] vrid    - virtual router ID
 * @param[in] prefix  - IP prefix
 * @param[in] rifid   - router interface ID
 *
 * @return 0  if operation completed successfully.
 * @return -1 if operation failed.*/
static int
__route_local_add(const handle_t *vrid,
                  const char     *prefix,
                  const handle_t *rifid)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
    return 0;
}

/*
 *  Function for adding next hops(list of remote routes) which are accessible
 *  over specified IP prefix
 *
 * @param[in] vrid           - virtual router ID
 * @param[in] prefix         - IP prefix
 * @param[in] next_hop_count - count of next hops
 * @param[in] next_hops      - list of next hops
 *
 * @return 0  if operation completed successfully.
 * @return -1 if operation failed.*/
static int
__route_remote_add(handle_t           vrid,
                   const char        *prefix,
                   uint32_t           next_hop_count,
                   char *const *const next_hops)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
    return 0;
}

/*
 *  Function for deleting next hops(list of remote routes) which now are not
 *  accessible over specified IP prefix
 *
 * @param[in] vrid           - virtual router ID
 * @param[in] prefix         - IP prefix
 * @param[in] next_hop_count - count of next hops
 * @param[in] next_hops      - list of next hops
 *
 * @return 0  if operation completed successfully.
 * @return -1 if operation failed.*/
static int
__route_remote_nh_remove(handle_t           vrid,
                         const char        *prefix,
                         uint32_t           next_hop_count,
                         char *const *const next_hops)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
    return 0;
}

/*
 *  Function for deleting remote route
 *
 * @param[in] vrid           - virtual router ID
 * @param[in] prefix         - IP prefix over which next hops can be accessed
 *
 * @notes if next hops were already configured earlier for this route then
 *        delete all next-hops of a route as well as the route itself.
 *
 * @return 0  if operation completed successfully.
 * @return -1 if operation failed.*/
static int
__route_remove(const handle_t *vrid, const char     *prefix)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
    return 0;
}

/*
 * De-initializes route.
 */
static void
__route_deinit(void)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
}

DEFINE_GENERIC_CLASS(struct route_class, route) = {
    .init = __route_init,
    .ip_to_me_add = __route_ip_to_me_add,
    .local_add = __route_local_add,
    .remote_add = __route_remote_add,
    .remote_nh_remove = __route_remote_nh_remove,
    .remove = __route_remove,
    .deinit = __route_deinit,
};

DEFINE_GENERIC_CLASS_GETTER(struct route_class, route);
