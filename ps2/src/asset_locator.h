#ifndef GDPS2_ASSET_LOCATOR_H
#define GDPS2_ASSET_LOCATOR_H
#define GDPS2_ASSET_MAX_ROOTS 24
#define GDPS2_ASSET_PATH_BYTES 512
/* Pure path discovery; ROMFS handles device I/O separately. */
typedef struct {
    char paths[GDPS2_ASSET_MAX_ROOTS][GDPS2_ASSET_PATH_BYTES];
    unsigned count;
    int launch_is_disc;
} Ps2AssetCandidates;
int PS2_AssetCandidatesAdd(Ps2AssetCandidates *out, const char *root);
void PS2_AssetCandidatesInit(Ps2AssetCandidates *out,
                             const char *argv0, const char *cwd);
int PS2_AssetIsDisc(const char *path);
int PS2_AssetIsUsb(const char *path);
int PS2_AssetIsHost(const char *path);
int PS2_AssetIsDeviceRoot(const char *path);
#endif