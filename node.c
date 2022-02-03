//
// Created by maffin on 1/31/22.
//

#include "node.h"
#include "shared/so_conf.h"
#include "shared/so_ipc.h"
#include "shared/so_random.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#define GIFTNSEC 500000000
#define REWARDSENDER -1

extern conf_t conf;
extern char sigint;
extern masterbook *mbook;

static pid_t *myFriends;
static sig_atomic_t myFriendsNum = 0;
static transaction *myTransactionPool;
static sig_atomic_t myTransactionPoolNum = 0;



void handleMessage(tmessage *ptr);

static void multipurposeHandler(int snum);
static void setupSignalHandlers();

void nodeRoutine(int loopIndex)
{
    int i, j, sum, notified;
    cmessage cmsg;
    tmessage tmsg;
    transaction t;
    pid_t *notificationarray;   /* To prevent notification flood to the same user regarding the same block update */
    setupSignalHandlers();
    srandom(time(NULL) ^ (getpid()<<16));

    notificationarray = calloc(conf.USERS_NUM, sizeof(pid_t));
    transaction *block = calloc(conf.BLOCK_SIZE, sizeof(transaction));
    myTransactionPool = calloc(conf.TP_SIZE, sizeof(transaction));
    /* We initialize our friends array to store base friends */
    myFriends = calloc(conf.NUM_FRIENDS, sizeof(pid_t));
    /* We wait for control messages containing friends infos */
    for (myFriendsNum = 0; myFriendsNum < conf.NUM_FRIENDS; myFriendsNum++)
    {
        waitcmessage(&cmsg);
        if(cmsg.object != CMEX_FRIENDS_INFO)
        {
            fprintf(stderr, "[N%i] Quitting because of unexpected message when waiting for friends\n",
                    getpid());
            exit(EXIT_FAILURE);
        }
        myFriends[myFriendsNum] = cmsg.friend;
    }
    updateMyListeningQueue(loopIndex);
    /* We wait for the entire simulation to be ready */
    syncwait();

    /* We setup the alarm timer to gift each nsec times */
    setalarmtimer(GIFTNSEC);
    /* We now can proceed to listen */
    while(!sigint)
    {
        /* Resto in waiting fino ad avere sufficienti transazioni per emettere un blocco */
        while(myTransactionPoolNum < conf.BLOCK_SIZE - 1)
        {
            if(waittmessage(&tmsg) == -1)
                continue;
            blocksignal(SIGALRM);
            handleMessage(&tmsg);
            unblocksignal(SIGALRM);
        }
        blocksignal(SIGALRM);
        while(myTransactionPoolNum < conf.TP_SIZE && checktmessage(&tmsg) != -1)
            handleMessage(&tmsg);
        /* 1. Creazione del blocco candidato */
        /* We now have enough transactions to write a block, so we proceed to create one */

        /* Controlliamo nuovamente se vi sono sufficienti transazioni perché*/
        /* We proceed by transferring BLOCK_SIZE - 1 transactions to our block */
        for(i = 0; i < conf.BLOCK_SIZE - 1; i++)
            block[i] = myTransactionPool[i];
        for(; i < myTransactionPoolNum; i++)
            myTransactionPool[i - conf.BLOCK_SIZE + 1] = myTransactionPool[i];
        myTransactionPoolNum -= (conf.BLOCK_SIZE - 1);
        sum = 0;
        for(i = 0; i < conf.BLOCK_SIZE - 1; i++)
            sum += block[i].reward;
        t.timestamp = time(NULL);
        t.sender = REWARDSENDER;
        t.receiver = getpid();
        t.quantity = sum;
        t.reward = 0;
        block[conf.BLOCK_SIZE - 1] = t;
        /* 2. Simulo l'elaborazione di un blocco attraverso una attesa non attiva di un intervallo temporale casuale
         * espresso in nanosecondi compreso tra SO_MIN_TRANS_PROC_NSEC e SO_MAX_TRANS_PROC_NSEC */
        nsleep(so_random(conf.MIN_TRANS_PROC_NSEC, conf.MAX_TRANS_PROC_NSEC));
        /* 3. Scrivo il nuovo blocco appena elaborato nel libro mastro. N.B. Le transazioni scritte con successo
         * all'interno della Transaction Pool sono già stata eliminate dalla transaction pool prima. */
        waitbookwrite();
        if(mbook->n_blocks == conf.REGISTRY_SIZE)
        {
            endbookwrite();
            /* Utilizziamo l'handler per gestire la chiusura e inviare le info necessarie al master */
            multipurposeHandler(SIGINT);
            /* L'handler di sigint si occuperà anche di terminare la nostra esecuzione */
        }
        for(i = 0; i < conf.BLOCK_SIZE; i++)
        {
            mbook->blocks[mbook->n_blocks][i] = block[i];
        }
        mbook->n_blocks++;
        endbookwrite();

        /* We send signals for eventually waking up users */
        notified = 0;
        for(i = 0; i < conf.BLOCK_SIZE - 1; i++)
        {
            for(j = 0; j < notified; j++)
                if(block[i].receiver == notificationarray[j])
                    break;
            if(j == notified)
            {
                cmsg.object = CMEX_NEW_BLOCK;
                cmsg.recipient = block[i].receiver;
                sendcmessage(cmsg);
                notificationarray[notified++] = block[i].receiver;
            }
        }

        unblocksignal(SIGALRM);
    }

    printf("[%i] Finalmente posso fare qualcosa\n", getpid());
    exit(EXIT_SUCCESS);
}

