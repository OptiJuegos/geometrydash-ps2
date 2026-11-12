#include "asset_locator.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void check(const char *elf,const char *cwd,const char *expected,int disc)
{
    Ps2AssetCandidates c;
    PS2_AssetCandidatesInit(&c,elf,cwd);
    assert(c.count > 0);
    assert(!strcmp(c.paths[0],expected));
    assert(c.launch_is_disc == disc);
}
int main(void)
{
    check("mass:/Juegos/GeometryDash/BOOT.ELF",NULL,"mass:/Juegos/GeometryDash",0);
    check("mass0:\\Apps\\GD\\GAME.ELF",NULL,"mass0:/Apps/GD",0);
    check("host:E:\\Juegos\\geometrydash-psp\\ps2\\out\\GAME.ELF",
          "host:", "host:E:/Juegos/geometrydash-psp/ps2/out",0);
    check("cdrom0:\\JUEGOS\\GEO\\BOOT.ELF;1",NULL,"cdrom0:/JUEGOS/GEO",1);
    check("mass:/BOOT.ELF",NULL,"mass:",0);
    Ps2AssetCandidates usb;
    PS2_AssetCandidatesInit(&usb, "mass:/Juegos/GD/GAME.ELF", "host:");
    assert(!strcmp(usb.paths[0], "mass:/Juegos/GD"));
    assert(!strcmp(usb.paths[1], "mass:/GDPS2"));
    assert(!strcmp(usb.paths[2], "mass0:/GDPS2"));
    assert(!strcmp(usb.paths[3], "mass1:/GDPS2"));
    Ps2AssetCandidates disc;
    PS2_AssetCandidatesInit(&disc, "cdrom0:/JUEGOS/BOOT.ELF;1", "host:");
    assert(!strcmp(disc.paths[0], "cdrom0:/JUEGOS"));
    assert(!strcmp(disc.paths[1], "cdrom0:"));
    assert(!strcmp(disc.paths[2], "cdfs:"));
    for (unsigned i = 0; i < disc.count; ++i)
        assert(strcmp(disc.paths[i], "mass:/GDPS2") != 0);
    Ps2AssetCandidates c;
    PS2_AssetCandidatesInit(&c,NULL,NULL);
    assert(!strcmp(c.paths[0],"host:"));
    unsigned original=c.count;
    assert(!PS2_AssetCandidatesAdd(&c,"host:"));
    assert(c.count==original);
    assert(PS2_AssetIsDeviceRoot("mass:/"));
    assert(!PS2_AssetIsDeviceRoot("mass:/Games"));
    assert(!PS2_AssetIsDeviceRoot("host:E:/Games"));
    assert(PS2_AssetIsUsb("mass1:/Games/GD"));
    assert(PS2_AssetIsDisc("cdrom0:/GAME.ELF;1"));
    assert(!PS2_AssetCandidatesAdd(&c,"E:/Windows"));
    puts("ASSET LOCATOR TESTS PASS");
    return 0;
}
