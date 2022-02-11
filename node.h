#ifndef NEWTSIM_NODE_H
#define NEWTSIM_NODE_H

#include <unistd.h>
#include <sys/types.h>

pid_t spawnNode(int loopIndex);
void sendFriendsTo(int loopIndex);

#endif
