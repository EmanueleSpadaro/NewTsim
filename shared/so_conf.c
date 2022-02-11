#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "so_conf.h"
#include "../lib/jsmn-1.1.0/jsmn.h"

#define EXACT_TOKEN_NUMBER 27

/* SO_BLOCK_SIZE and SO_REGISTRY_SIZE are defined at Compile-Time */

/* Injects id example values into a conf_t struct (skipping compile-time ones) */
void injectConf(int id, conf_t *confPtr)
{
    switch (id) {
        case 1:
            confPtr->USERS_NUM = SO_USERS_NUM_1;
            confPtr->NODES_NUM = SO_NODES_NUM_1;
            confPtr->BUDGET_INIT = SO_BUDGET_INIT_1;
            confPtr->REWARD = SO_REWARD_1;
            confPtr->MIN_TRANS_GEN_NSEC = SO_MIN_TRANS_GEN_NSEC_1;
            confPtr->MAX_TRANS_GEN_NSEC = SO_MAX_TRANS_GEN_NSEC_1;
            confPtr->RETRY = SO_RETRY_1;
            confPtr->TP_SIZE = SO_TP_SIZE_1;
            confPtr->MIN_TRANS_PROC_NSEC = SO_MIN_TRANS_PROC_NSEC_1;
            confPtr->MAX_TRANS_PROC_NSEC = SO_MAX_TRANS_PROC_NSEC_1;
            confPtr->SIM_SEC = SO_SIM_SEC_1;
            confPtr->NUM_FRIENDS = SO_FRIENDS_NUM_1;
            confPtr->HOPS = SO_HOPS_1;
            confPtr->BLOCK_SIZE = SO_BLOCK_SIZE;
            confPtr->REGISTRY_SIZE = SO_REGISTRY_SIZE;
            break;

        case 2:
            confPtr->USERS_NUM = SO_USERS_NUM_2;
            confPtr->NODES_NUM = SO_NODES_NUM_2;
            confPtr->BUDGET_INIT = SO_BUDGET_INIT_2;
            confPtr->REWARD = SO_REWARD_2;
            confPtr->MIN_TRANS_GEN_NSEC = SO_MIN_TRANS_GEN_NSEC_2;
            confPtr->MAX_TRANS_GEN_NSEC = SO_MAX_TRANS_GEN_NSEC_2;
            confPtr->RETRY = SO_RETRY_2;
            confPtr->TP_SIZE = SO_TP_SIZE_2;
            confPtr->MIN_TRANS_PROC_NSEC = SO_MIN_TRANS_PROC_NSEC_2;
            confPtr->MAX_TRANS_PROC_NSEC = SO_MAX_TRANS_PROC_NSEC_2;
            confPtr->SIM_SEC = SO_SIM_SEC_2;
            confPtr->NUM_FRIENDS = SO_FRIENDS_NUM_2;
            confPtr->HOPS = SO_HOPS_2;
            confPtr->BLOCK_SIZE = SO_BLOCK_SIZE;
            confPtr->REGISTRY_SIZE = SO_REGISTRY_SIZE;
            break;

        case 3:
            confPtr->USERS_NUM = SO_USERS_NUM_3;
            confPtr->NODES_NUM = SO_NODES_NUM_3;
            confPtr->BUDGET_INIT = SO_BUDGET_INIT_3;
            confPtr->REWARD = SO_REWARD_3;
            confPtr->MIN_TRANS_GEN_NSEC = SO_MIN_TRANS_GEN_NSEC_3;
            confPtr->MAX_TRANS_GEN_NSEC = SO_MAX_TRANS_GEN_NSEC_3;
            confPtr->RETRY = SO_RETRY_3;
            confPtr->TP_SIZE = SO_TP_SIZE_3;
            confPtr->MIN_TRANS_PROC_NSEC = SO_MIN_TRANS_PROC_NSEC_3;
            confPtr->MAX_TRANS_PROC_NSEC = SO_MAX_TRANS_PROC_NSEC_3;
            confPtr->SIM_SEC = SO_SIM_SEC_3;
            confPtr->NUM_FRIENDS = SO_FRIENDS_NUM_3;
            confPtr->HOPS = SO_HOPS_3;
            confPtr->BLOCK_SIZE = SO_BLOCK_SIZE;
            confPtr->REGISTRY_SIZE = SO_REGISTRY_SIZE;
            break;
    }
}

