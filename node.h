/*
 Created by maffin on 1/31/22.
*/

#ifndef NEWTSIM_NODE_H
#define NEWTSIM_NODE_H

#include <unistd.h>
#include <sys/types.h>

pid_t spawnNode(int loopIndex);
void sendFriendsTo(pid_t node, int loopIndex);

#endif
