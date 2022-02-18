#include "node.h"
#include "shared/so_conf.h"
#include "shared/so_ipc.h"
#include "shared/so_random.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#define GIFTNSEC 50000000
#define REWARDSENDER -1

extern conf_t conf;
extern char sigint;
extern masterbook *mbook;

static int *myFriends;
static int myFriendsNum = 0;
static transaction *myTransactionPool;
static int myTransactionPoolNum = 0;

void handleMessage(tmessage *ptr);

static void multipurposeHandler(int snum);
static void setupSignalHandlers();

static int myLoopIndex;

void nodeRoutine(int loopIndex)
{
    int i, j, sum, notified;
    tmessage tmsg;
    transaction t;
    transaction *block;
    pid_t *notificationarray; /* To prevent notification flood to the same user regarding the same block update */
    setupSignalHandlers();
    srandom(time(NULL) ^ (getpid() << 16));
    updateMyListeningQueue(loopIndex, UPDATE_AS_NODE);
    myLoopIndex = loopIndex;
    notificationarray = calloc(conf.USERS_NUM, sizeof(pid_t));
    block = calloc(conf.BLOCK_SIZE, sizeof(transaction));
    myTransactionPool = calloc(conf.TP_SIZE, sizeof(transaction));
    /* We initialize our friends array to store base friends */
    myFriends = calloc(conf.NUM_FRIENDS, sizeof(pid_t));
    /* We wait for control messages containing friends infos */
    for (myFriendsNum = 0; myFriendsNum < conf.NUM_FRIENDS; myFriendsNum++)
    {
        waittmessage(&tmsg, TMEX_FRIENDS_INFO);
        myFriends[myFriendsNum] = tmsg.value;
    }
    /* We wait for the entire simulation to be ready */
    syncwait();

    /* We setup the alarm timer to gift each nsec times */
    if (conf.NUM_FRIENDS > 0)
        setalarmtimer(GIFTNSEC);
    /* We now can proceed to listen */
    while (!sigint)
    {
        /* Resto in waiting fino ad avere sufficienti transazioni per emettere un blocco */
        while (myTransactionPoolNum < conf.BLOCK_SIZE - 1)
        {
            while(checktmessage(&tmsg, TMEX_NEW_NODE) != -1)
            {
                blocksignal(SIGALRM);
                handleMessage(&tmsg);
                unblocksignal(SIGALRM);
            }
            if (waittmessage(&tmsg, 0) == -1)
                continue;
            blocksignal(SIGALRM);
            handleMessage(&tmsg);
            unblocksignal(SIGALRM);
        }
        blocksignal(SIGALRM);
        while (checktmessage(&tmsg, 0) != -1 || checktmessage(&tmsg, TMEX_NEW_NODE) != -1)
            handleMessage(&tmsg);
        /* 1. Creazione del blocco candidato */
        /* We now have enough transactions to write a block, so we proceed to create one */

        /* Controlliamo nuovamente se vi sono sufficienti transazioni perché*/
        /* We proceed by transferring BLOCK_SIZE - 1 transactions to our block */
        for (i = 0; i < conf.BLOCK_SIZE - 1; i++)
            block[i] = myTransactionPool[i];
        for (; i < myTransactionPoolNum; i++)
            myTransactionPool[i - conf.BLOCK_SIZE + 1] = myTransactionPool[i];
        myTransactionPoolNum -= (conf.BLOCK_SIZE - 1);
        sum = 0;
        for (i = 0; i < conf.BLOCK_SIZE - 1; i++)
            sum += block[i].reward;
        t.timestamp = time(NULL);
        t.sender = REWARDSENDER;
        t.receiver = loopIndex;
        t.quantity = sum;
        t.reward = 0;
        block[conf.BLOCK_SIZE - 1] = t;
        /* 2. Simulo l'elaborazione di un blocco attraverso una attesa non attiva di un intervallo temporale casuale
         * espresso in nanosecondi compreso tra SO_MIN_TRANS_PROC_NSEC e SO_MAX_TRANS_PROC_NSEC */
        nsleep(so_random(conf.MIN_TRANS_PROC_NSEC, conf.MAX_TRANS_PROC_NSEC));
        /* 3. Scrivo il nuovo blocco appena elaborato nel libro mastro. N.B. Le transazioni scritte con successo
         * all'interno della Transaction Pool sono già stata eliminate dalla transaction pool prima. */
        waitbookwrite();
        if (mbook->n_blocks == conf.REGISTRY_SIZE)
        {
            endbookwrite();
            /* Utilizziamo l'handler per gestire la chiusura e inviare le info necessarie al master */
            multipurposeHandler(SIGINT);
            /* L'handler di sigint si occuperà anche di terminare la nostra esecuzione */
        }
        for (i = 0; i < conf.BLOCK_SIZE; i++)
        {
            mbook->blocks[mbook->n_blocks][i] = block[i];
        }
        mbook->n_blocks++;
        endbookwrite();

        /* We send signals for eventually waking up users */
        notified = 0;
        for (i = 0; i < conf.BLOCK_SIZE - 1; i++)
        {
            for (j = 0; j < notified; j++)
                if (block[i].receiver == notificationarray[j])
                    break;
            if (j == notified)
            {
                tmsg.object = TMEX_NEW_BLOCK;
                /* L'utente se ha la queue piena è morto molto probabilmente, ma comunque inviamo i messaggi solo a scopo 
                di notifica, di conseguenza essendo che legge solo per sfruttare l'attesa, non ci importa inviargliene uno con certezza */
                trysendtmessage(tmsg, block[i].receiver, TO_USER);
                notificationarray[notified++] = block[i].receiver;
            }
        }
        unblocksignal(SIGALRM);
    }
    exit(EXIT_SUCCESS);
}