/* Returns a default conf_t with illegal values */
conf_t getdefaultconf()
{
    conf_t conf;

    conf.USERS_NUM = -1;
    conf.NODES_NUM = -1;
    conf.BUDGET_INIT = -1;
    conf.REWARD = -1;
    conf.MIN_TRANS_GEN_NSEC = -1;
    conf.MAX_TRANS_GEN_NSEC = -1;
    conf.RETRY = -1;
    conf.TP_SIZE = -1;
    conf.MIN_TRANS_PROC_NSEC = -1;
    conf.MAX_TRANS_PROC_NSEC = -1;
    conf.SIM_SEC = -1;
    conf.NUM_FRIENDS = -1;
    conf.HOPS = -1;
    conf.BLOCK_SIZE = SO_BLOCK_SIZE;
    conf.REGISTRY_SIZE = SO_REGISTRY_SIZE;

    return conf;
}


/* Why const char * ?*/
/* https://stackoverflow.com/questions/8091770/const-char-and-char-const-are-they-the-same */

/* Returns 1 if tokName is the same string as the one represented by tok */
int strtokequal(const char *js, const char *tokName, jsmntok_t tok)
{
    /* If token is a JSMN_STRING token  and has the same
       length as the desired name, we compare them*/
    if (tok.type == JSMN_STRING &&
        tok.end - tok.start == strlen(tokName) &&
        strncmp(js + tok.start, tokName, tok.end - tok.start) == 0)
        return 1;

    return 0;
}

/* Named after atoi (alphabet to integer) */
int toktoi(const char *js, jsmntok_t tok)
{
    /* Returning value, since we can't return directly because of free */
    int retval;
    /* We allocate n chars + 1 for termination */
    char *charBuffer = malloc(tok.end - tok.start + 1);
    /* We copy token's string and terminate our buffer with \0*/
    strncpy(charBuffer, js + tok.start, tok.end - tok.start);
    charBuffer[tok.end - tok.start] = '\0';
    retval = atoi(charBuffer);

    free(charBuffer);
    return retval;
}

/* toktoi, but for long types */
long toktol(const char *js, jsmntok_t tok)
{
    /* Returning value, since we can't return directly because of free*/
    long retval;
    /* We allocate n chars + 1 for termination */
    char *charBuffer = malloc(tok.end - tok.start + 1);
    /* We copy token's string and terminate our buffer with \0*/
    strncpy(charBuffer, js + tok.start, tok.end - tok.start);
    charBuffer[tok.end - tok.start] = '\0';
    retval = atol(charBuffer);

    free(charBuffer);
    return retval;
}

