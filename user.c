//
// Created by maffin on 2/1/22.
//
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include "user.h"
#include "shared/so_random.h"
#include "shared/so_ipc.h"
#include "shared/so_conf.h"

extern conf_t conf;
extern masterbook *mbook;
extern pid_t *userPIDs;
extern pid_t *nodePIDs;
extern int nodesNumber;
extern char sigint;

typedef struct tnode
{
    struct tnode *next;
    transaction t;
} tnode;

static tnode *pendingList = NULL;
static void addPending(transaction t);
static void removePending(transaction t);
static int getMyBudget();

static tmessage genRandomTmsg(int budget);
static void sendManualTransaction();
static void handleMessageU(cmessage *cmsg);
static void setupUserHandlers();

void userRoutine()
{
    srandom(time(NULL) ^ (getpid()<<16));
    syncwait();
    setupUserHandlers();

    int myRetry = conf.RETRY;
    int myBudget;
    tmessage tmsg;
    cmessage cmsg;

    while(myRetry > 0)
    {
        /* 1. Calcolo il bilancio corrente a partire dal budget iniziale, facendo la somma algebrica delle entrate
         * e delle uscite registrate nelle transazioni presenti nel libro mastro, sottraendo gli importi delle
         * transazioni spedite, ma non ancora registrate nel libro mastro */
        blocksignal(SIGUSR1);   /* Preveniamo transazioni manuali che possano rendere inconsistente il budget */
        blocksignal(SIGUSR2);   /* Preveniamo che le update dei nodi non interrompano le system call */
        myBudget = getMyBudget();
        //fprintf(stderr,"[%i] Got my budget\n", getpid());
        if(myBudget >= 2)
        {
            /* Avendo budget >= 2, generiamo una transazione*/
            //fprintf(stderr,"[%i] Generating transaction\n", getpid());
            tmsg = genRandomTmsg(myBudget);
            /* Inviamo al nodo estratto la transazione e attendiamo un intervallo di tempo casuale */
            /* Se non riusciamo ad inviare la transazione, non la aggiungiamo alla pending list e decrementiamo retry */
            //fprintf(stderr,"[%i] Trying to send\n", getpid());
            if(trysendtmessage(tmsg, (int)so_random(0, nodesNumber)) == -1)
                myRetry--;
            else
            {
                /* Siamo riusciti ad inviarla, resettiamo al valore di fallimenti consecutivi consentito */
                myRetry = conf.RETRY;
                addPending(tmsg.transaction);
            }
            //fprintf(stderr,"[%i] Waiting pseudo gen\n", getpid());
            nsleep(so_random(conf.MIN_TRANS_GEN_NSEC, conf.MAX_TRANS_GEN_NSEC));
            //fprintf(stderr,"[%i] Pseudo Gen Completed\n", getpid());
        }
        else
        {
            waitcmessage(&cmsg);
            handleMessageU(&cmsg);
            while(checkcmessage(&cmsg) != -1)
                handleMessageU(&cmsg);
        }
        unblocksignal(SIGUSR1); /* Prevenzione inconsistenza budget con transazioni manuali */
        unblocksignal(SIGUSR2); /* Preveniamo che le update dei nodi non interrompano le system call */
    }
    fprintf(stderr, "Exiting because of so-retry\n");
    kill(getppid(), SIGUSR1);
    exit(EXIT_SUCCESS);
}

static void handleMessageU(cmessage *cmsg) {
    switch ((*cmsg).object) {
        case CMEX_TP_FULL:
        {
            removePending((*cmsg).transaction);
            break;
        }
        case CMEX_NEW_BLOCK:
        {
            break;
        }
        default:
        {
            break;
        }
    }
}

pid_t spawnUser() {
    pid_t fValue = fork();
    if(fValue == 0)
        userRoutine();
    else
        return fValue;
}

static tmessage genRandomTmsg(int budget)
{
    int tmp;
    pid_t tmpPid;
    tmessage tmsg;
    tmsg.object = TMEX_PROCESS_RQST;
    /* Il totale che spenderemo per la transazione */
    tmp = (int)so_random(2, budget+1);
    /* Un utente diverso da noi che riceverà i soldi*/
    do{
        tmpPid = userPIDs[so_random(0, conf.USERS_NUM)];
    }while(tmpPid == getpid());
    tmsg.transaction.timestamp = time(NULL);
    tmsg.transaction.sender = getpid();
    tmsg.transaction.receiver = tmpPid;
    tmsg.transaction.reward = tmp * conf.REWARD / 100;
    if(tmsg.transaction.reward < 1)
    {
        tmsg.transaction.reward = 1;
    }
    tmsg.transaction.quantity = (tmp - tmsg.transaction.reward);
    return tmsg;
}