void handleMessage(tmessage *ptr)
{
    extern int *nodemsgids;
    extern int nodesNumber;
    if(ptr->object == getpid())
    {
        nodemsgids = reallocarray(nodemsgids, nodesNumber + 1, sizeof(int));
        myFriends = reallocarray(myFriends, myFriendsNum + 1, sizeof(pid_t));
        /* Il campo hops contiene il msgid del nuovo nodo */
        nodemsgids[nodesNumber] = ptr->value;
        /* Aggiungiamo l'index dell'id della queue come amico a quelli già esistenti e successivamente incrementiamo
             * sia il contatore degli amici, sia il numero dei nodi */
        myFriends[myFriendsNum++] = nodesNumber++;
        break;
    }
    switch (ptr->object)
    {
    /* Se abbiamo sufficiente spazio, la aggiungiamo alla pool, sennò la rimandiamo indietro */
    case TMEX_PROCESS_RQST:
    {
        if (myTransactionPoolNum < conf.TP_SIZE)
            myTransactionPool[myTransactionPoolNum++] = ptr->transaction;
        else
        {
            ptr->object = TMEX_GIFT_MESSAGE;
            ptr->value = conf.HOPS;
            while(ptr->value > 0)
                if(trysendtmessage(*ptr, myFriends[so_random(0, myFriendsNum)], TO_NODE) != -1)
                    break;
                else
                    ptr->value--;
            if(ptr->value == 0)
            {
                ptr->object = TMEX_HOPS_ZERO;
                sendtmessage(*ptr, 0, TO_MSTR);
            }
        }
        break;
    }
    case TMEX_GIFT_MESSAGE:
    {
        if (myTransactionPoolNum < conf.TP_SIZE)
            myTransactionPool[myTransactionPoolNum++] = ptr->transaction;
        else
        {
            /* Object is already set to gift */
            ptr->value = conf.HOPS;
            while(ptr->value > 0)
                if(trysendtmessage(*ptr, myFriends[so_random(0, myFriendsNum)], TO_NODE) != -1)
                    break;
                else
                    ptr->value--;
            if(ptr->value == 0)
            {
                ptr->object = TMEX_HOPS_ZERO;
                sendtmessage(*ptr, 0, TO_MSTR);
            }
        }
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
    tmessage tmsg;
    switch (snum)
    {
    case SIGALRM:
    {
        if (myTransactionPoolNum > 0)
        {
            tmsg.object = TMEX_GIFT_MESSAGE;
            tmsg.value = conf.HOPS;
            tmsg.transaction = myTransactionPool[--myTransactionPoolNum];
            while(tmsg.value > 0)
                if(trysendtmessage(tmsg, myFriends[so_random(0, myFriendsNum)], TO_NODE) != -1)
                    break;
                else
                    tmsg.value--;
            if(tmsg.value == 0)
            {
                tmsg.object = TMEX_HOPS_ZERO;
                sendtmessage(tmsg, 0, TO_MSTR);
            }
        }
        break;
    }
    case SIGINT:
    {
        /* Non possiamo prevedere quando riceveremo e dove il prossimo SIGALRM, per prevenire che interrompa la call di send la blocchiamo */
        blocksignal(SIGALRM);
        tmsg.object = TMEX_NODE_EXIT_INFO;
        tmsg.value = myTransactionPoolNum;
        tmsg.transaction.sender = getpid();
        sendtmessage(tmsg, 0, TO_MSTR);
        /* Non ci interessa sbloccare il segnale, dato che usciamo */
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

pid_t spawnNode(int loopIndex)
{
    pid_t fValue = fork();
    if (fValue == 0)
        nodeRoutine(loopIndex);
    else
        return fValue;
}

/* It still requires a pid_t node for cmessage type, loopIndex to avoid considering our index as friend's one */
void sendFriendsTo(int loopIndex)
{
    extern int nodesNumber;
    int i, j, alreadySet;
    int *includedArr;
    tmessage tm;
    tm.object = TMEX_FRIENDS_INFO;
    includedArr = calloc(conf.NUM_FRIENDS, sizeof(int));
    alreadySet = 0;
    for (i = 0; i < conf.NUM_FRIENDS; i++)
    {
        tm.value = (int)so_random(0, nodesNumber);
        for (j = 0; j < alreadySet; j++)
            if (includedArr[j] == tm.value)
                break;
        if (j == alreadySet && tm.value != loopIndex)
        {
            includedArr[alreadySet++] = tm.value;
            sendtmessage(tm, loopIndex, TO_NODE);
        }
        else
            i--;
    }
    free(includedArr);
}
