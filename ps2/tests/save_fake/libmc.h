#pragma once
#define MC_WAIT 0
#define MC_TYPE_MC 0
#define MC_TYPE_NONE 0
#define MC_TYPE_PS2 2
int mcInit(int type);
int mcGetInfo(int port, int slot, int *type, int *free_clusters, int *formatted);
int mcSync(int mode, int *cmd, int *result);