void handleMessage(tmessage *ptr) {
    extern int *nodemsgids;
    extern int nodesNumber;
    cmessage cmsg;
    switch (ptr->object) {
        /* Se abbiamo sufficiente spazio, la aggiungiamo alla pool, sennò la rimandiamo indietro */
        case TMEX_PROCESS_RQST:
        {
            if(myTransactionPoolNum < conf.TP_SIZE)
                myTransactionPool[myTransactionPoolNum++] = ptr->transaction;
            else
            {
                cmsg.object = CMEX_TP_FULL;
                cmsg.recipient = ptr->transaction.sender;
                cmsg.transaction = ptr->transaction;
                sendcmessage(cmsg);
            }
            break;
        }
        case TMEX_GIFT_MESSAGE:
        {
            if(myTransactionPoolNum < conf.TP_SIZE)
                myTransactionPool[myTransactionPoolNum++] = ptr->transaction;
            else
            {
                /* Object is already set */
                if(--(ptr->hops) > 0)
                    sendtmessage(*ptr, (int)so_random(0, myFriendsNum));
                else
                {
                    /* HOPS hanno raggiunto 0, di conseguenza inviamo un messaggio di richiesta al master e gli inviamo
                     * la transazione da inviare al nuovo nodo che genererà come da specifiche */
                    cmsg.recipient = getppid();
                    cmsg.object = CMEX_HOPS_ZERO;
                    cmsg.transaction = ptr->transaction;
                    sendcmessage(cmsg);
                }
            }
            break;
        }
        case TMEX_NEW_NODE:
        {
            nodemsgids = reallocarray(nodemsgids, nodesNumber + 1, sizeof(int));
            myFriends = reallocarray(myFriends, myFriendsNum+1, sizeof(pid_t));
            /* Il campo hops contiene il msgid del nuovo nodo */
            nodemsgids[nodesNumber] = ptr->hops;
            /* Aggiungiamo l'index dell'id della queue come amico a quelli già esistenti e successivamente incrementiamo
             * sia il contatore degli amici, sia il numero dei nodi */
            myFriends[myFriendsNum++] = nodesNumber++;
            break;
        }
        default:
        {
            fprintf(stderr, "[N%i] Received unknown %li message object from %i\n", getpid(), ptr->object,
                    ptr->transaction.sender);
            break;
        }
    }
}

static void multipurposeHandler(int snum)
{
    cmessage cmsg;
    switch (snum) {
        case SIGALRM:
        {
            tmessage tmsg;
            tmsg.object = TMEX_GIFT_MESSAGE;
            tmsg.hops = conf.HOPS;
            tmsg.transaction = myTransactionPool[--myTransactionPoolNum];
            sendtmessage(tmsg, so_random(0, myFriendsNum));
            break;
        }
        case SIGINT:
        {
            cmsg.object = CMEX_NODE_EXIT_INFO;
            cmsg.friend = myTransactionPoolNum;
            cmsg.recipient = getppid();
            cmsg.transaction.sender = getpid();
            sendcmessage(cmsg);
            exit(EXIT_SUCCESS);
        }
        default:
        {
            fprintf(stderr, "[N%i] Unhandled signal #%i received\n", getpid(), snum);
        }
    }
}

static void setupSignalHandlers()
{
    signal(SIGALRM, multipurposeHandler);
    signal(SIGINT, multipurposeHandler);
}


pid_t spawnNode(int loopIndex) {
    pid_t fValue = fork();
    if(fValue == 0)
        nodeRoutine(loopIndex);
    else
        return fValue;
}

/* It still requires a pid_t node for cmessage type, loopIndex to avoid considering our index as friend's one */
void sendFriendsTo(pid_t node, int loopIndex) {
    extern int nodesNumber;
    int i, j, alreadySet;
    int *includedArr;
    cmessage cm;
    cm.recipient = node;
    cm.object = CMEX_FRIENDS_INFO;
    includedArr = calloc(conf.NUM_FRIENDS, sizeof(int));
    alreadySet = 0;
    for (i = 0; i < conf.NUM_FRIENDS; i++)
    {
        cm.friend = (int)so_random(0, nodesNumber);
        for(j = 0; j < alreadySet; j++)
            if(includedArr[j] == cm.friend)
                break;
        if(j == alreadySet && cm.friend != loopIndex)
        {
            includedArr[alreadySet++] = cm.friend;
            sendcmessage(cm);
        }
        else
            i--;
    }
    free(includedArr);
}
