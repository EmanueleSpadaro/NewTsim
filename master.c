#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "shared/so_conf.h"
#include "shared/so_ipc.h"
#include "shared/so_random.h"
#include "node.h"
#include "user.h"

/* Macro taken from StackOverflow */
#define refreshscreen() printf("\033[1;1H\033[2J");
#define MAXUSERTHRESHOLD    10  /* Se vi sono più di questi utenti, stampiamo solo parzialmente gli utenti */
#define MAXRICHESTNODES     3   /* Numero di utenti più ricchi da stampare se vi sono troppi utenti */
#define MAXPOORNODES        3   /* Numero di utenti più poveri da stampare se vi sono troppi utenti */

/* Global variables shared across modules */
conf_t conf;
masterbook *mbook;
extern pid_t *userPIDs;
pid_t *nodePIDs;
int nodesNumber;
char sigint = 0;

/* Struttura per agevolare la gestione delle informazioni di uscita ed evitare due array paralleli */
struct node_exit_info
{
    pid_t pid;  /* Il pid del nodo */
    int tpnum;  /* Il numero di transazioni che conteneva all'uscita */
};

/* Array per gestire la stampa  */
static int userExitCounter = 0;
static int *user_budgets, *node_budgets;
/* Questi vengono utilizzati e allocati solo ed esclusivamente se vi sono troppi utenti per poter stampare in modo
 * leggibile le informazioni della simulazione a runtime */
static int *richArr, *poorArr;
static pid_t *richArrPids, *poorArrPids;

static struct node_exit_info *node_exit_info;
static void altersighandlers();
static void printstatus(int secs);
static void handleMessageM(cmessage *cmsgptr);
static void simterm();

int main(int argc, char const *argv[]) {
    int i;
    cmessage cmsg; /* Control Message used to eventually handle notification messages from users / node */
    conf = loadconf(argc, argv);
    printconf(conf);
    puts("Premere un qualsiasi tasto per iniziare la simulazione, altrimenti uscire con CTRL+C");
    getc(stdin);

    altersighandlers();
    srandom(time(NULL) ^ (getpid()<<16));
    /* Se gli utenti impongono di stampare valori parziali della simulazione, allochiamo lo spazio */
    if(conf.USERS_NUM > MAXUSERTHRESHOLD)
    {
        richArr = calloc(MAXRICHESTNODES, sizeof(int));
        richArrPids = calloc(MAXRICHESTNODES, sizeof(pid_t));
        poorArr = calloc(MAXPOORNODES, sizeof(int));
        poorArrPids = calloc(MAXPOORNODES, sizeof(pid_t));
    }

    /* Allochiamo nella HEAP i nodi considerando che loro possono effettivamente aumentare */
    nodePIDs = calloc(conf.NODES_NUM, sizeof(pid_t));
    nodesNumber = conf.NODES_NUM;

    if(initipcs())
        fprintf(stderr, "Error while initializing IPCs\n");

    for( i = 0; i < nodesNumber; i++)
        nodePIDs[i] = spawnNode(i);
    for( i = 0; i < nodesNumber; i++)
        sendFriendsTo(nodePIDs[i], i);

    for( i = 0; i < conf.USERS_NUM; i++)
        userPIDs[i] = spawnUser();
    /* Stampo i pid di tutti i nodi e degli utenti */
    for ( i = 0; i < nodesNumber; i++)
        printf("N%i ", nodePIDs[i]);
    for ( i = 0; i < conf.USERS_NUM; i++)
        printf("U%i ", userPIDs[i]);
    puts("");


    startsyncsem();

    user_budgets = calloc(conf.USERS_NUM, sizeof(int));
    for(i = 0; i < conf.USERS_NUM; i++)
        user_budgets[i] = conf.BUDGET_INIT;
    /* Verrà riallocato a dovere, se venisse aggiunto un nuovo nodo, dall'handler */
    node_budgets = calloc(conf.NODES_NUM, sizeof(int));
    /* Non è necessario inializzarlo a 0 perché calloc ritorna una zeroed-memory */


    /* La simulazione è pronta per partire con tutti gli utenti e i nodi */
    for(i = 0; i < conf.SIM_SEC && !sigint && userExitCounter < conf.USERS_NUM; i++)
    {
        waitbookread();
        printstatus(i);
        if(mbook->n_blocks == conf.REGISTRY_SIZE)
            break;
        endbookread();
        while(checkcmessage(&cmsg) != -1)
            handleMessageM(&cmsg);
        nsleep(1000000000);
    }
    printstatus(i);
    simterm();
    if(i == conf.SIM_SEC)
        puts("Termination occurred because of maximum time elapsed");
    if(sigint)
        puts("Termination called prematurely by user");
    if(mbook->n_blocks == conf.REGISTRY_SIZE)
        puts("Termination occurred because of full masterbook");
    if(userExitCounter == conf.USERS_NUM)
        puts("Termination occurred because of all users exit");
    /* Stampo i valori delle transaction pool dei nodi */
    for(i = 0; i < nodesNumber; i++)
        printf("[Node %d] Exiting with %i/%i/%i transactions in pool\n",
               node_exit_info[i].pid, node_exit_info[i].tpnum, conf.BLOCK_SIZE, conf.TP_SIZE);
    printf("%i/%i users exited prematurely\n", userExitCounter, conf.USERS_NUM);









    for( i = 0; i < nodesNumber + conf.USERS_NUM; i++)
        wait(NULL);
    puts("[MASTER] Clearing IPCs and exiting...");
    if(releaseipcs())
        fprintf(stderr, "Error while cleaning IPCs\n");
    return 0;
}

