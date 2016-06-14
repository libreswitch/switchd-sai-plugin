/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#include <mlnx_sai.h>
#include <sai-vendor-util.h>
#include <sai-log.h>

#include <packets.h>
#include <socket-util.h>
#include <netinet/in.h>

#include <util.h>
#include <config-yaml.h>
#include <i2c.h>

#define HW_DESC_DIR "/etc/openswitch/hwdesc"
#define BASE_SUBSYSTEM "base"
#define FRU_EEPROM_NAME "fru_eeprom"
#define FRU_BASE_MAC_ADDRESS_TYPE 0x24
#define FRU_BASE_MAC_ADDRESS_LEN 6

VLOG_DEFINE_THIS_MODULE(mlnx_sai_util);

struct fru_header {
    char id[8];
    uint8_t header_version;
    uint8_t total_length[2];
};

struct fru_tlv {
    uint8_t code;
    uint8_t length;
    uint8_t value[255];
};

static sai_status_t __cfg_yaml_fru_read(uint8_t *, int, const YamlDevice *,
                                        const YamlConfigHandle);
static sai_status_t __eeprom_mac_get(const uint8_t *, sai_mac_t, int);

/*
 * Converts string representation of IP prefix into SX SDK format.
 *
 * @param[in] prefix     - IPv4/IPv6 prefix in string representation.
 * @param[out] sx_prefix - IPv4/IPv6 prefix in format used by SX SDK.
 *
 * @return 0 operation completed successfully
 * @return errno operation failed
 */
int ops_sai_common_ip_prefix_to_sx_ip_prefix(const char *prefix,
                                             sx_ip_prefix_t *sx_prefix)
{
    int                i = 0;
    int                error = 0;
    uint32_t           *addr_chunk = NULL;
    uint32_t           *mask_chunk = NULL;
    char               *error_msg = NULL;

    memset(sx_prefix, 0, sizeof(*sx_prefix));

    if (addr_is_ipv6(prefix)) {
        sx_prefix->version = SX_IP_VERSION_IPV6;
        error_msg = ipv6_parse_masked(prefix,
                                      (struct in6_addr*)
                                      &sx_prefix->prefix.ipv6.addr,
                                      (struct in6_addr*)
                                      &sx_prefix->prefix.ipv6.mask);
        if (!error_msg) {
            /* SDK IPv6 is 4*uint32. Each uint32 is in host order.
             * Between uint32s there is network byte order */
            addr_chunk = sx_prefix->prefix.ipv6.addr.s6_addr32;
            mask_chunk = sx_prefix->prefix.ipv6.mask.s6_addr32;

            for (i = 0; i < 4; ++i) {
                addr_chunk[i] = ntohl(addr_chunk[i]);
                mask_chunk[i] = ntohl(mask_chunk[i]);
            }
        }
    } else {
        sx_prefix->version = SX_IP_VERSION_IPV4;
        error_msg = ip_parse_masked(prefix,
                                    (ovs_be32*)
                                    &sx_prefix->prefix.ipv4.addr.s_addr,
                                    (ovs_be32*)
                                    &sx_prefix->prefix.ipv4.mask.s_addr);
        if (!error_msg) {
            /* SDK IPv4 is in host order*/
            sx_prefix->prefix.ipv4.addr.s_addr =
                    ntohl(sx_prefix->prefix.ipv4.addr.s_addr);
            sx_prefix->prefix.ipv4.mask.s_addr =
                    ntohl(sx_prefix->prefix.ipv4.mask.s_addr);
        }
    }

    if (NULL != error_msg) {
        error = -1;
        ERRNO_LOG_EXIT(error, "%s", error_msg);
    }

exit:
    if (error_msg) {
        free(error_msg);
    }

    return error;
}

/*
 * Converts string representation of IP address into SX SDK format.
 *
 * @param[in] ip     - IPv4/IPv6 address in string representation.
 * @param[out] sx_ip - IPv4/IPv6 address in format used by SX SDK.
 *
 * @return 0 operation completed successfully
 * @return errno operation failed
 */
int ops_sai_common_ip_to_sx_ip(const char *ip, sx_ip_addr_t *sx_ip)
{
    int                i = 0;
    int                error = 0;
    uint8_t            family = AF_INET;
    uint32_t           *addr_chunk = NULL;

    if (addr_is_ipv6(ip)){
        family = AF_INET6;
        sx_ip->version = SX_IP_VERSION_IPV6;
    } else {
        sx_ip->version = SX_IP_VERSION_IPV4;
    }

    /* inet_pton return 1 on success */
    if (1 != inet_pton(family, ip, (void*)&sx_ip->addr)) {
        error = -1;
        ERRNO_LOG_EXIT(error, "Invalid IP address: %s", ip);
    }

    if (sx_ip->version == SX_IP_VERSION_IPV6) {
        /* SDK IPv6 is 4*uint32. Each uint32 is in host order.
         * Between uint32s there is network byte order */
        addr_chunk = sx_ip->addr.ipv6.s6_addr32;

        for (i = 0; i < 4; ++i) {
            addr_chunk[i] = ntohl(addr_chunk[i]);
        }
    } else {
        /* SDK IPv4 is in host order*/
        sx_ip->addr.ipv4.s_addr = ntohl(sx_ip->addr.ipv4.s_addr);
    }

exit:
    return error;
}

