/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#include <sai-log.h>
#include <sai-api-class.h>
#include <sai-common.h>
#include <sai-policer.h>

VLOG_DEFINE_THIS_MODULE(sai_policer);

/*
 * Initialize policers.
 */
void __policer_init(void)
{
    VLOG_INFO("Initializing policers");
}

/*
 * Create policer.
 *
 * @param[out] handle pointer to policer object. Will be set to policer object
 * id.
 * @param[in] config pointer to policer configuration.
 *
 * @return 0 on success, sai status converted to errno value.
 */
int
__policer_create(handle_t *handle,
                 const struct ops_sai_policer_config *config)
{
    sai_attribute_t attr[7] = { };
    sai_status_t status = SAI_STATUS_SUCCESS;
    const struct ops_sai_api_class *sai_api = ops_sai_api_get_instance();

    NULL_PARAM_LOG_ABORT(handle);
    NULL_PARAM_LOG_ABORT(config);

    attr[0].id = SAI_POLICER_ATTR_METER_TYPE;
    attr[0].value.u32 = SAI_METER_TYPE_PACKETS;
    attr[1].id = SAI_POLICER_ATTR_MODE;
    attr[1].value.u32 = SAI_POLICER_MODE_Sr_TCM;
    attr[2].id = SAI_POLICER_ATTR_CBS;
    attr[2].value.u64 = config->burst_max;
    attr[3].id = SAI_POLICER_ATTR_CIR;
    attr[3].value.u64 = config->rate_max;
    attr[4].id = SAI_POLICER_ATTR_PBS;
    attr[4].value.u64 = config->burst_max;
    attr[5].id = SAI_POLICER_ATTR_PIR;
    attr[5].value.u64 = config->rate_max;
    attr[6].id = SAI_POLICER_ATTR_RED_PACKET_ACTION;
    attr[6].value.s32 = SAI_PACKET_ACTION_DROP;

    status = sai_api->policer_api->create_policer(&handle->data,
                                                 ARRAY_SIZE(attr),
                                                 attr);
    SAI_ERROR_LOG_EXIT(status, "Failed to create policer");

exit:
    return SAI_ERROR_2_ERRNO(status);
}

/*
 * Destroy policer.
 *
 * param[in] handle pointer to policer object.
 *
 * @return 0 on success, sai status converted to errno value.
 */
int
__policer_remove(const handle_t *handle)
{
    sai_status_t status = SAI_STATUS_SUCCESS;
    const struct ops_sai_api_class *sai_api = ops_sai_api_get_instance();

    NULL_PARAM_LOG_ABORT(handle);

    status = sai_api->policer_api->remove_policer(handle->data);
    SAI_ERROR_LOG_EXIT(status, "Failed to remove policer");

exit:
    return SAI_ERROR_2_ERRNO(status);
}

/*
 * De-initialize policers.
 */
void __policer_deinit(void)
{
    VLOG_INFO("De-initializing policers");
}

DEFINE_GENERIC_CLASS(struct policer_class, policer) = {
        .init = __policer_init,
        .create = __policer_create,
        .remove = __policer_remove,
        .deinit = __policer_deinit,
};

DEFINE_GENERIC_CLASS_GETTER(struct policer_class, policer);