static void multipurposeMasterHandler(int snum)
{
    switch (snum) {
        /* Interrompiamo la simulazione prematuramente */
        case SIGINT:
        {
            sigint = 1;
            break;
        }
        default:
        {
            fprintf(stderr, "[MASTER] Unknown signal #%i received\n", snum);
            break;
        }
    }
}
static void altersighandlers()
{
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = multipurposeMasterHandler;
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
}

static int getblockbudget(pid_t ofwho, transaction *block)
{
    int i;
    int sum = 0;
    for(i = 0; i < conf.BLOCK_SIZE; i++)
    {
        if(block[i].receiver == ofwho)
            sum += block[i].quantity;
        if(block[i].sender == ofwho)
            sum -= (block[i].quantity + block[i].reward);
    }
    return sum;
}

static void printstatus(int secs)
{
    /* Last Block processes to avoid computing again same block results */
    int i, j, tmp, tmpindex;
    static int lastBlock = 0;
    if(lastBlock != mbook->n_blocks)
    {
        for(i = lastBlock; i < mbook->n_blocks; i++)
        {
            for(j = 0; j < conf.USERS_NUM; j++)
                user_budgets[j] += getblockbudget(userPIDs[j], mbook->blocks[i]);
            for(j = 0; j < nodesNumber; j++)
                node_budgets[j] += getblockbudget(nodePIDs[j], mbook->blocks[i]);
        }
        lastBlock = i;
    }
    refreshscreen()
    printf("====%d/%d==== %d BLOCKS PROCESSED =============\n", secs, conf.SIM_SEC, lastBlock);
    printf("=== Users online %i/%i | Nodes online: %i ===\n", conf.USERS_NUM - userExitCounter, conf.USERS_NUM,
           nodesNumber);
    if(conf.USERS_NUM <= MAXUSERTHRESHOLD)
    {
        for(i = 0; i < conf.USERS_NUM; i++)
        {
            printf("[USER %d] Accounted Balance: %d\n", userPIDs[i], user_budgets[i]);
        }
        printf("\n");
        for(i = 0; i < conf.NODES_NUM; i++)
        {
            printf("[NODE %d] Accounted Balance: %d\n", nodePIDs[i], node_budgets[i]);
        }
    }
    else {
        /* Con questi teniamo conto del più povero per poter avere i più ricchi nell'array */
        tmp = user_budgets[0];
        tmpindex = 0;
        /* Inizialmente riempiamo l'array con MAXRICHESTNODES, di base inizialmente sono "i più ricchi" */
        for (i = 0; i < MAXRICHESTNODES; i++) {
            richArrPids[i] = userPIDs[i];
            richArr[i] = user_budgets[i];
            /* Vogliamo assicurarci di puntare sempre all'elemento più povero per avere un array con i più ricchi */
            if (tmp > richArr[i]) {
                tmp = user_budgets[i];
                tmpindex = i;
            }
        }
        /* Ora scorriamo i rimanenti utenti della configurazione e se ne troviamo uno più ricco del più povero
         * swappiamo i valori e scorriamo l'array per ripuntare a quello più povero */
        for (; i < conf.USERS_NUM; i++) {
            /* Se è più ricco del più povero nell'array, sostituiamo con i suoi valori e otteniamo quello più povero */
            if (user_budgets[i] > tmp) {
                richArrPids[tmpindex] = userPIDs[i];
                richArr[tmpindex] = user_budgets[i];
                /* Non necessitiamo di far puntare tmpindex ad i, perché lo fa già dato lo swap, ma solo il budget */
                tmp = richArr[tmpindex];
                /* Vogliamo assicurarci di far puntare tmp e tmpindex ai valori del più povero */
                for (j = 0; j < MAXRICHESTNODES; j++) {
                    if (tmp > richArr[j]) {
                        tmp = richArr[j];
                        tmpindex = j;
                    }
                }
            }
        }
        /* Ora li riordino dal più ricco in posizione zero al più povero in posizione MAXRICHESTNODES-1 */
        for (i = 0; i < MAXRICHESTNODES; i++) {
            for (j = 0; j < MAXRICHESTNODES; j++) {
                if (richArr[i] > richArr[j]) {
                    tmp = richArr[i];
                    richArr[i] = richArr[j];
                    richArr[j] = tmp;
                    tmp = richArrPids[i];
                    richArrPids[i] = richArrPids[j];
                    richArrPids[j] = tmp;
                }
            }
        }
        /* Con questi teniamo conto del più ricco per poter avere i più poveri nell'array */
        tmp = user_budgets[0];
        tmpindex = 0;
        /* Inizialmente riempiamo l'array con MAXPOORNODES, di base inizialmente sono "i più poveri" */
        for (i = 0; i < MAXPOORNODES; i++) {
            poorArrPids[i] = userPIDs[i];
            poorArr[i] = user_budgets[i];
            /* Vogliamo assicurarci di puntare sempre all'elemento più ricco per avere un array con i più poveri */
            if (tmp < poorArr[i]) {
                tmp = user_budgets[i];
                tmpindex = i;
            }
        }
        /* Ora scorriamo i rimanenti utenti della configurazione e se ne troviamo uno più povero del più ricco
         * swappiamo i valori e scorriamo l'array per ripuntare a quello più ricco */
        for (; i < conf.USERS_NUM; i++) {
            /* Se è più povero del più ricco nell'array, sostituiamo con i suoi valori e otteniamo quello più ricco */
            if (user_budgets[i] < tmp) {
                poorArrPids[tmpindex] = userPIDs[i];
                poorArr[tmpindex] = user_budgets[i];
                /* Non necessitiamo di far puntare tmpindex ad i, perché lo fa già dato lo swap, ma solo il budget */
                tmp = poorArr[tmpindex];
                /* Vogliamo assicurarci di far puntare tmp e tmpindex ai valori del più ricco */
                for (j = 0; j < MAXPOORNODES; j++) {
                    if (tmp < poorArr[j]) {
                        tmp = poorArr[j];
                        tmpindex = j;
                    }
                }
            }
        }
        /* Ora li riordino dal più ricco in posizione zero al più povero in posizione MAXPOORNODES-1 */
        for (i = 0; i < MAXPOORNODES; i++) {
            for (j = 0; j < MAXPOORNODES; j++) {
                if (poorArr[i] > poorArr[j]) {
                    tmp = poorArr[i];
                    poorArr[i] = poorArr[j];
                    poorArr[j] = tmp;
                    tmp = poorArrPids[i];
                    poorArrPids[i] = poorArrPids[j];
                    poorArrPids[j] = tmp;
                }
            }
        }
        puts("");
        for (i = 0; i < MAXRICHESTNODES; i++)
            printf("[USER %d] Accounted Balance: %d\n", richArrPids[i], richArr[i]);
        puts("\n\t\t(...)\n");
        for (i = 0; i < MAXPOORNODES; i++)
            printf("[USER %d] Accounted Balance: %d\n", poorArrPids[i], poorArr[i]);
    }
    printf("=====================================================\n");
}

