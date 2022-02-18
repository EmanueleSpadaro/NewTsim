#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "shared/so_conf.h"
#include "shared/so_ipc.h"
#include "shared/so_random.h"
#include "node.h"
#include "user.h"

/* Macro presa da StackOverflow */
#define refreshscreen() printf("\033[1;1H\033[2J");
#define MAXUSERTHRESHOLD 10 /* Se vi sono più di questi utenti, stampiamo solo parzialmente gli utenti */
#define MAXRICHESTUSERS 3   /* Numero di utenti più ricchi da stampare se vi sono troppi utenti */
#define MAXPOORUSERS 3      /* Numero di utenti più poveri da stampare se vi sono troppi utenti */

/* Variabili globali accessibili da tutti i moduli */
conf_t conf;
masterbook *mbook;
extern pid_t *userPIDs;
pid_t *nodePIDs;
int nodesNumber;
char sigint = 0;

/* Struttura per agevolare la gestione delle informazioni di uscita ed evitare due array paralleli */
struct node_exit_info
{
    pid_t pid; /* Il pid del nodo */
    int tpnum; /* Il numero di transazioni che conteneva all'uscita */
};

/* Array per gestire la stampa  */
static int userExitCounter = 0;
static int *user_budgets, *node_budgets;
/* Questi vengono utilizzati e allocati solo ed esclusivamente se vi sono troppi utenti per poter stampare in modo
 * leggibile le informazioni della simulazione a runtime */
static int *richArr, *poorArr, *richArrIDs, *poorArrIDs;

static struct node_exit_info *node_exit_info;
static void altersighandlers();
static void printstatus(int secs, char total);
static void handleMessageM(tmessage *tmsgptr);
static void simterm();

int main(int argc, char const *argv[])
{
    int i;
    tmessage tmsg; /* Control Message used to eventually handle notification messages from users / node */
    conf = loadconf(argc, argv);
    printconf(conf);
    puts("Premere un qualsiasi tasto per iniziare la simulazione, altrimenti uscire con CTRL+C");
    getc(stdin);

    altersighandlers();
    srandom(time(NULL) ^ (getpid() << 16));
    /* Se gli utenti impongono di stampare valori parziali della simulazione, allochiamo lo spazio */
    if (conf.USERS_NUM > MAXUSERTHRESHOLD)
    {
        richArr = calloc(MAXRICHESTUSERS, sizeof(int));
        richArrIDs = calloc(MAXRICHESTUSERS, sizeof(int));
        poorArr = calloc(MAXPOORUSERS, sizeof(int));
        poorArrIDs = calloc(MAXPOORUSERS, sizeof(int));
    }

    /* Allochiamo nella HEAP i nodi considerando che loro possono effettivamente aumentare */
    nodePIDs = calloc(conf.NODES_NUM, sizeof(pid_t));
    nodesNumber = conf.NODES_NUM;

    if (initipcs())
        fprintf(stderr, "Error while initializing IPCs\n");

    for (i = 0; i < nodesNumber; i++)
        nodePIDs[i] = spawnNode(i);
    for (i = 0; i < nodesNumber; i++)
        sendFriendsTo(i);

    for (i = 0; i < conf.USERS_NUM; i++)
        userPIDs[i] = spawnUser(i);

    startsyncsem();

    user_budgets = calloc(conf.USERS_NUM, sizeof(int));
    for (i = 0; i < conf.USERS_NUM; i++)
        user_budgets[i] = conf.BUDGET_INIT;
    /* Verrà riallocato a dovere, se venisse aggiunto un nuovo nodo, dall'handler */
    node_budgets = calloc(conf.NODES_NUM, sizeof(int));
    /* Non è necessario inializzarlo a 0 perché calloc ritorna una zeroed-memory */

    /* La simulazione è pronta per partire con tutti gli utenti e i nodi */
    for (i = 0; i < conf.SIM_SEC && !sigint && userExitCounter < conf.USERS_NUM; i++)
    {
        waitbookread();
        printstatus(i, 0);
        if (mbook->n_blocks == conf.REGISTRY_SIZE)
            break;
        endbookread();
        while (checktmessage(&tmsg, TMEX_USER_EXIT) != -1)
            handleMessageM(&tmsg);
        while (checktmessage(&tmsg, TMEX_HOPS_ZERO) != -1)
            handleMessageM(&tmsg);
        nsleep(1000000000);
    }
    printstatus(i, 1);
    simterm();
    if (i == conf.SIM_SEC)
        puts("Termination occurred because of maximum time elapsed");
    if (sigint)
        puts("Termination called prematurely by user");
    if (mbook->n_blocks == conf.REGISTRY_SIZE)
        puts("Termination occurred because of full masterbook");
    if (userExitCounter == conf.USERS_NUM)
        puts("Termination occurred because of all users exit");
    /* Stampo i valori delle transaction pool dei nodi */
    for (i = 0; i < nodesNumber; i++)
        printf("[Node %d] Exiting with %i/%i/%i transactions in pool\n",
               node_exit_info[i].pid, node_exit_info[i].tpnum, conf.BLOCK_SIZE, conf.TP_SIZE);
    printf("%i/%i users exited prematurely\n", userExitCounter, conf.USERS_NUM);

    for (i = 0; i < nodesNumber + conf.USERS_NUM; i++)
        wait(NULL);
    puts("[MASTER] Clearing IPCs and exiting...");
    if (releaseipcs())
        fprintf(stderr, "Error while cleaning IPCs\n");
    return 0;
}

