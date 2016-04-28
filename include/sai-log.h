/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.  
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#ifndef SAI_LOG_H
#define SAI_LOG_H 1

#include <sai.h>
#include <errno.h>
#include <openvswitch/vlog.h>

#define SAI_ERROR_2_ERRNO(status) sai_error_2_errno(status)

/* Requires "exit" label to be defined. */
#define SAI_ERROR_LOG_EXIT(status, msg, args...) \
    do { \
        if (SAI_ERROR_2_ERRNO(status)) { \
            VLOG_ERR("SAI error %d " msg, status, ##args); \
            goto exit; \
        } \
    } while (0);

/* Requires "exit" label to be defined. */
#define SAI_ERROR_EXIT(status) \
    do { \
        if (SAI_ERROR_2_ERRNO(status)) { \
            goto exit; \
        } \
    } while (0);

#define SAI_ERROR_LOG_ABORT(status, msg, args...) \
    do { \
        if (SAI_ERROR_2_ERRNO(status)) { \
            VLOG_ERR("SAI error %d " msg, status, ##args); \
            ovs_assert(false); \
        } \
    } while (0);

/* Requires "exit" label to be defined. */
#define ERRNO_LOG_EXIT(status, msg, args...) \
    do { \
        if (status) { \
            VLOG_ERR("error %d " msg, status, ##args); \
            goto exit; \
        } \
    } while (0);

/* Requires "exit" label to be defined. */
#define ERRNO_EXIT(status) \
    do { \
        if (status) { \
            goto exit; \
        } \
    } while (0);

#define NULL_PARAM_LOG_ABORT(param) \
    do { \
        if (NULL == (param)) { \
            VLOG_ERR("Got null param " #param ". Aborting."); \
            ovs_assert(false); \
        } \
    } while (0);

#define SAI_API_TRACE_FN() VLOG_DBG("Entering %s function", __FUNCTION__)
#define SAI_API_TRACE_EXIT_FN() VLOG_DBG("Exiting %s function", __FUNCTION__)
#define SAI_API_TRACE_NOT_IMPLEMENTED_FN() \
    VLOG_DBG("Function %s is not yet implemented", __FUNCTION__)

static inline int
sai_error_2_errno(sai_status_t status)
{
    switch (status) {
        case SAI_STATUS_SUCCESS: return 0;
        case SAI_STATUS_INVALID_PARAMETER: return EINVAL;
        case SAI_STATUS_NO_MEMORY: return ENOMEM;
        case SAI_STATUS_BUFFER_OVERFLOW: return EOVERFLOW;
        default: return -1;
    }

    return 0;
}

#endif /* sai-log.h */