static void simterm()
{
    int i, xnodecount;
    cmessage cmsg;
    for(i = 0; i < conf.USERS_NUM; i++)
        kill(userPIDs[i], SIGKILL);
    for(i = 0; i < nodesNumber; i++)
        kill(nodePIDs[i], SIGINT);
    xnodecount = 0;
    node_exit_info = calloc(nodesNumber, sizeof(int));
    while(xnodecount < nodesNumber)
    {
        waitcmessage(&cmsg);
        if(cmsg.object == CMEX_NODE_EXIT_INFO)
        {
            node_exit_info[xnodecount].pid = cmsg.transaction.sender;
            node_exit_info[xnodecount++].tpnum = cmsg.friend;
        }

    }
}

static void handleMessageM(cmessage *cmsgptr)
{
    extern int *nodemsgids;
    int i, j, notifiedCtr, newmsgqid;
    pid_t *alreadyNotified;
    pid_t tmpindex;
    tmessage tmsg;
    cmessage cmsg;
    switch (cmsgptr->object) {
        /* Incrementiamo il contatore degli utenti usciti */
        case CMEX_USER_EXIT:
        {
            userExitCounter++;
            break;
        }
        /* Creiamo un nuovo nodo, notifichiamo gli utenti della sua esistenza, imponiamo a SO_NUM_FRIENDS di
         * aggiungerlo come tale. */
        case CMEX_HOPS_ZERO:
        {
            /* Rialloco per aver una cella in più nell'array dei nodi per contenere quello nuovo*/
            nodePIDs =  reallocarray(nodePIDs, nodesNumber+1, sizeof(pid_t));
            nodemsgids = reallocarray(nodemsgids, nodesNumber+1, sizeof(int));
            newmsgqid = allocnewmsgq();
            nodemsgids[nodesNumber] = newmsgqid;
            /* Per mantenere la consistenza di nodesNumber e permettere agevolmente di ricevere amici anche al nuovo
             * nodo, incremento in modo fittizio nodesNumber per far si che spawnNode permetta al nuovo nodo di avere
             * nodesNumber effettivamente consistente con quello che sarà della simulazione dopo la sua creazione */
            nodesNumber += 1;
            nodePIDs[nodesNumber-1] = spawnNode(nodesNumber-1);
            /* Riporto nodesNumber ad un valore che ci permette di usarlo come riferimento alla nuova istanza di nodo */
            nodesNumber -= 1;
            /* Gli invio NUM_FRIENDS nodi come amici */
            for(i = 0; i < conf.NUM_FRIENDS; i++)
                sendFriendsTo(nodePIDs[nodesNumber], nodesNumber);
            /* Gli invio la transazione da gestire, sarà il primo messaggio a lui destinato e di conseguenza sarà
             * effettivamente la prima transazione che avrà nella Transaction Pool come da richiesta */
            tmsg.object = TMEX_PROCESS_RQST;
            sendtmessage(tmsg, nodesNumber);

            /* Notifico SO_NUM_FRIENDS nodi diversi di aggiungere il nuovo arrivato alla loro lista di amici */
            /* Preparo un array sufficientemente grande per evitare doppie inclusioni da parte dello stesso nodo */
            alreadyNotified = calloc(conf.NUM_FRIENDS, sizeof(pid_t));
            notifiedCtr = 0;
            /* Inviamo nel campo hops la msgid del nuovo arrivato */
            tmsg.object = TMEX_NEW_NODE;
            tmsg.hops = newmsgqid;
            for(i = 0; i < conf.NUM_FRIENDS; i++){
                /* NodesNumber non è ancora incrementato, quindi otteniamo solo un nodo tra quelli già preesistenti */
                tmpindex = (int)so_random(0, nodesNumber);
                for(j = 0; j < notifiedCtr; j++)
                    if(alreadyNotified[j] == tmpindex)
                        break;
                /* Se j è uguale al numero di notificati, allora il pid generato non è stato ancora notificato */
                /* In questo modo evitiamo che un nodo preesistente aggiunga due volte il nuovo arrivato agli amici */
                if(j == notifiedCtr)
                {
                    sendtmessage(tmsg, tmpindex);
                    alreadyNotified[notifiedCtr++] = tmpindex;
                }
                else
                    i--;
            }
            free(alreadyNotified); /* Disalloco l'array allocato per gestire le duplicazioni di segnale */

            /* Una volta notificati i nodi di aggiungere il nuovo arrivato alla lista degli amici, informiamo gli utenti */
            cmsg.object = CMEX_NEW_NODE;
            cmsg.friend = newmsgqid;
            for(i = 0; i < conf.USERS_NUM; i++)
            {
                cmsg.recipient = userPIDs[i];
                sendcmessage(cmsg);
            }
            /* Aggiorno il contatore dei nodi online nella simulazione finalmente */
            nodesNumber++;
            break;
        }
    }
}