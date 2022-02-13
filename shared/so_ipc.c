#include "so_ipc.h"
#include "so_conf.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <errno.h>

extern conf_t conf;
extern masterbook *mbook;
extern int nodesNumber;

static int semid;
static int shmid;
static int ctrlmsgid;
static int ilistenfrom;
/* Non statico, per permettere di accedervi e gestire l'evento di un nuovo nodo differentemente
 * tra user, node, master*/
int *nodemsgids;
static int *usermsgids;

pid_t *userPIDs;
#define IFERRNORETIT if(errno){ return errno; }

int initipcs() {
    int i, memsize, mbooksize;
    /* We get 3 System-V Semaphores, one for sync, two for read/write on MasterBook */
    semid = semget(IPC_PRIVATE, 3, IPC_CREAT | IPC_EXCL | IPC_RW);
    /* If errno has a value, we stop operating with the semaphore and return errno */
    IFERRNORETIT
    /* We set the value to 1 so that we can further decrease it since users wait for zero */
    semctl(semid, SEM_SYNC, SETVAL, 1);
    IFERRNORETIT
    semctl(semid, SEM_READER, SETVAL, 1);
    IFERRNORETIT
    semctl(semid, SEM_WRITER, SETVAL, 1);
    IFERRNORETIT

    /* We get a Message Queue to enable easy transactions exchange */
    ctrlmsgid = msgget(IPC_PRIVATE, IPC_CREAT | IPC_EXCL | IPC_RW);
    ilistenfrom = ctrlmsgid;
    IFERRNORETIT
    nodemsgids = calloc(nodesNumber, sizeof(int));
    for(i = 0; i < nodesNumber; i++)
    {
        nodemsgids[i] = msgget(IPC_PRIVATE, IPC_CREAT | IPC_EXCL | IPC_RW);
        IFERRNORETIT
    }
    usermsgids = calloc(conf.USERS_NUM, sizeof(int));
    for(i = 0; i < conf.USERS_NUM; i++)
    {
        usermsgids[i] = msgget(IPC_PRIVATE, IPC_CREAT | IPC_EXCL | IPC_RW);
        IFERRNORETIT
    }

    memsize = sizeof(pid_t) * conf.USERS_NUM;
    /*
     * Allocating enough for the structs and the arrays
     * [sizeof(masterbook) + ( sizeof(transaction*) * REGISTER_SIZE) + (sizeof(transaction) * BLOCK_SIZE)]
     * ^(struct masterbook)^         (space for BLOCK pointers)      ^  (space for Blocks' Transactions  )
     * BASE           BLOCK* POINTERS                    TRANSACTIONS POINTED BY BLOCKs*
     */
    mbooksize = sizeof(masterbook) + (sizeof(transaction *) * conf.REGISTRY_SIZE)
                    + (sizeof(transaction) * conf.BLOCK_SIZE * conf.REGISTRY_SIZE);
    /* We get a properly sized shared memory for storing the master book and users */
    shmid = shmget(IPC_PRIVATE, memsize + mbooksize, IPC_CREAT | IPC_EXCL | IPC_RW);
    IFERRNORETIT

    userPIDs = shmat(shmid, NULL, SHM_W | SHM_R);
    mbook = (masterbook*)(userPIDs + conf.USERS_NUM);
    mbook->n_blocks = 0;
    mbook->n_readers = 0;
    mbook->blocks = (transaction**)(mbook+1);
    for (i = 0; i < conf.REGISTRY_SIZE; i++)
        mbook->blocks[i] = ((transaction*)(mbook->blocks + conf.REGISTRY_SIZE)) + (conf.BLOCK_SIZE * i);

    return 0;
}

int releaseipcs() {
    int errOR = 0, i;
    for(i = 0; i < nodesNumber; i++)
        errOR |= msgctl(nodemsgids[i], IPC_RMID, 0);
    for(i = 0; i < conf.USERS_NUM; i++)
        errOR |= msgctl(usermsgids[i], IPC_RMID, 0);
    return errOR
    | msgctl(ctrlmsgid, IPC_RMID, 0)
    | shmctl(shmid, IPC_RMID, 0)
    | semctl(semid, IPC_RMID, 0);
}

int startsyncsem() {
    struct sembuf sbuf;
    sbuf.sem_flg = 0;
    sbuf.sem_num = 0;
    sbuf.sem_op = -1;
    if(semop(semid, &sbuf, 1) == -1)
        return errno;
    return 0;
}

int syncwait() {
    struct sembuf sbuf;
    /* We wait that semaphore is set to 0 */
    sbuf.sem_flg = 0;
    sbuf.sem_num = 0;
    sbuf.sem_op = 0; /* sem_op set to 0 indicates 'wait-for-zero' behaviour */
    if(semop(semid, &sbuf, 1) == -1)
    {
        perror("SyncWait");
        exit(EXIT_FAILURE);
    }
    return 0;
}

