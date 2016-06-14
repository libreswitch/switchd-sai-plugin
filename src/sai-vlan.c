/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#include <vlan-bitmap.h>
#include <ofproto/ofproto.h>

#include <sai-log.h>
#include <sai-api-class.h>
#include <sai-port.h>
#include <sai-vlan.h>

VLOG_DEFINE_THIS_MODULE(sai_vlan);

static int __vlan_port_set(sai_vlan_id_t, uint32_t, sai_vlan_tagging_mode_t,
                           bool);
static int __trunks_port_set(const unsigned long *, uint32_t, bool);

/*
 * Initialize VLANs.
 */
void
__vlan_init(void)
{
    VLOG_INFO("Initializing VLANs");
}

/*
 * De-initialize VLANs.
 */
void
__vlan_deinit(void)
{
    VLOG_INFO("De-initializing VLANs");
}

/*
 * Adds port to access vlan.
 * @param[in] vid VLAN id.
 * @param[in] hw_id port label id.
 * @return 0, sai error converted to errno otherwise.
 */
int
__vlan_access_port_add(sai_vlan_id_t vid, uint32_t hw_id)
{
    return __vlan_port_set(vid, hw_id, SAI_VLAN_PORT_UNTAGGED, true);
}

/*
 * Removes port from access vlan and sets PVID to default.
 * @param[in] vid VLAN id.
 * @param[in] hw_id port label id.
 * @return 0, sai error converted to errno otherwise.
 */
int
__vlan_access_port_del(sai_vlan_id_t vid, uint32_t hw_id)
{
    int status = 0;

    /* Mode doesn't matter when port is removed from vlan. */
    status = __vlan_port_set(vid, hw_id, SAI_VLAN_PORT_UNTAGGED, false);
    ERRNO_EXIT(status);

    status = ops_sai_port_pvid_set(hw_id, OPS_SAI_PORT_DEFAULT_PVID);
    ERRNO_EXIT(status);

exit:
    return status;
}

/*
 * Adds port to trunks.
 * @param[in] trunks vlan bitmap.
 * @param[in] hw_id port label id.
 * @return 0, sai error converted to errno otherwise.
 */
int
__vlan_trunks_port_add(const unsigned long *trunks, uint32_t hw_id)
{
    return __trunks_port_set(trunks, hw_id, true);
}

/*
 * Removes port from trunks.
 * @param[in] hw_id port label id.
 * @param[in] trunks vlan bitmap.
 * @return 0, sai error converted to errno otherwise.
 */
int
__vlan_trunks_port_del(const unsigned long *trunks, uint32_t hw_id)
{
    return __trunks_port_set(trunks, hw_id, false);
}

/*
 * Creates or destroys vlan.
 * @param[in] vid VLAN id.
 * @param[in] add boolean which says if vlan should be added or removed.
 * @return 0, sai error converted to errno otherwise.
 */
int
__vlan_set(int vid, bool add)
{
    sai_status_t status = SAI_STATUS_SUCCESS;
    const struct ops_sai_api_class *sai_api = ops_sai_api_get_instance();

    if (add) {
        status = sai_api->vlan_api->create_vlan(vid);
    } else {
        status = sai_api->vlan_api->remove_vlan(vid);
    }
    SAI_ERROR_LOG_EXIT(status, "Failed to %s vlan vid %d",
                       add ? "create" : "remove", vid);

exit:
    return SAI_ERROR_2_ERRNO(status);
}

/*
 * Sets vlan to port.
 * @param[in] vid VLAN id.
 * @param[in] hw_id port label id.
 * @param[in] mode tagging mode: tagged/untagged.
 * @param[in] add boolean which says if port should be added or removed to/from
 * vlan.
 * @return 0, sai error converted to errno otherwise.
 */
static int
__vlan_port_set(sai_vlan_id_t vid, uint32_t hw_id,
                sai_vlan_tagging_mode_t mode, bool add)
{
    sai_vlan_port_t vlan_port = { };
    sai_status_t status = SAI_STATUS_SUCCESS;
    const struct ops_sai_api_class *sai_api = ops_sai_api_get_instance();

    vlan_port.port_id = ops_sai_api_hw_id2port_id(hw_id);
    vlan_port.tagging_mode = mode;
    if (add) {
        status = sai_api->vlan_api->add_ports_to_vlan(vid, 1, &vlan_port);
    } else {
        status = sai_api->vlan_api->remove_ports_from_vlan(vid, 1, &vlan_port);
    }
    SAI_ERROR_LOG_EXIT(status, "Failed to %s vlan %d on port %u",
                       add ? "add" : "remove", vid, hw_id);

    if (add && (SAI_VLAN_PORT_UNTAGGED != mode)) {
        goto exit;
    }

    /* No need to convert return code - already errno value. */
    return ops_sai_port_pvid_set(hw_id, vid);

exit:
    return SAI_ERROR_2_ERRNO(status);
}

/*
 * Sets trunks to port.
 * @param[in] trunks vlan bitmap.
 * @param[in] hw_id port label id.
 * @param[in] add boolean which says if port should be added or removed to/from
 * vlan.
 * @return 0, sai error converted to errno otherwise.
 */
static int
__trunks_port_set(const unsigned long *trunks, uint32_t hw_id, bool add)
{
    int vid = 0;
    int status = 0;

    NULL_PARAM_LOG_ABORT(trunks);

    BITMAP_FOR_EACH_1(vid, VLAN_BITMAP_SIZE, trunks) {
        status = __vlan_port_set(vid, hw_id, SAI_VLAN_PORT_TAGGED, add);
        ERRNO_LOG_EXIT(status, "Failed to %s trunks", add ? "add" : "remove");
    }

exit:
    return status;
}

DEFINE_GENERIC_CLASS(struct vlan_class, vlan) = {
        .init = __vlan_init,
        .access_port_add = __vlan_access_port_add,
        .access_port_del = __vlan_access_port_del,
        .trunks_port_add = __vlan_trunks_port_add,
        .trunks_port_del = __vlan_trunks_port_del,
        .set = __vlan_set,
        .deinit = __vlan_deinit,
};

DEFINE_GENERIC_CLASS_GETTER(struct vlan_class, vlan);