static void multipurposeMasterHandler(int snum)
{
    switch (snum)
    {
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

static int computeblockbudget(int index, int blockNumber, int toConst)
{
    int i;
    int sum = 0;
    /* La conf.BLOCK_SIZE-1esima è sempre di reward per nodi */
    if (toConst == TO_USER)
    {
        for (i = 0; i < conf.BLOCK_SIZE - 1; i++)
        {
            if (mbook->blocks[blockNumber][i].sender == index)
                user_budgets[index] -= (mbook->blocks[blockNumber][i].quantity + mbook->blocks[blockNumber][i].reward);
            else if (mbook->blocks[blockNumber][i].receiver == index)
                user_budgets[index] += (mbook->blocks[blockNumber][i].quantity);
        }
        return user_budgets[index];
    }
    else if (toConst == TO_NODE)
    {
        if (mbook->blocks[blockNumber][conf.BLOCK_SIZE - 1].receiver == index)
            node_budgets[index] += mbook->blocks[blockNumber][conf.BLOCK_SIZE - 1].quantity;
        return node_budgets[index];
    }
    else
    {
        fprintf(stderr, "Invalid toConst passed to computeblockbudget@Master\n index: %i blockn: %i toConst: %i\n", index, blockNumber, toConst);
        return -1;
    }
}

static void printstatus(int secs, char total)
{
    /* Last Block processes to avoid computing again same block results */
    int i, j, tmp, tmpindex;
    static int lastBlock = 0;
    if (lastBlock != mbook->n_blocks)
    {
        for (i = lastBlock; i < mbook->n_blocks; i++)
        {
            for (j = 0; j < conf.USERS_NUM; j++)
                computeblockbudget(j, i, TO_USER);
            for (j = 0; j < nodesNumber; j++)
                computeblockbudget(j, i, TO_NODE);
        }
        lastBlock = i;
    }
    refreshscreen()
        printf("====%d/%d==== %d BLOCKS PROCESSED =============\n", secs, conf.SIM_SEC, lastBlock);
    printf("=== Users online %i/%i | Nodes online: %i ===\n", conf.USERS_NUM - userExitCounter, conf.USERS_NUM,
           nodesNumber);
    if (conf.USERS_NUM <= MAXUSERTHRESHOLD || total)
    {
        for (i = 0; i < conf.USERS_NUM; i++)
        {
            printf("[USER %d] Accounted Balance: %d\n", userPIDs[i], user_budgets[i]);
        }
    }
    else
    {
        /* Con questi teniamo conto del più povero per poter avere i più ricchi nell'array */
        tmp = user_budgets[0];
        tmpindex = 0;
        /* Inizialmente riempiamo l'array con MAXRICHESTUSERS, di base inizialmente sono "i più ricchi" */
        for (i = 0; i < MAXRICHESTUSERS; i++)
        {
            richArrIDs[i] = i;
            richArr[i] = user_budgets[i];
            /* Vogliamo assicurarci di puntare sempre all'elemento più povero per avere un array con i più ricchi */
            if (tmp > richArr[i])
            {
                tmp = user_budgets[i];
                tmpindex = i;
            }
        }
        /* Ora scorriamo i rimanenti utenti della configurazione e se ne troviamo uno più ricco del più povero
         * swappiamo i valori e scorriamo l'array per ripuntare a quello più povero */
        for (; i < conf.USERS_NUM; i++)
        {
            /* Se è più ricco del più povero nell'array, sostituiamo con i suoi valori e otteniamo quello più povero */
            if (user_budgets[i] > tmp)
            {
                richArrIDs[tmpindex] = i;
                richArr[tmpindex] = user_budgets[i];
                /* Non necessitiamo di far puntare tmpindex ad i, perché lo fa già dato lo swap, ma solo il budget */
                tmp = richArr[tmpindex];
                /* Vogliamo assicurarci di far puntare tmp e tmpindex ai valori del più povero */
                for (j = 0; j < MAXRICHESTUSERS; j++)
                {
                    if (tmp > richArr[j])
                    {
                        tmp = richArr[j];
                        tmpindex = j;
                    }
                }
            }
        }
        /* Ora li riordino dal più ricco in posizione zero al più povero in posizione MAXRICHESTUSERS-1 */
        for (i = 0; i < MAXRICHESTUSERS; i++)
        {
            for (j = 0; j < MAXRICHESTUSERS; j++)
            {
                if (richArr[i] > richArr[j])
                {
                    tmp = richArr[i];
                    richArr[i] = richArr[j];
                    richArr[j] = tmp;
                    tmp = richArrIDs[i];
                    richArrIDs[i] = richArrIDs[j];
                    richArrIDs[j] = tmp;
                }
            }
        }
        /* Con questi teniamo conto del più ricco per poter avere i più poveri nell'array */
        tmp = user_budgets[0];
        tmpindex;
        /* Inizialme    nte riempiamo l'arra        y con MAXPOORUSERS,  di base inizialmente sono "i più poveri" */
        for (i = 0; i < MAXPOORUSERS; i++)
        {
            poorArrIDs[i] = i;
            poorArr[i] = user_budgets[i];
            /* Vogliamo assicurarci di puntare sempre all'elemento più ricco per avere un array con i più poveri */
            if (tmp < poorArr[i])
            {
                tmp = user_budgets[i];
                tmpindex = i;
            }
        }
        /* Ora scorriamo i rimanenti utenti della configurazione e se ne troviamo uno più povero del più ricco
         * swappiamo i valori e scorriamo l'array per ripuntare a quello più ricco */
        for (; i < conf.USERS_NUM; i++)
        {
            /* Se è più povero del più ricco nell'array, sostituiamo con i suoi valori e otteniamo quello più ricco */
            if (user_budgets[i] < tmp)
            {
                poorArrIDs[tmpindex] = i;
                poorArr[tmpindex] = user_budgets[i];
                /* Non necessitiamo di far puntare tmpindex ad i, perché lo fa già dato lo swap, ma solo il budget */
                tmp = poorArr[tmpindex];
                /* Vogliamo assicurarci di far p    unta    re tmp e tmpindex ai valori del più ricco */
                for (j = 0; j < MAXPOORUSERS; j++)
                {
                    if (tmp < poorArr[j])
                    {
                        tmp = poorArr[j];
                        tmpindex = j;
                    }
                }
            }
        }
        /* Ora li riordino dal più ricco         in posizione zero al più povero in posizione MAXPOORUSERS- 1 */
        for (i = 0; i < MAXPOORUSERS; i++)
        {
            for (j = 0; j < MAXPOORUSERS; j++)
            {
                if (poorArr[i] > poorArr[j])
                {
                    tmp = poorArr[i];
                    poorArr[i] = poorArr[j];
                    poorArr[j] = tmp;
                    tmp = poorArrIDs[i];
                    poorArrIDs[i] = poorArrIDs[j];
                    poorArrIDs[j] = tmp;
                }
            }
        }
        puts("");
        for (i = 0; i < MAXRICHESTUSERS; i++)
            printf("[USER %d] Accounted Balance: %d\n", userPIDs[richArrIDs[i]], richArr[i]);
        puts("\n\t\t(...)\n");
        for (i = 0; i < MAXPOORUSERS; i++)
            printf("[USER %d] Accounted Balance: %d\n", userPIDs[poorArrIDs[i]], poorArr[i]);
    }
    printf("\n");
    if (!total)
    {
        for (i = 0; i < conf.NODES_NUM; i++)
            printf("[NODE %d] Accounted Balance: %d\n", nodePIDs[i], node_budgets[i]);
        if (nodesNumber > conf.NODES_NUM)
            printf("\n(%i more nodes not printed for readability...)\n", nodesNumber - conf.NODES_NUM);
    }
    else
    {
        for (i = 0; i < nodesNumber; i++)
            printf("[NODE %d] Accounted Balance: %d\n", nodePIDs[i], node_budgets[i]);
    }
    printf("=====================================================\n");
}

static void simterm()
{
    int i, xnodecount;
    tmessage tmsg;
    for (i = 0; i < conf.USERS_NUM; i++)
        kill(userPIDs[i], SIGKILL);
    for (i = 0; i < nodesNumber; i++)
        kill(nodePIDs[i], SIGINT);
    xnodecount = 0;
    node_exit_info = calloc(nodesNumber, sizeof(struct node_exit_info));
    while (xnodecount < nodesNumber)
    {
        waittmessage(&tmsg, 0);
        if (tmsg.object == TMEX_NODE_EXIT_INFO)
        {
            node_exit_info[xnodecount].pid = tmsg.transaction.sender;
            node_exit_info[xnodecount++].tpnum = tmsg.value;
        }
    }
}

static void handleMessageM(tmessage *tmsgptr)
{
    extern int *nodemsgids;
    int i, j, notifiedCtr, newmsgqid;
    pid_t *alreadyNotified;
    pid_t tmpindex;
    switch (tmsgptr->object)
    {
    /* Incrementiamo il contatore degli utenti usciti */
    case TMEX_USER_EXIT:
    {
        userExitCounter++;
        break;
    }
    /* Creiamo un nuovo nodo, notifichiamo gli utenti della sua esistenza, imponiamo a SO_NUM_FRIENDS di
         * aggiungerlo come tale. */
    case TMEX_HOPS_ZERO:
    {
        /* Rialloco per aver una cella in più nell'array dei nodi per contenere quello nuovo*/
        nodePIDs = reallocarray(nodePIDs, nodesNumber + 1, sizeof(pid_t));
        nodemsgids = reallocarray(nodemsgids, nodesNumber + 1, sizeof(int));
        node_budgets = reallocarray(node_budgets, nodesNumber + 1, sizeof(int));

        newmsgqid = allocnewmsgq();
        nodemsgids[nodesNumber] = newmsgqid;
        node_budgets[nodesNumber] = 0;
        /* Per mantenere la consistenza di nodesNumber e permettere agevolmente di ricevere amici anche al nuovo
             * nodo, incremento in modo fittizio nodesNumber per far si che spawnNode permetta al nuovo nodo di avere
             * nodesNumber effettivamente consistente con quello che sarà della simulazione dopo la sua creazione */
        nodesNumber += 1;
        nodePIDs[nodesNumber - 1] = spawnNode(nodesNumber - 1);
        /* Riporto nodesNumber ad un valore che ci permette di usarlo come riferimento alla nuova istanza di nodo */
        nodesNumber -= 1;
        /* Gli invio NUM_FRIENDS nodi come amici */
        sendFriendsTo(nodesNumber);
        /* Gli invio la transazione da gestire, sarà il primo messaggio a lui destinato e di conseguenza sarà
             * effettivamente la prima transazione che avrà nella Transaction Pool come da richiesta */
        tmsgptr->object = TMEX_PROCESS_RQST;
        sendtmessage(*tmsgptr, nodesNumber, TO_NODE);

        /* Notifico SO_NUM_FRIENDS nodi diversi di aggiungere il nuovo arrivato alla loro lista di amici */
        /* Preparo un array sufficientemente grande per evitare doppie inclusioni da parte dello stesso nodo */
        alreadyNotified = calloc(conf.NUM_FRIENDS, sizeof(int));
        notifiedCtr = 0;
        /* Inviamo nel campo value la msgid del nuovo arrivato */
        tmsgptr->object = TMEX_NEW_NODE;
        tmsgptr->value = newmsgqid;
        for (i = 0; i < conf.NUM_FRIENDS; i++)
        {
            /* NodesNumber non è ancora incrementato, quindi otteniamo solo un nodo tra quelli già preesistenti */
            tmpindex = (int)so_random(0, nodesNumber);
            for (j = 0; j < notifiedCtr; j++)
                if (alreadyNotified[j] == tmpindex)
                    break;
            /* Se j è uguale al numero di notificati, allora l'id generato non è stato ancora notificato */
            /* In questo modo evitiamo che un nodo preesistente aggiunga due volte il nuovo arrivato agli amici */
            if (j == notifiedCtr)
            {
                tmsgptr->transaction.receiver = nodePIDs[tmpindex];
                sendtmessage(*tmsgptr, tmpindex, TO_NODE);
                alreadyNotified[notifiedCtr++] = tmpindex;
            }
            else
                i--;
        }
        free(alreadyNotified); /* Disalloco l'array allocato per gestire le duplicazioni di segnale */

        /* Una volta notificati i nodi di aggiungere il nuovo arrivato alla lista degli amici, informiamo gli utenti */
        tmsgptr->object = TMEX_NEW_NODE;
        tmsgptr->value = newmsgqid;
        for (i = 0; i < conf.USERS_NUM; i++)
            trysendtmessage(*tmsgptr, i, TO_USER);
        /* Aggiorno il contatore dei nodi online nella simulazione finalmente */
        nodesNumber++;
        break;
    }
    }
}