/* Reads from the json configuration file and injects its values into a conf_t struct */
conf_t readjsonconf()
{
    int i;                  /* Iterator */
    conf_t conf;            /* Structure to be filled with file values */
    char *js;               /* Char buffer for storing file's flength bytes */
    int toklength;          /* Length of the dynamically sized jsmntok_t array */
    long flength;           /* File length for allocating enough bytes */
    jsmn_parser parser;     /* Parser */
    /* We allocate the exact amount of tokens of our conf files */
    jsmntok_t tokens[EXACT_TOKEN_NUMBER];

    /* We read the json file entirely before processing it */
    FILE *file = fopen(CONF_FILENAME, "r");

    if (file == NULL) {
        printf("Error while loading "CONF_FILENAME"\n");
        exit(-1);
    }

    /* We compute the necessary buffer length before reading */
    fseek(file, 0, SEEK_END);
    /* We'll need SEEK_END index + 1 char for termination char */
    flength = ftell(file);
    js = (char*)malloc(flength + 1);
    /* We point back again to the file start */
    fseek(file, 0, SEEK_SET);
    /* We load the entire file, close its handle and terminate the string */
    fread(js, 1, flength, file);
    fclose(file);
    js[flength] = '\0';

    /* We happen to have now js as a char string containing the entire json */
    /* We know the json structure, so we proceed to validate and parse it */

    /* We intialize the json parser as-per jsmn documentation */
    jsmn_init(&parser);
    toklength = jsmn_parse(&parser, js, strlen(js), tokens,
                           sizeof(tokens) / sizeof(tokens[0]));

    if (toklength < 0)
        return getdefaultconf();

    /* Our structure happens to be a top-level object contains keys to integers */
    /* So if there's less then 1 token and the first isn't an object the json isn't valid */
    if (toklength < 1 || tokens[0].type != JSMN_OBJECT)
        return getdefaultconf();


    /*
     * We loop over each token and check for key-names.
     * Once a key's found, we convert the next token to and integer and
     * we assign its value to our in-memory struct.
     * If we find an unknown string token, we return the conf as it is
     * since we're sure it will contain illegal values which will happen
     * to throw a premature exit after checks
     */
    for (i = 1; i < toklength; i++) {
        if(strtokequal(js, "USERS_NUM", tokens[i]))
            conf.USERS_NUM = toktoi(js, tokens[++i]);
        else if(strtokequal(js, "NODES_NUM", tokens[i]))
            conf.NODES_NUM = toktoi(js, tokens[++i]);
        else if(strtokequal(js, "BUDGET_INIT", tokens[i]))
            conf.BUDGET_INIT = toktoi(js, tokens[++i]);
        else if(strtokequal(js, "REWARD", tokens[i]))
            conf.REWARD = toktoi(js, tokens[++i]);
        else if(strtokequal(js, "MIN_TRANS_GEN_NSEC", tokens[i]))
            conf.MIN_TRANS_GEN_NSEC = toktol(js, tokens[++i]);
        else if(strtokequal(js, "MAX_TRANS_GEN_NSEC", tokens[i]))
            conf.MAX_TRANS_GEN_NSEC = toktol(js, tokens[++i]);
        else if(strtokequal(js, "RETRY", tokens[i]))
            conf.RETRY = toktoi(js, tokens[++i]);
        else if(strtokequal(js, "TP_SIZE", tokens[i]))
            conf.TP_SIZE = toktoi(js, tokens[++i]);
        else if(strtokequal(js, "MIN_TRANS_PROC_NSEC", tokens[i]))
            conf.MIN_TRANS_PROC_NSEC = toktol(js, tokens[++i]);
        else if(strtokequal(js, "MAX_TRANS_PROC_NSEC", tokens[i]))
            conf.MAX_TRANS_PROC_NSEC = toktol(js, tokens[++i]);
        else if(strtokequal(js, "SIM_SEC", tokens[i]))
            conf.SIM_SEC = toktoi(js, tokens[++i]);
        else if(strtokequal(js, "NUM_FRIENDS", tokens[i]))
            conf.NUM_FRIENDS = toktoi(js, tokens[++i]);
        else if(strtokequal(js, "HOPS", tokens[i]))
            conf.HOPS = toktoi(js, tokens[++i]);
        else
            return conf;
    }

    /* We assign compile-time values */
    conf.BLOCK_SIZE = SO_BLOCK_SIZE;
    conf.REGISTRY_SIZE = SO_REGISTRY_SIZE;

    return conf;
}

/* Writes a given conf_t inside of the json configuration file */
void writejsonconf(conf_t conf)
{
    FILE *file = fopen(CONF_FILENAME, "w");

    fprintf(file,
            "{\n"
            "\"USERS_NUM\": %d,\n"
            "\"NODES_NUM\": %d,\n"
            "\"BUDGET_INIT\": %d,\n"
            "\"REWARD\": %d,\n"
            "\"MIN_TRANS_GEN_NSEC\": %ld,\n"
            "\"MAX_TRANS_GEN_NSEC\": %ld,\n"
            "\"RETRY\": %d,\n"
            "\"TP_SIZE\": %d,\n"
            "\"MIN_TRANS_PROC_NSEC\": %ld,\n"
            "\"MAX_TRANS_PROC_NSEC\": %ld,\n"
            "\"SIM_SEC\": %d,\n"
            "\"NUM_FRIENDS\": %d,\n"
            "\"HOPS\": %d\n"
            "}",
            conf.USERS_NUM,
            conf.NODES_NUM,
            conf.BUDGET_INIT,
            conf.REWARD,
            conf.MIN_TRANS_GEN_NSEC,
            conf.MAX_TRANS_GEN_NSEC,
            conf.RETRY,
            conf.TP_SIZE,
            conf.MIN_TRANS_PROC_NSEC,
            conf.MAX_TRANS_PROC_NSEC,
            conf.SIM_SEC,
            conf.NUM_FRIENDS,
            conf.HOPS
    );

    fclose(file);
}

