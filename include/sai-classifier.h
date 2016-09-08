/*
 * sai-classifier.h
 *
 *  Created on: Jul 24, 2016
 *      Author: nird
 */

#ifndef SAI_CLASSIFIER_H
#define SAI_CLASSIFIER_H 1

#include <sai-common.h>
#ifdef SAI_VENDOR
#include <sai-vendor-common.h>
#endif /* SAI_VENDOR */

#define ACL_COUNTERS_MAX            1000
#define ACL_RULES_PER_TABLE_MAX     512
#define ACL_L4_RANGE_MAX            0
#define ACL_TABLES_MAX              128

struct sai_classifier_resources {
    int rules;
    int acl_range;
    int counters;
};

struct sai_classifier_global_db {
    int tables;
    struct sai_classifier_table *existing_tables[ACL_TABLES_MAX];
    struct sai_classifier_resources global_use;
    struct sai_classifier_resources per_table_use[ACL_TABLES_MAX];
};

struct classifier_class {
    /**
     * Initializes classifier.
     */
    void (*init)(void);
    /**
     * De-initializes classifier.
     */
    void (*deinit)(void);

};

DECLARE_GENERIC_CLASS_GETTER(struct classifier_class, classifier);

#define ops_sai_classifier_class_generic() (CLASS_GENERIC_GETTER(classifier)())

#ifndef ops_sai_classifier_class
#define ops_sai_classifier_class ops_sai_classifier_class_generic
#endif

static inline void
ops_sai_classifier_init(void)
{
    ovs_assert(ops_sai_classifier_class()->init);
    ops_sai_classifier_class()->init();
}


static inline void
ops_sai_classifier_deinit(void)
{
    ovs_assert(ops_sai_classifier_class()->deinit);
    ops_sai_classifier_class()->deinit();
}

int
register_classifier_sai_plugin(void);


#endif /* SAI_CLASSIFIER_H */
