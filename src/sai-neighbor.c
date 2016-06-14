/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#include <sai-log.h>
#include <sai-neighbor.h>
#include <sai-common.h>

VLOG_DEFINE_THIS_MODULE(sai_neighbor);

/*
 * Initializes neighbor.
 */
static void
__neighbor_init(void)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
}

/*
 *  This function adds a neighbour information.
 *
 * @param[in] is_ipv6_addr - is address IPv6 or not
 * @param[in] ip_addr      - neighbor IP address
 * @param[in] mac_addr     - neighbor MAC address
 * @param[in] rif          - router Interface ID
 *
 * @return 0  if operation completed successfully.
 * @return -1 if operation failed.*/
static int
__neighbor_create(bool           is_ipv6_addr,
                  const char     *ip_addr,
                  const char     *mac_addr,
                  const handle_t *rif)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
    return 0;
}

/*
 *  This function deletes a neighbour information.
 *
 * @param[in] is_ipv6_addr - is address IPv6 or not
 * @param[in] ip_addr      - neighbor IP address
 * @param[in] rif          - router Interface ID
 *
 * @return 0  if operation completed successfully.
 * @return -1 if operation failed.*/
static int
__neighbor_remove(bool is_ipv6_addr, const char *ip_addr, const handle_t *rif)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
    return 0;
}

/*
 *  This function reads the neighbor's activity information.
 *
 * @param[in] is_ipv6_addr - is address IPv6 or not
 * @param[in] ip_addr      - neighbor IP address
 * @param[in] rif          - router Interface ID
 * @param[out] activity_p  - activity
 *
 * @return 0  if operation completed successfully.
 * @return -1 if operation failed.*/
static int
__neighbor_activity_get(bool           is_ipv6_addr,
                        const char     *ip_addr,
                        const handle_t *rif,
                        bool           *activity)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
    return 0;
}

/*
 * De-initializes neighbor.
 */
static void
__neighbor_deinit(void)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
}

DEFINE_GENERIC_CLASS(struct neighbor_class, neighbor) = {
    .init = __neighbor_init,
    .create = __neighbor_create,
    .remove = __neighbor_remove,
    .activity_get = __neighbor_activity_get,
    .deinit = __neighbor_deinit
};

DEFINE_GENERIC_CLASS_GETTER(struct neighbor_class, neighbor);
