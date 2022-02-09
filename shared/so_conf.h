/*
 Created by maffin on 1/31/22.
*/

#ifndef NEWTSIM_SO_CONF_H
#define NEWTSIM_SO_CONF_H

/* This module is delegated to manage configuration and prepare the entire simulation */
/*
 * This file contains every macro used inside the project
 * Macros default to Conf#1 specifications if not defined differently
 */

#define CONF_FILENAME "tsim.conf.json"

/* This struct helps to manage simulation parameters */
typedef struct so_conf
{
    int USERS_NUM;               /* Amount of Users generated */
    int NODES_NUM;               /* Amount of Nodes generated */
    int BUDGET_INIT;             /* Users Initial budget */
    int REWARD;                  /* Nodes transaction processing fee [0-100] as percentage */
    long MIN_TRANS_GEN_NSEC;     /* Users Min. InterTransaction Generation Gap Time */
    long MAX_TRANS_GEN_NSEC;     /* Users Max. InterTransaction Generation Gap Time */
    int RETRY;                   /* Users Max. amount of attempts to generate a transaction */
    int TP_SIZE;                 /* Nodes Transaction Pool size */
    int BLOCK_SIZE;              /* Master Book Block Size */
    long MIN_TRANS_PROC_NSEC;    /* Nodes Min. emulated time for transaction processing */
    long MAX_TRANS_PROC_NSEC;    /* Nodes Max. emulated time for transaction processing */
    int REGISTRY_SIZE;           /* Master Book Blocks Capacity */
    int SIM_SEC;                 /* Simulation duration in seconds */
    int NUM_FRIENDS;             /* Nodes amount of Friend Nodes */
    int HOPS;                    /* Transaction Hops between Nodes before Master gens new node */
} conf_t;

/* Generates the proper configuration struct considering optional arguments */
conf_t loadconf(int argc, char const *argv[]);

void printconf(conf_t conf);

/*
 * This file contains every macro used inside the project
 * Macros default to Conf#1 specifications if not defined differently
 */

#define SO_BLOCK_SIZE_1 100
#define SO_BLOCK_SIZE_2 10
#define SO_BLOCK_SIZE_3 10
#ifndef SO_BLOCK_SIZE
/* This macro specifies node's blocks size */
#define SO_BLOCK_SIZE SO_BLOCK_SIZE_1
#endif

#define SO_REGISTRY_SIZE_1 1000
#define SO_REGISTRY_SIZE_2 10000
#define SO_REGISTRY_SIZE_3 1000
#ifndef SO_REGISTRY_SIZE
/* This macro specifies master's book block capacity */
#define SO_REGISTRY_SIZE SO_REGISTRY_SIZE_1
#endif


/*
 * Macros to ease standard configuration specifications switch
 * CONF1 might be redundant, but it's still specified in case we want to change
 * the default value of macros from Conf#1 specifications to different ones
 */
#ifdef CONF1
#undef SO_BLOCK_SIZE
    #define SO_BLOCK_SIZE SO_BLOCK_SIZE_1
    #undef SO_REGISTRY_SIZE
    #define SO_REGISTRY_SIZE SO_REGISTRY_SIZE_1
#endif
#ifdef CONF2
#undef SO_BLOCK_SIZE
    #define SO_BLOCK_SIZE SO_BLOCK_SIZE_2
    #undef SO_REGISTRY_SIZE
    #define SO_REGISTRY_SIZE SO_REGISTRY_SIZE_2
#endif
#ifdef CONF3
#undef SO_BLOCK_SIZE
    #define SO_BLOCK_SIZE SO_BLOCK_SIZE_3
    #undef SO_REGISTRY_SIZE
    #define SO_REGISTRY_SIZE SO_REGISTRY_SIZE_3
#endif


/*
 * These macros aren't required, but are added to ease example Configuration testing
 * KEEP IN MIND: SO_REGISTRY_SIZE and SO_BLOCK_SIZE are NOT TO BE FOUND BELOW since
 * THEY ARE REQUIRED TO BE DEFINED AT COMPILE-TIME by Project Specification
 */
#define SO_USERS_NUM_1 100
#define SO_USERS_NUM_2 1000
#define SO_USERS_NUM_3 20

#define SO_NODES_NUM_1 10
#define SO_NODES_NUM_2 10
#define SO_NODES_NUM_3 10

#define SO_BUDGET_INIT_1 1000
#define SO_BUDGET_INIT_2 1000
#define SO_BUDGET_INIT_3 10000

#define SO_REWARD_1 1
#define SO_REWARD_2 20
#define SO_REWARD_3 1

#define SO_MIN_TRANS_GEN_NSEC_1 100000000
#define SO_MIN_TRANS_GEN_NSEC_2 10000000
#define SO_MIN_TRANS_GEN_NSEC_3 10000000

#define SO_MAX_TRANS_GEN_NSEC_1 200000000
#define SO_MAX_TRANS_GEN_NSEC_2 10000000
#define SO_MAX_TRANS_GEN_NSEC_3 20000000

#define SO_RETRY_1 20
#define SO_RETRY_2 2
#define SO_RETRY_3 10

#define SO_TP_SIZE_1 1000
#define SO_TP_SIZE_2 20
#define SO_TP_SIZE_3 100

#define SO_MIN_TRANS_PROC_NSEC_1 10000000
#define SO_MIN_TRANS_PROC_NSEC_2 1000000
#define SO_MIN_TRANS_PROC_NSEC_3 SO_MIN_TRANS_PROC_NSEC_2

#define SO_MAX_TRANS_PROC_NSEC_1 20000000
#define SO_MAX_TRANS_PROC_NSEC_2 1000000
#define SO_MAX_TRANS_PROC_NSEC_3 SO_MAX_TRANS_PROC_NSEC_2

#define SO_SIM_SEC_1 10
#define SO_SIM_SEC_2 20
#define SO_SIM_SEC_3 20

#define SO_FRIENDS_NUM_1 3
#define SO_FRIENDS_NUM_2 5
#define SO_FRIENDS_NUM_3 3

#define SO_HOPS_1 10
#define SO_HOPS_2 2
#define SO_HOPS_3 10

#endif
