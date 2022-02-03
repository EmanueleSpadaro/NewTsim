#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "shared/so_conf.h"
#include "shared/so_ipc.h"
#include "shared/so_random.h"
#include "node.h"
#include "user.h"

/* Global variables shared across modules */
conf_t conf;
masterbook *mbook;
extern pid_t *userPIDs;
pid_t *nodePIDs;
int nodesNumber;
char sigint = 0;

static sig_atomic_t userExitCounter = 0;
static int *user_budgets, *node_budgets;
static void altersighandlers();
static void printstatus(int secs);
static void simterm();

int main(int argc, char const *argv[]) {
    int i;
    conf = loadconf(argc, argv);
    printconf(conf);
    puts("Premere un qualsiasi tasto per iniziare la simulazione, altrimenti uscire con CTRL+C");
    getc(stdin);

    altersighandlers();
    srandom(time(NULL) ^ (getpid()<<16));

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









    for( i = 0; i < nodesNumber + conf.USERS_NUM; i++)
        wait(NULL);
    puts("[MASTER] Clearing IPCs and exiting...");
    if(releaseipcs())
        fprintf(stderr, "Error while cleaning IPCs\n");
    return 0;
}

static void multipurposeMasterHandler(int snum)
{
    extern int *nodemsgids;
    int i, j, notifiedCtr, newmsgqid;
    pid_t *alreadyNotified;
    pid_t tmpindex;
    tmessage tmsg;
    cmessage cmsg; /* Per */
    union sigval sv;
    switch (snum) {
        /* Interrompiamo la simulazione prematuramente */
        case SIGINT:
        {
            sigint = 1;
            break;
        }
        /* Incrementiamo il contatore degli utenti usciti */
        case SIGUSR1:
        {
            userExitCounter++;
            break;
        }
        /* Creiamo un nuovo nodo, notifichiamo gli utenti della sua esistenza, imponiamo a SO_NUM_FRIENDS di
         * aggiungerlo come tale. */

        case SIGUSR2:
        {
            /* Otteniamo il messaggio, siamo sicuri esista nella queue sennò il nodo non avrebbe segnalato */
            /* N.B. Sapendo che esiste gestiamo i messaggi che otteniamo a nome nostro, fino a quello che effettivamente
             * risulta essere una richiesta di generazione di un nuovo nodo dall'object */
            do {
                waitcmessage(&cmsg);
            }while(cmsg.object != CMEX_HOPS_ZERO);

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
            /* INVIO LA MSGQUEUE ID COME PARAMETRO, I NODI LO AGGIUNGERANNO ALLA NODEMSGIDS + AGGIUNGERANNO myFriendsNUM
             * come indice amico, prima di incrementarlo !*/
            /* Preparo il segnale da inviare che conterrà come valore di info il pid del nuovo arrivato */
            sv.sival_int = newmsgqid;
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
                    sigqueue(nodePIDs[tmpindex], SIGUSR2, sv);
                    alreadyNotified[notifiedCtr++] = tmpindex;
                }
                else
                    i--;
            }
            free(alreadyNotified); /* Disalloco l'array allocato per gestire le duplicazioni di segnale */
            /* Una volta notificati i nodi di aggiungere il nuovo arrivato alla lista degli amici, informiamo gli utenti */
            for(i = 0; i < conf.USERS_NUM; i++)
                sigqueue(userPIDs[i], SIGUSR2, sv);
            /* Aggiorno il contatore dei nodi online nella simulazione finalmente */
            nodesNumber++;
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
    sa.sa_flags = SA_NODEFER;
    sa.sa_handler = multipurposeMasterHandler;
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);
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
    int i, j;
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
    printf("====%d/%d==== %d BLOCKS PROCESSED =============\n", secs, conf.SIM_SEC, lastBlock);
    printf("=== Users online %i/%i | Nodes online: %i ===\n", conf.USERS_NUM - userExitCounter, conf.USERS_NUM,
           nodesNumber);
    for(i = 0; i < conf.USERS_NUM; i++)
    {
        printf("[USER %d] Accounted Balance: %d\n", userPIDs[i], user_budgets[i]);
    }
    for(i = 0; i < conf.NODES_NUM; i++)
    {
        printf("[NODE %d] Accounted Balance: %d\n", nodePIDs[i], node_budgets[i]);
    }
    printf("=====================================================\n");
}

static void simterm()
{
    int i;
    for(i = 0; i < conf.USERS_NUM; i++)
        kill(userPIDs[i], SIGKILL);
    for(i = 0; i < nodesNumber; i++)
        kill(nodePIDs[i], SIGINT);
}