/**
 * Read base MAC address from EEPROM.
 * @param[out] mac pointer to MAC buffer.
 * @return sai_status_t.
 */
sai_status_t
ops_sai_vendor_base_mac_get(sai_mac_t mac)
{
    int len = 0;
    struct fru_header header = { };
    uint8_t *buf = NULL;
    uint16_t total_len = 0;
    const YamlDevice *fru_dev = NULL;
    sai_status_t status = SAI_STATUS_SUCCESS;
    YamlConfigHandle cfg_yaml_handle = yaml_new_config_handle();

    NULL_PARAM_LOG_ABORT(mac);

    if (NULL == cfg_yaml_handle) {
        status = SAI_STATUS_FAILURE;
    }
    SAI_ERROR_LOG_EXIT(status, "Failed to get config yaml handle");

    status = yaml_add_subsystem(cfg_yaml_handle, BASE_SUBSYSTEM, HW_DESC_DIR)
           ? SAI_STATUS_FAILURE
           : SAI_STATUS_SUCCESS;
    SAI_ERROR_LOG_EXIT(status, "Failed to add yaml base subsystem");

    status = yaml_parse_devices(cfg_yaml_handle, BASE_SUBSYSTEM)
           ? SAI_STATUS_FAILURE
           : SAI_STATUS_SUCCESS;
    SAI_ERROR_LOG_EXIT(status, "Failed to parse devices");

    fru_dev = yaml_find_device(cfg_yaml_handle, BASE_SUBSYSTEM,
                               FRU_EEPROM_NAME);

    if (NULL == fru_dev) {
        status = SAI_STATUS_FAILURE;
    }
    SAI_ERROR_LOG_EXIT(status, "Failed to get fru_eeprom device");

    /* Read header info */
    status = __cfg_yaml_fru_read((unsigned char *) &header, sizeof(header),
                                   fru_dev, cfg_yaml_handle);
    SAI_ERROR_LOG_EXIT(status, "Failed to read FRU EEPROM Header");

    total_len = (header.total_length[0] << 8) | (header.total_length[1]);

    /* Using length from header, read remainder of FRU EEPROM */
    len = total_len + sizeof(struct fru_header) + 1;
    buf = (uint8_t *) xzalloc(len);

    status = __cfg_yaml_fru_read(buf, len, fru_dev, cfg_yaml_handle);
    SAI_ERROR_LOG_EXIT(status, "Failed to read FRU EEPROM");

    status = __eeprom_mac_get(buf, mac, total_len);
    SAI_ERROR_LOG_EXIT(status, "Failed to process FRU EEPROM info");

    mac[5] &= PORT_MAC_BITMASK;

exit:
    if (NULL != buf) {
        free(buf);
    }
    return status;
}

/*
 * Read FRU data from EEPROM.
 */
static sai_status_t
__cfg_yaml_fru_read(uint8_t *fru, int hdr_len, const YamlDevice *fru_dev,
                    const YamlConfigHandle cfg_yaml_handle)
{
    i2c_op op = {};
    i2c_op *cmds[2] = { };
    sai_status_t status = SAI_STATUS_SUCCESS;

    NULL_PARAM_LOG_ABORT(fru);
    NULL_PARAM_LOG_ABORT(fru_dev);

    op.direction = READ;
    op.device = fru_dev->name;
    op.register_address = 0;
    op.byte_count = hdr_len;
    op.data = fru;
    op.set_register = false;
    op.negative_polarity = false;

    cmds[0] = &op;
    cmds[1] = (i2c_op *) NULL;

    status = i2c_execute(cfg_yaml_handle, BASE_SUBSYSTEM, fru_dev, cmds)
             ? SAI_STATUS_FAILURE
             : SAI_STATUS_SUCCESS;
    SAI_ERROR_LOG_EXIT(status, "Failed to read FRU EEPROM");

exit:
    return status;
}

/*
 * Reads mac from tlv values in FRU buffer.
 */
static sai_status_t
__eeprom_mac_get(const uint8_t *buffer, sai_mac_t mac, int len)
{
    struct fru_tlv *tlv = NULL;
    int idx = 0, skip = 0;
    const uint8_t *buf = buffer;
    sai_status_t status = SAI_STATUS_FAILURE;

    NULL_PARAM_LOG_ABORT(buffer);

    /* Skip the FRU header. */
    buf += sizeof(struct fru_header);

    while (idx < len) {
        tlv = (struct fru_tlv *) buf;

        if (FRU_BASE_MAC_ADDRESS_TYPE == tlv->code) {
            memcpy(mac, tlv->value, FRU_BASE_MAC_ADDRESS_LEN);
            status = SAI_STATUS_SUCCESS;
            break;
        }

        skip = tlv->length + sizeof(tlv->length)
             + sizeof(tlv->code);
        idx += skip;
        buf += skip;
    }

    SAI_ERROR_LOG_EXIT(status, "MAC address not found in FRU EEPROM");

exit:
    return status;
}
