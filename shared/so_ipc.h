//
// Created by maffin on 1/31/22.
//

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

#define TMEX_PROCESS_RQST 0x32
#define TMEX_GIFT_MESSAGE 0x69
#define TMEX_TP_FULL      0x24
#define TMEX_NEW_BLOCK    0x55
#define CMEX_FRIENDS_INFO 0x3333

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

/* Struttura che definisce la struttura dei messaggi all'interno della message queue */
/* Utilizziamo il valore del tipo di messaggio come slot per indicare il destinatario */
typedef struct {
    long recipient;
    int object;
    transaction transaction;
    int hops;
} tmessage;

/* Struttura che definisce la struttura dei messaggi contenente gli amici per i nodi */
/* La union non è*/
typedef struct {
    long recipient;
    int object;
    pid_t friend;
} cmessage;

int trysendtmessage(tmessage tm);
int sendtmessage(tmessage tm);
int waittmessage(tmessage *tm);
int checktmessage(tmessage *tm);
int sendcmessage(cmessage cm);
int waitcmessage(cmessage *cm);

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


#endif //NEWTSIM_SO_IPC_H