/* Checks if a configuration struct is a valid set of parameters */
int checkconf(conf_t conf2check)
{
    /* We check if the struct contains illegal values */
    if(conf2check.USERS_NUM <= 0 || conf2check.NODES_NUM <= 0
       || conf2check.BUDGET_INIT <= 0 || conf2check.REWARD < 0 || conf2check.REWARD > 100
       || conf2check.MIN_TRANS_GEN_NSEC < 0 || conf2check.MAX_TRANS_GEN_NSEC < 0
       || conf2check.RETRY < 0 || conf2check.TP_SIZE < 0
       || conf2check.MIN_TRANS_PROC_NSEC < 0 || conf2check.MAX_TRANS_PROC_NSEC < 0
       || conf2check.SIM_SEC < 0 || conf2check.NUM_FRIENDS < 0
       || conf2check.HOPS < 0
       || conf2check.MAX_TRANS_GEN_NSEC < conf2check.MIN_TRANS_GEN_NSEC
       || conf2check.MAX_TRANS_PROC_NSEC < conf2check.MIN_TRANS_PROC_NSEC
       || conf2check.TP_SIZE < conf2check.BLOCK_SIZE
       || conf2check.NUM_FRIENDS >= conf2check.NODES_NUM)
        return 0;
    /* If it doesn't contain illegal values */
    return 1;
}

/* Prints a conf_t value to screen */
void printconf(conf_t conf)
{
    printf("SO_USERS_NUM: %d | SO_NODES_NUM: %d\n"
           "SO_BUDGET_INIT: %d | SO_REWARD: %d\n"
           "SO_MIN_TRANS_GEN_NSEC: %ld | SO_MAX_TRANS_GEN_NSEC: %ld\n"
           "SO_RETRY: %d | SO_TP_SIZE: %d\n"
           "SO_BLOCK_SIZE: %d | SO_REGISTRY_SIZE: %d\n"
           "SO_MIN_TRANS_PROC_NSEC: %ld | SO_MAX_TRANS_PROC_NSEC: %ld\n"
           "SO_SIM_SEC: %d\n"
           "SO_NUM_FRIENDS: %d | SO_HOPS: %d\n",
           conf.USERS_NUM, conf.NODES_NUM, conf.BUDGET_INIT, conf.REWARD,
           conf.MIN_TRANS_GEN_NSEC, conf.MAX_TRANS_GEN_NSEC,
           conf.RETRY, conf.TP_SIZE, conf.BLOCK_SIZE, conf.REGISTRY_SIZE,
           conf.MIN_TRANS_PROC_NSEC, conf.MAX_TRANS_PROC_NSEC, conf.SIM_SEC,
           conf.NUM_FRIENDS, conf.HOPS);
}

/* Prints the help message to stdout */
void printhelp()
{
    printf("tsim - A OS Lab. Project\n"
           "Emanuele Spadaro\n"
           "Usage:\n"
           "tsim (no arguments)\n"
           " it writes a configuration file to customize simulation or reads it if it exists\n"
           "tsim -h\n"
           " it prints this help message\n"
           "tsim -c <1-3>\n"
           " it loads the given example configuration (Conf#1 || Conf#2 || Conf#3)\n"
    );
}

/* Returns the proper conf_t value corresponding to user parameters */
conf_t loadconf(int argc, char const *argv[])
{
    /* We get a stub configuration that has illegal values to check after */
    conf_t conf = getdefaultconf();

    /* If there's no argument, we create a stub json configuration file if there's none,
    or we load it for configuration */
    if (argc == 1) {
        FILE *file = fopen(CONF_FILENAME, "r");
        /*If there's no configuration file, we create a stub one*/
        if (file == NULL) {
            writejsonconf(conf);
            printf(CONF_FILENAME" has been written. Make sure to modify it properly and run again"
                   " without arguments to load the configuration file.\n");
            exit(-1);
        }

        conf = readjsonconf();
    } else if (argc == 2) {
        if(strcmp("-h", argv[1]) == 0)
            printhelp();
        else if(strcmp("-c", argv[1]) == 0)
            printf("Usage: tsim -c <example_conf_number [1-3]>\n");
        exit(-1);
    } else if (argc == 3) {
        if (strcmp("-c", argv[1]) == 0) {
            int opt = atoi(argv[2]);
            if(opt >= 1 && opt <= 3)
                injectConf(opt, &conf);
            else {
                printf("Invalid example configuration! Choose [1-3]\n");
                exit(-1);
            }
        }
    } else {
        printhelp();
        exit(-1);
    }

    if (checkconf(conf) == 0) {
        printf("Error: invalid configuration after processing.\n"
               "Make sure you've set proper values to run the simulation.\n");
        printconf(conf);
        exit(-1);
    }

    return conf;
}