/*
 * * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#include <ofproto/ofproto-provider.h>

#include <sai-log.h>
#include <sai-common.h>
#include <sai-hash.h>
#include <sai-api-class.h>

/* should be included last due to defines conflicts */
#include <sai-vendor-util.h>

#define HASH_LIST_SIZE (4)
#define SUPPORTED_HASH_FIELDS    \
    (OFPROTO_ECMP_HASH_SRCIP |   \
     OFPROTO_ECMP_HASH_DSTIP |   \
     OFPROTO_ECMP_HASH_SRCPORT | \
     OFPROTO_ECMP_HASH_DSTPORT)

VLOG_DEFINE_THIS_MODULE(mlnx_sai_hash);

static uint64_t ghash_fields = 0;

/*
 * Convert OPS hash fields to SX SDK.
 *
 * @param[in] ops_hash      - bitmap with OPS hash fields.
 * @param[out] fields_list  - list of SX SDK hash fields.
 * @param[out] fields_cnt   - count of SX SDK hash fields.
 * @param[out] enables_list - array of enables to be included in the hash calculation
 * @param[out] enables_cnt  - count of enables
 */
static void
__ops_hash_to_sxsdk(uint64_t                            ops_hash,
                    sx_router_ecmp_hash_field_t        *fields_list,
                    int                                *fields_cnt,
                    sx_router_ecmp_hash_field_enable_t *enables_list,
                    int                                *enables_cnt)
{
    bool ip_enable = false;
    bool l4_enable = false;

    *fields_cnt = 0;
    *enables_cnt = 0;

    if (ops_hash & OFPROTO_ECMP_HASH_SRCIP) {
        ip_enable = true;

        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_OUTER_IPV4_SIP_BYTE_0;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_OUTER_IPV4_SIP_BYTE_1;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_OUTER_IPV4_SIP_BYTE_2;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_OUTER_IPV4_SIP_BYTE_3;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_OUTER_IPV6_SIP_BYTES_0_TO_7;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_OUTER_IPV6_SIP_BYTE_8;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_OUTER_IPV6_SIP_BYTE_9;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_OUTER_IPV6_SIP_BYTE_10;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_OUTER_IPV6_SIP_BYTE_11;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_OUTER_IPV6_SIP_BYTE_12;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_OUTER_IPV6_SIP_BYTE_13;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_OUTER_IPV6_SIP_BYTE_14;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_OUTER_IPV6_SIP_BYTE_15;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_INNER_IPV4_SIP_BYTE_0;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_INNER_IPV4_SIP_BYTE_1;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_INNER_IPV4_SIP_BYTE_2;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_INNER_IPV4_SIP_BYTE_3;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_INNER_IPV6_SIP_BYTES_0_TO_7;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_INNER_IPV6_SIP_BYTE_8;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_INNER_IPV6_SIP_BYTE_9;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_INNER_IPV6_SIP_BYTE_10;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_INNER_IPV6_SIP_BYTE_11;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_INNER_IPV6_SIP_BYTE_12;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_INNER_IPV6_SIP_BYTE_13;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_INNER_IPV6_SIP_BYTE_14;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_INNER_IPV6_SIP_BYTE_15;
    }

    if (ops_hash & OFPROTO_ECMP_HASH_DSTIP) {
        ip_enable = true;

        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_OUTER_IPV4_DIP_BYTE_0;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_OUTER_IPV4_DIP_BYTE_1;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_OUTER_IPV4_DIP_BYTE_2;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_OUTER_IPV4_DIP_BYTE_3;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_OUTER_IPV6_DIP_BYTES_0_TO_7;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_OUTER_IPV6_DIP_BYTE_8;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_OUTER_IPV6_DIP_BYTE_9;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_OUTER_IPV6_DIP_BYTE_10;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_OUTER_IPV6_DIP_BYTE_11;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_OUTER_IPV6_DIP_BYTE_12;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_OUTER_IPV6_DIP_BYTE_13;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_OUTER_IPV6_DIP_BYTE_14;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_OUTER_IPV6_DIP_BYTE_15;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_INNER_IPV4_DIP_BYTE_0;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_INNER_IPV4_DIP_BYTE_1;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_INNER_IPV4_DIP_BYTE_2;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_INNER_IPV4_DIP_BYTE_3;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_INNER_IPV6_DIP_BYTES_0_TO_7;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_INNER_IPV6_DIP_BYTE_8;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_INNER_IPV6_DIP_BYTE_9;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_INNER_IPV6_DIP_BYTE_10;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_INNER_IPV6_DIP_BYTE_11;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_INNER_IPV6_DIP_BYTE_12;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_INNER_IPV6_DIP_BYTE_13;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_INNER_IPV6_DIP_BYTE_14;
        fields_list[(*fields_cnt)++] =
            SX_ROUTER_ECMP_HASH_INNER_IPV6_DIP_BYTE_15;
    }

    if (ops_hash & OFPROTO_ECMP_HASH_SRCPORT) {
        l4_enable = true;

        fields_list[(*fields_cnt)++] = SX_ROUTER_ECMP_HASH_INNER_TCP_UDP_SPORT;
        fields_list[(*fields_cnt)++] = SX_ROUTER_ECMP_HASH_OUTER_TCP_UDP_SPORT;
    }

    if (ops_hash & OFPROTO_ECMP_HASH_DSTPORT) {
        l4_enable = true;

        fields_list[(*fields_cnt)++] = SX_ROUTER_ECMP_HASH_INNER_TCP_UDP_SPORT;
        fields_list[(*fields_cnt)++] = SX_ROUTER_ECMP_HASH_OUTER_TCP_UDP_SPORT;
    }

    if (ip_enable) {
        enables_list[(*enables_cnt)++] =
            SX_ROUTER_ECMP_HASH_FIELD_ENABLE_OUTER_IPV4_NON_TCP_UDP;
        enables_list[(*enables_cnt)++] =
            SX_ROUTER_ECMP_HASH_FIELD_ENABLE_OUTER_IPV4_TCP_UDP;
        enables_list[(*enables_cnt)++] =
            SX_ROUTER_ECMP_HASH_FIELD_ENABLE_INNER_IPV4_NON_TCP_UDP;
        enables_list[(*enables_cnt)++] =
            SX_ROUTER_ECMP_HASH_FIELD_ENABLE_INNER_IPV4_TCP_UDP;
        enables_list[(*enables_cnt)++] =
            SX_ROUTER_ECMP_HASH_FIELD_ENABLE_OUTER_IPV6_NON_TCP_UDP;
        enables_list[(*enables_cnt)++] =
            SX_ROUTER_ECMP_HASH_FIELD_ENABLE_OUTER_IPV6_TCP_UDP;
        enables_list[(*enables_cnt)++] =
            SX_ROUTER_ECMP_HASH_FIELD_ENABLE_INNER_IPV6_NON_TCP_UDP;
        enables_list[(*enables_cnt)++] =
            SX_ROUTER_ECMP_HASH_FIELD_ENABLE_INNER_IPV6_TCP_UDP;
    }

    if (l4_enable) {
        enables_list[(*enables_cnt)++] =
            SX_ROUTER_ECMP_HASH_FIELD_ENABLE_INNER_L4_IPV4;
        enables_list[(*enables_cnt)++] =
            SX_ROUTER_ECMP_HASH_FIELD_ENABLE_INNER_L4_IPV6;
        enables_list[(*enables_cnt)++] =
            SX_ROUTER_ECMP_HASH_FIELD_ENABLE_OUTER_L4_IPV4;
        enables_list[(*enables_cnt)++] =
            SX_ROUTER_ECMP_HASH_FIELD_ENABLE_OUTER_L4_IPV6;
    }
}