int updateMyListeningQueue(int i, short type)
{
    switch (type)
    {
        case UPDATE_AS_NODE:
        {
            ilistenfrom = nodemsgids[i];
            break;
        }
        case UPDATE_AS_USER:
        {
            ilistenfrom = usermsgids[i];
            break;
        }
        default:
        {
            fprintf(stderr, "Unknown queue type @ updateMyListeningQueue");
            break;
        }
    }
    return ilistenfrom;
}

int trysendtmessage(tmessage tm, int index, short toConst)
{
    switch(toConst)
    {
        case TO_USER:
        {
            return msgsnd(usermsgids[index], &tm, sizeof(tm) - sizeof(long), IPC_NOWAIT);
        }
        case TO_NODE:
        {
            return msgsnd(nodemsgids[index], &tm, sizeof(tm) - sizeof(long), IPC_NOWAIT);
        }
        case TO_MSTR:
        {
            return msgsnd(ctrlmsgid, &tm, sizeof(tm) - sizeof(long), IPC_NOWAIT);
        }
        default:
        {
            fprintf(stderr, "Unknown TO constant passed to trysendtmessage\n");
        }
    }
}

int sendtmessage(tmessage tm, int index, short toConst) {
    switch(toConst)
    {
        case TO_USER:
        {
            return msgsnd(usermsgids[index], &tm, sizeof(tm) - sizeof(long), 0);
        }
        case TO_NODE:
        {
            return msgsnd(nodemsgids[index], &tm, sizeof(tm) - sizeof(long), 0);
        }
        case TO_MSTR:
        {
            return msgsnd(ctrlmsgid, &tm, sizeof(tm) - sizeof(long), 0);
        }
        default:
        {
            fprintf(stderr, "Unknown TO constant passed to sendtmessage\n");
        }
    }
}

int waittmessage(tmessage *tm, long msgType) {
    return msgrcv(ilistenfrom, tm, sizeof(*tm) - sizeof(long), msgType, 0);
}

int checktmessage(tmessage *tm, long msgType) {
    return msgrcv(ilistenfrom, tm, sizeof(*tm) - sizeof(long), msgType, IPC_NOWAIT);
}

int allocnewmsgq()
{
    return msgget(IPC_PRIVATE, IPC_CREAT | IPC_EXCL | IPC_RW);
}

int blocksignal(int signum) {
    sigset_t sset;
    sigemptyset(&sset);
    sigaddset(&sset, signum);
    sigprocmask(SIG_BLOCK, &sset, NULL);
}

int unblocksignal(int signum) {
    sigset_t sset;
    sigemptyset(&sset);
    sigaddset(&sset, signum);
    sigprocmask(SIG_UNBLOCK, &sset, NULL);
}

int waitbookwrite() {
    struct sembuf sbuf;
    sbuf.sem_flg = SEM_UNDO;
    sbuf.sem_num = SEM_WRITER;
    sbuf.sem_op = -1;
    semop(semid, &sbuf, 1);
    return 0;
}

int endbookwrite() {
    struct sembuf sbuf;
    sbuf.sem_flg = 0;   /* Non serve SEM_UNDO per l'incremento */
    sbuf.sem_num = SEM_WRITER;
    sbuf.sem_op = +1;
    semop(semid, &sbuf, 1);
    return 0;
}

int waitbookread() {
    struct sembuf sbuf;
    sbuf.sem_flg = 0;
    sbuf.sem_num = SEM_READER;
    sbuf.sem_op = -1;
    /* We wait for the reader semaphore to increase read count */
    semop(semid, &sbuf, 1);
    /* We increase the count */
    mbook->n_readers++;
    /* If we're the first readers, we lock the writing semaphore */
    if(mbook->n_readers == 1)
    {
        sbuf.sem_num = SEM_WRITER;
        semop(semid, &sbuf, 1);
        sbuf.sem_num = SEM_READER;
    }
    sbuf.sem_op = +1;
    /* We let other readers access this critical section */
    semop(semid, &sbuf, 1);
    /* We can now read */
    return 0;
}

int endbookread() {
    struct sembuf sbuf;
    sbuf.sem_flg = 0;
    sbuf.sem_num = SEM_READER;
    sbuf.sem_op = -1;
    semop(semid, &sbuf, 1);
    /* We decrease the readers counter */
    mbook->n_readers--;
    /* We're gonna increment in both cases */
    sbuf.sem_op = +1;
    /* If we were the last readers we enable writing aswell */
    if(mbook->n_readers == 0)
    {
        sbuf.sem_num = SEM_WRITER;
        semop(semid, &sbuf, 1);
        sbuf.sem_num = SEM_READER;
    }
    /* At last, we're gonna enable reading again */
    semop(semid, &sbuf, 1);
    return 0;
}


