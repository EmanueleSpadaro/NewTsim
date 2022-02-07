/*
 Created by maffin on 1/31/22.
*/

#ifndef NEWTSIM_SO_IPC_H
#define NEWTSIM_SO_IPC_H

#include <sys/types.h>

#define SEM_SYNC 0
#define SEM_WRITER 1
#define SEM_READER 2
/* IPC Read Permissions */
#define IPC_RD 0400
/* IPC Write Permissions */
#define IPC_WR 0200
/* IPC Read/Write Permissions */
#define IPC_RW 0600

/* Inizializza le risorse IPC utilizzate durante la simulazione */
int initipcs();

/* Rilascia al sistema operativo le risorse IPC utilizzate durante la simulazione */
int releaseipcs();

/* Sets the semaphore to a value for which users are notified to start execution */
int startsyncsem();
/* Waits for the sync semaphore to go */
int syncwait();


/* Struttura che definisce una transazione all'interno della simulazione */
typedef struct {
    long timestamp;
    pid_t sender;
    pid_t receiver;
    int quantity;
    int reward;
} transaction;

#define UPDATE_AS_NODE 0x0D
#define UPDATE_AS_USER 0xE
int updateMyListeningQueue(int i, short type);

#define TMEX_PROCESS_RQST 0x32
#define TMEX_GIFT_MESSAGE 0x69
#define TMEX_NEW_NODE     0xAAAA
#define TMEX_FRIENDS_INFO   0x3333
#define TMEX_TP_FULL        0x24
#define TMEX_NEW_BLOCK      0x55
#define TMEX_HOPS_ZERO      0x71
#define TMEX_USER_EXIT      0xEEEE
#define TMEX_NODE_EXIT_INFO 0xBBBB
/* Struttura che definisce la struttura dei messaggi all'interno della message queue */
/* Utilizziamo il valore del tipo di messaggio come slot per indicare il destinatario */
typedef struct {
    long object;
    transaction transaction;
    int value;
} tmessage;

#define TO_NODE 0x1
#define TO_USER 0x2
#define TO_MSTR 0x3
int trysendtmessage(tmessage tm, int index, short toConst);
int sendtmessage(tmessage tm, int index, short toConst);
int waittmessage(tmessage *tm);
int checktmessage(tmessage *tm);
int allocnewmsgq();

int waitbookwrite();
int endbookwrite();
int waitbookread();
int endbookread();

int blocksignal(int signum);
int unblocksignal(int signum);

/* Struttura che definisce i libro mastro condiviso da tutti i processi della simulazione */
typedef struct {
    int n_readers;
    int n_blocks;
    transaction **blocks;
} masterbook;


#endif