/*
 * Initialize hashing. Set default hash fields.
 *
 * @return 0 operation completed successfully
 * @return errno operation failed
 */
void
__ecmp_hash_init(void)
{
    /* In OPS by default all supported fields are enabled */
    int status = 0;

    status = ops_sai_ecmp_hash_set(SUPPORTED_HASH_FIELDS, true);
    ERRNO_LOG_ABORT(status, "Failed to initialize ECMP hashing");
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
int
__ecmp_hash_set(uint64_t fields_to_set, bool enable)
{
    sx_status_t                        status = SX_STATUS_SUCCESS;
    uint64_t                           new_hash_value = ghash_fields;
    int                                fields_cnt = 0;
    sx_router_ecmp_hash_field_t        fields_list[FIELDS_NUM];
    int                                enables_cnt = 0;
    sx_router_ecmp_hash_field_enable_t enables_list[FIELDS_ENABLES_NUM];
    sx_port_id_t                      *port_list = NULL;
    length_t                           port_cnt = 0;
    int                                port_idx = 0;
    sx_port_type_t                     port_type = 0;
    sx_router_ecmp_port_hash_params_t  hash_params = { };

    if ((UINT64_MAX ^ SUPPORTED_HASH_FIELDS) & fields_to_set) {
        VLOG_WARN("Hash fields validation failed. "
                  "Unsupported hash field(s) will be ignored (has_fields: 0x%lx)",
                  fields_to_set ^ SUPPORTED_HASH_FIELDS);
    }

    VLOG_INFO("%s ECMP hash fields (hash_fields:%s%s%s%s)",
              enable ? "Enabling" : "Disabling",
              fields_to_set & OFPROTO_ECMP_HASH_SRCIP ? " SRC-IP" : "",
              fields_to_set & OFPROTO_ECMP_HASH_DSTIP ? " DST-IP" : "",
              fields_to_set & OFPROTO_ECMP_HASH_SRCPORT ? " SRC-PORT" : "",
              fields_to_set & OFPROTO_ECMP_HASH_DSTPORT ? " DST-PORT" : "");

    if (enable) {
        /* Turn on bits */
        new_hash_value |= fields_to_set;
    } else {
        /* Turn off bits */
        new_hash_value &= (UINT64_MAX ^ fields_to_set);
    }

    VLOG_INFO("Setting ECMP hash fields (hash_fields:%s%s%s%s)",
              new_hash_value & OFPROTO_ECMP_HASH_SRCIP ? " SRC-IP" : "",
              new_hash_value & OFPROTO_ECMP_HASH_DSTIP ? " DST-IP" : "",
              new_hash_value & OFPROTO_ECMP_HASH_SRCPORT ? " SRC-PORT" : "",
              new_hash_value & OFPROTO_ECMP_HASH_DSTPORT ? " DST-PORT" : "");

    __ops_hash_to_sxsdk(new_hash_value,
                        fields_list, &fields_cnt,
                        enables_list, &enables_cnt);

    if (!fields_cnt) {
        status = SX_STATUS_PARAM_ERROR;
        SX_ERROR_LOG_EXIT(status,
                          "Failed to convert hash fields into SX SDK representation. Hash "
                          "fields list could not be empty");
    }

    hash_params.ecmp_hash_type = SX_ROUTER_ECMP_HASH_TYPE_CRC;

    status = sx_api_port_swid_port_list_get(gh_sdk,
                                            DEFAULT_ETH_SWID,
                                            NULL,
                                            &port_cnt);
    SX_ERROR_LOG_EXIT(status,
                      "Failed to retrieve number of ports (error: %s)",
                      SX_STATUS_MSG(status));

    port_list = xzalloc(sizeof(*port_list) * port_cnt);

    status = sx_api_port_swid_port_list_get(gh_sdk,
                                            DEFAULT_ETH_SWID,
                                            port_list,
                                            &port_cnt);
    SX_ERROR_LOG_EXIT(status,
                      "Failed to retrieve port list (error: %s)",
                      SX_STATUS_MSG(status));

    for (port_idx = 0; port_idx < port_cnt; port_idx++) {
        port_type = SX_PORT_TYPE_ID_GET(port_list[port_idx]);
        if ((port_type != SX_PORT_TYPE_LAG) &&
            (port_type != SX_PORT_TYPE_NETWORK)) {
            continue;
        }

        status = sx_api_router_ecmp_port_hash_params_set(gh_sdk,
                                                         SX_ACCESS_CMD_SET,
                                                         port_list[port_idx],
                                                         &hash_params,
                                                         enables_list,
                                                         enables_cnt,
                                                         fields_list,
                                                         fields_cnt);
        SX_ERROR_LOG_EXIT(status,
                          "Failed to set ECMP hash (port_log_id: %u, error: %s)",
                          port_list[port_idx],
                          SX_STATUS_MSG(status));
    }

exit:
    /* Update global hash fields bit map.
     * This value is set even in case of error to
     * be equal what user want to set via OPS */
    ghash_fields = new_hash_value;

    if (port_list) {
        free(port_list);
    }

    return SX_ERROR_2_ERRNO(status);
}

/*
 * De-initialize hashing.
 *
 * @return 0 operation completed successfully
 * @return errno operation failed
 */
void
__ecmp_hash_deinit(void)
{
    /* In OPS by default all supported fields are enabled */
    int status = 0;

    status = ops_sai_ecmp_hash_set(SUPPORTED_HASH_FIELDS, true);
    ERRNO_LOG_ABORT(status, "Failed to initialize ECMP hashing");
}

DEFINE_VENDOR_CLASS(struct hash_class, hash) = {
    .init = __ecmp_hash_init,
    .ecmp_hash_set = __ecmp_hash_set,
    .deinit = __ecmp_hash_deinit
};

DEFINE_VENDOR_CLASS_GETTER(struct hash_class, hash);
