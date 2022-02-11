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
static void handleMessageU(tmessage *tmsg);
static void setupUserHandlers();
static int myIndex;

void userRoutine()
{
    int myRetry, myBudget, delete;
    tmessage tmsg;
    srandom(time(NULL) ^ (getpid() << 16));
    updateMyListeningQueue(myIndex, UPDATE_AS_USER);
    syncwait();
    setupUserHandlers();
    myRetry = conf.RETRY;

    while (myRetry > 0)
    {
        /* 1. Calcolo il bilancio corrente a partire dal budget iniziale, facendo la somma algebrica delle entrate
         * e delle uscite registrate nelle transazioni presenti nel libro mastro, sottraendo gli importi delle
         * transazioni spedite, ma non ancora registrate nel libro mastro */
        blocksignal(SIGUSR1); /* Preveniamo transazioni manuali che possano rendere inconsistente il budget */
        myBudget = getMyBudget();
        if (myBudget >= 2)
        {
            /* Avendo budget >= 2, generiamo una transazione*/
            tmsg = genRandomTmsg(myBudget);
            /* Inviamo al nodo estratto la transazione e attendiamo un intervallo di tempo casuale */
            /* Se non riusciamo ad inviare la transazione, non la aggiungiamo alla pending list e decrementiamo retry */
            if (trysendtmessage(tmsg, so_random(0, nodesNumber), TO_NODE) == -1)
                myRetry--;
            else
            {
                /* Siamo riusciti ad inviarla, resettiamo al valore di fallimenti consecutivi consentito */
                myRetry = conf.RETRY;
                addPending(tmsg.transaction);
            }
            nsleep(so_random(conf.MIN_TRANS_GEN_NSEC, conf.MAX_TRANS_GEN_NSEC));
        }
        else
        {
            if (waittmessage(&tmsg) != -1)
                handleMessageU(&tmsg);
            while (checktmessage(&tmsg) != -1)
                handleMessageU(&tmsg);
        }
        unblocksignal(SIGUSR1); /* Prevenzione inconsistenza budget con transazioni manuali */
    }
    tmsg.object = TMEX_USER_EXIT;
    tmsg.value = myIndex;
    sendtmessage(tmsg, 0, TO_MSTR);
    exit(EXIT_SUCCESS);
}

static void handleMessageU(tmessage *tmsg)
{
    extern int *nodemsgids;
    switch ((*tmsg).object)
    {
    case TMEX_TP_FULL:
    {
        removePending((*tmsg).transaction);
        break;
    }
    case TMEX_NEW_BLOCK:
    {
        break;
    }
    case TMEX_NEW_NODE:
    {
        nodemsgids = reallocarray(nodemsgids, nodesNumber + 1, sizeof(int));
        /* Il messaggio contiene il msgid del nuovo nodo in friends, incrementiamo pure il contatore dei nodi per poter ottenere
             * l'indice della nuova msgid dalla selezione casuale dell'indice di riferimento ai nodi che ricevono le nostre
             * transazioni */
        nodemsgids[nodesNumber++] = tmsg->value;
        break;
    }
    default:
    {
        fprintf(stderr, "Unknown msg type @ UserHandleMessage\n");
        break;
    }
    }
}

pid_t spawnUser(int loopIndex)
{
    pid_t fValue = fork();
    myIndex = loopIndex;
    if (fValue == 0)
        userRoutine();
    else
        return fValue;
}

static tmessage genRandomTmsg(int budget)
{
    int tmp;
    int tmpID;
    tmessage tmsg;
    tmsg.object = TMEX_PROCESS_RQST;
    /* Il totale che spenderemo per la transazione */
    tmp = (int)so_random(2, budget + 1);
    /* Un utente diverso da noi che riceverà i soldi*/
    do
    {
        tmpID = so_random(0, conf.USERS_NUM);
    } while (tmpID == myIndex);
    tmsg.transaction.timestamp = time(NULL);
    tmsg.transaction.sender = myIndex;
    tmsg.transaction.receiver = tmpID;
    tmsg.transaction.reward = tmp * conf.REWARD / 100;
    if (tmsg.transaction.reward < 1)
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
    while (tptr != NULL)
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
    if (lastBlock != mbook->n_blocks)
    {
        for (i = lastBlock; i < mbook->n_blocks; i++)
        {
            for (j = 0; j < conf.BLOCK_SIZE - 1; j++)
            {
                if (mbook->blocks[i][j].sender == myIndex)
                {
                    bookBudgetReport -= (mbook->blocks[i][j].quantity + mbook->blocks[i][j].reward);
                    removePending(mbook->blocks[i][j]);
                }
                else if (mbook->blocks[i][j].receiver == myIndex)
                    bookBudgetReport += (mbook->blocks[i][j].quantity);
            }
        }
        lastBlock = mbook->n_blocks;
    }
    endbookread();
    return conf.BUDGET_INIT + bookBudgetReport - pendingBudget();
}

static void addPending(transaction t)
{
    tnode *tptr;
    tnode *node = malloc(sizeof(tnode));
    node->next = NULL;
    node->t = t;
    if (pendingList == NULL)
    {
        pendingList = node;
        return;
    }
    tptr = pendingList;
    while (tptr->next != NULL)
        tptr = tptr->next;
    tptr->next = node;
}

static int transeq(transaction t1, transaction t2)
{
    if (t1.timestamp == t2.timestamp && t1.sender == t2.sender && t1.receiver == t2.receiver)
        return 1;
    return 0;
}

static void removePending(transaction t)
{
    tnode *tptr, *prev;
    if (pendingList == NULL)
        return;
    /* Se è uguale alla testa della lista, cancelliamo la testa dalla lista e settiamo la testa al next */
    if (transeq(pendingList->t, t))
    {
        tptr = pendingList->next;
        free(pendingList);
        pendingList = tptr;
        return;
    }
    prev = pendingList;
    for (tptr = pendingList->next; tptr != NULL; tptr = tptr->next)
    {
        if (transeq(tptr->t, t))
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
    if (myBudget >= 2)
    {
        /* Avendo budget >= 2, generiamo una transazione*/
        tmsg = genRandomTmsg(myBudget);
        /* Inviamo al nodo estratto la transazione e attendiamo un intervallo di tempo casuale */
        if (trysendtmessage(tmsg, (int)so_random(0, nodesNumber), TO_NODE) != -1)
            addPending(tmsg.transaction);
        nsleep(so_random(conf.MIN_TRANS_GEN_NSEC, conf.MAX_TRANS_GEN_NSEC));
    }
}

static void setupUserHandlers()
{
    signal(SIGUSR1, sendManualTransaction);
}