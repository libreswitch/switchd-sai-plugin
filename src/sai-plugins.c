/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.  
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#include <unistd.h>
#include <ovs-thread.h>
#include <ovs-rcu.h>
#include <openvswitch/vlog.h>
#include <netdev-provider.h>
#include <ofproto/ofproto-provider.h>
#include <ovs-thread.h>
#include <ovs-rcu.h>

#include <sai-netdev.h>
#include <sai-ofproto-provider.h>
#include <sai-api-class.h>
#include <sai-log.h>

#define init libovs_sai_plugin_LTX_init
#define run libovs_sai_plugin_LTX_run
#define wait libovs_sai_plugin_LTX_wait
#define destroy libovs_sai_plugin_LTX_destroy
#define netdev_register libovs_sai_plugin_LTX_netdev_register
#define ofproto_register libovs_sai_plugin_LTX_ofproto_register
#define bufmon_register libovs_bcm_plugin_LTX_bufmon_register

#define MAX_CMD_LEN             50

VLOG_DEFINE_THIS_MODULE(sai_plugin);

static pthread_t sai_init_thread;
static struct ovs_barrier sai_init_barrier;

/* This function is called from the BCM SDK. */
void
ovs_sai_init_done(void)
{
    SAI_API_TRACE_FN();

    ovs_barrier_block(&sai_init_barrier);
}

static void *
ovs_sai_init_main(void *args OVS_UNUSED)
{
    SAI_API_TRACE_FN();

    ovsrcu_quiesce_start();
    sai_api_init();
    ovs_sai_init_done();

    return NULL;
}

void
ovs_sai_init(void)
{
    SAI_API_TRACE_FN();

    ovs_barrier_init(&sai_init_barrier, 2);
    sai_init_thread = ovs_thread_create("ovs-sai", ovs_sai_init_main, NULL);
    ovs_barrier_block(&sai_init_barrier);
    VLOG_INFO("SAI initialization complete");
    ovs_barrier_destroy(&sai_init_barrier);
}

void
init(void)
{
    SAI_API_TRACE_FN();

    ovs_sai_init();
}

void
run(void)
{
    SAI_API_TRACE_FN();
}

void
wait(void)
{
    SAI_API_TRACE_FN();
}

void
destroy(void)
{
    SAI_API_TRACE_FN();

    sai_api_uninit();
}

void
netdev_register(void)
{
    SAI_API_TRACE_FN();

    netdev_sai_register();
}

void
ofproto_register(void)
{
    SAI_API_TRACE_FN();

    ofproto_sai_register();
}

void
bufmon_register(void)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
}