static int pendingBudget()
{
    tnode *tptr = pendingList;
    int pendingDebt = 0;
    while(tptr != NULL)
    {
        pendingDebt += (tptr->t.quantity + tptr->t.reward);
        tptr = tptr->next;
    }
    return pendingDebt;
}

static int getMyBudget()
{
    static int lastBlock = 0;
    static int bookBudgetReport = 0;
    int i, j;
    waitbookread();
    if(lastBlock != mbook->n_blocks) {
        for (i = lastBlock; i < mbook->n_blocks; i++) {
            for (j = 0; j < conf.BLOCK_SIZE; j++) {
                if (mbook->blocks[i][j].sender == getpid()) {
                    bookBudgetReport -= (mbook->blocks[i][j].quantity + mbook->blocks[i][j].reward);
                    removePending(mbook->blocks[i][j]);
                } else if (mbook->blocks[i][j].receiver == getpid())
                    bookBudgetReport += (mbook->blocks[i][j].quantity);
            }
        }
        lastBlock = mbook->n_blocks;
    }
    endbookread();
    return conf.BUDGET_INIT + bookBudgetReport - pendingBudget();
}

static void addPending(transaction t) {
    tnode *tptr;
    tnode *node = malloc(sizeof(tnode));
    node->next = NULL;
    node->t = t;
    if (pendingList == NULL) {
        pendingList = node;
        return;
    }
    tptr = pendingList;
    while(tptr->next != NULL)
        tptr = tptr->next;
    tptr->next = node;
}

static int transeq(transaction t1, transaction t2)
{
    if(t1.timestamp == t2.timestamp && t1.sender == t2.sender && t1.receiver == t2.receiver)
        return 1;
    return 0;
}

static void removePending(transaction t)
{
    tnode *tptr, *prev;
    if(pendingList == NULL)
        return;
    /* Se è uguale alla testa della lista, cancelliamo la testa dalla lista e settiamo la testa al next */
    if(transeq(pendingList->t, t))
    {
        tptr = pendingList->next;
        free(pendingList);
        pendingList = tptr;
        return;
    }
    prev = pendingList;
    for(tptr = pendingList->next; tptr != NULL; tptr = tptr->next)
    {
        if(transeq(tptr->t, t))
        {
            prev->next = tptr->next;
            free(tptr);
            return;
        }
        prev = tptr;
    }
    /* Se si giunge a questa parte del codice non vi è alcuna corrispondenza */
    fprintf(stderr, "removePending: no entry for given value\n");
    exit(EXIT_FAILURE);
}

static void sendManualTransaction()
{
    tmessage tmsg;
    int myBudget = getMyBudget();
    if(myBudget >= 2)
    {
        /* Avendo budget >= 2, generiamo una transazione*/
        tmsg = genRandomTmsg(myBudget);
        /* Inviamo al nodo estratto la transazione e attendiamo un intervallo di tempo casuale */
        if(trysendtmessage(tmsg, (int)so_random(0, nodesNumber)) != -1)
            addPending(tmsg.transaction);
        nsleep(so_random(conf.MIN_TRANS_GEN_NSEC, conf.MAX_TRANS_GEN_NSEC));
    }
}

static void friendsUpdateHandler(int sig, siginfo_t *si, void *ucontext)
{
    extern int *nodemsgids;
    nodemsgids = reallocarray(nodemsgids, nodesNumber + 1, sizeof(int));
    /* Il segnale contiene il msgid del nuovo nodo, incrementiamo pure il contatore dei nodi per poter ottenere
     * l'indice della nuova msgid dalla selezione casuale dell'indice di riferimento ai nodi che ricevono le nostre
     * transazioni */
    nodemsgids[nodesNumber++] = si->si_value.sival_int;
}

static void setupUserHandlers()
{
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = friendsUpdateHandler;
    sigaction(SIGUSR2, &sa, NULL);
    signal(SIGUSR1, sendManualTransaction);

}