/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#include <sai-log.h>
#include <mlnx_sai.h>
#include <util.h>
#include <config-yaml.h>
#include <i2c.h>

#define HW_DESC_DIR "/etc/openswitch/hwdesc"
#define BASE_SUBSYSTEM "base"
#define FRU_EEPROM_NAME "fru_eeprom"
#define FRU_BASE_MAC_ADDRESS_TYPE 0x24
#define FRU_BASE_MAC_ADDRESS_LEN 6

VLOG_DEFINE_THIS_MODULE(sai_vendor_mlnx);

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
