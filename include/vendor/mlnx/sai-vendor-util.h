/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#ifndef SAI_VENDOR_UTIL_H
#define SAI_VENDOR_UTIL_H 1

#include <mlnx_sai.h>
#include <openvswitch/vlog.h>

/*
 * Logging macros
 */
#define SX_ERROR_2_ERRNO(status) ((status) == SX_STATUS_SUCCESS ? 0 : -1)

/* Requires "exit" label to be defined. */
#define SX_ERROR_LOG_EXIT(status, msg, args...) \
do { \
    if (SX_ERROR_2_ERRNO(status)) { \
        VLOG_ERR("error %d " msg, status, ##args); \
        goto exit; \
    } \
} while (0);

#define SX_ERROR_LOG_ABORT(status, msg, args...) \
do { \
    if (SX_ERROR_2_ERRNO(status)) { \
        VLOG_ERR("error %d " msg, status, ##args); \
        ovs_assert(false); \
    } \
} while (0);

/* Requires "exit" label to be defined. */
#define SX_ERROR_EXIT(status) \
do { \
    if (SX_ERROR_2_ERRNO(status)) { \
        goto exit; \
    } \
} while (0);

/*
 *Function declaration
 */
int ops_sai_common_ip_prefix_to_sx_ip_prefix(const char *prefix,
                                             sx_ip_prefix_t *sx_prefix);
int ops_sai_common_ip_to_sx_ip(const char *ip, sx_ip_addr_t *sx_ip);

#endif /* sai-vendor-util.h */
