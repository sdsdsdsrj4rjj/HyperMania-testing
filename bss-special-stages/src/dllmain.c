#include "Game.h"

typedef struct {
    uint8 transferedEmeralds;
    uint8 padding1[3];
    uint8 superEmeralds;
    uint8 padding2[3];
} HM_SaveRAM_Compat;

typedef struct {
    int32 hyperStyle;
    bool32 hyperFlashDropDash;
    bool32 hyperFlashForwarding;
    bool32 GSWburst;
    bool32 GSWitemBoxes;
    bool32 JEAjank;
    float screenFlashFactor;
    bool32 twoHeavensMode;
    bool32 enableHyperMusic;
    bool32 superTailsOnly;
    int32 hyperMusicLoopPoint;
} HM_Config_Compat;

typedef struct {
    HM_Config_Compat config;
    HM_SaveRAM_Compat saveRAM[11];
    HM_SaveRAM_Compat noSaveSlot;
    HM_SaveRAM_Compat *currentSave;
} HM_Global_Compat;

static bool32 bssRouteActive;
static bool32 bssRouteIsSuper;
static int32 bssRouteStage;

static SaveRAM *(*SaveGame_GetSaveRAM_fn)(void);
static void (*SaveGame_SaveGameState_fn)(void);
static void (*GameProgress_GiveEmerald_fn)(int32 emeraldID);
static void (*Zone_StartFadeOut_fn)(int32 speed, color colorValue);
static HM_Global_Compat *(*HMAPI_GetGlobals_fn)(void);

static int32 ClampStageID(int32 id) {
    if (id < 0) id = 0;
    if (id > 6) id = 6;
    return id;
}

static HM_Global_Compat *GetHyperManiaGlobals(void) {
    return HMAPI_GetGlobals_fn ? HMAPI_GetGlobals_fn() : NULL;
}

static bool32 HyperManiaSuperEmeraldsComplete(void) {
    HM_Global_Compat *hm = GetHyperManiaGlobals();
    return hm && hm->currentSave && hm->currentSave->superEmeralds == 0x7F;
}

static bool32 HyperManiaAvailable(void) {
    return HMAPI_GetGlobals_fn != NULL;
}

// BSS_Setup is supplied by GameAPI. Only the fields we need are mirrored here.
typedef struct EntityBSS_Setup_Compat {
    RSDK_ENTITY
    StateMachine(state)
    int32 spinTimer;
    int32 speedupTimer;
    int32 speedupInterval;
    int32 timer;
    int32 spinState;
    int32 palettePage;
    int32 unused1;
    int32 xMultiplier;
    int32 divisor;
    int32 speedupLevel;
    int32 globeSpeed;
    bool32 playerWasBumped;
    int32 globeSpeedInc;
    bool32 disableBumpers;
    int32 globeTimer;
    int32 paletteLine;
    int32 offsetDir;
    int32 unused2;
    Vector2 offset;
    Vector2 playerPos;
    Vector2 lastSpherePos;
    int32 unused3;
    bool32 completedRingLoop;
    int32 paletteID;
    int32 stopMovement;
} EntityBSS_Setup_Compat;

// The built-in BSS object stores its 0x20x0x20 playfield here.
typedef struct ObjectBSS_Setup_Compat {
    RSDK_OBJECT
    uint8 _pad[sizeof(int32) * 0xF + sizeof(int32) * 0x70 * 3 + sizeof(int32) * 0x80 + sizeof(Vector2) * 0x100 + sizeof(int32) * 0x100 + sizeof(int32) * 4 + sizeof(int32) * 2 + sizeof(int32)];
    uint16 playField[0x400];
} ObjectBSS_Setup_Compat;

static ObjectBSS_Setup_Compat *BSS_Setup;

static void ReplaceFinishTarget(void) {
    if (!bssRouteActive || !BSS_Setup) return;

    const uint16 target = bssRouteIsSuper ? 17 : 16; // SUPER / CHAOS emerald
    for (int32 x = 0; x < 32; ++x) {
        for (int32 y = 0; y < 32; ++y) {
            const int32 pos = x * 32 + y;
            const uint16 tile = BSS_Setup->playField[pos];
            if (tile == 18 || tile == 19) BSS_Setup->playField[pos] = target;
        }
    }
}

static bool32 BSS_Setup_State_GlobeEmerald_HOOK(bool32 skippedState) {
    (void)skippedState;
    ReplaceFinishTarget();
    return false;
}

static void BSSSpecial_StageUnload(void *data) {
    (void)data;
    if (!bssRouteActive) return;

    SaveRAM *saveRAM = SaveGame_GetSaveRAM_fn ? SaveGame_GetSaveRAM_fn() : NULL;
    if (globals->specialCleared && saveRAM) {
        const int32 id = ClampStageID(bssRouteStage);
        if (bssRouteIsSuper) {
            HM_Global_Compat *hm = GetHyperManiaGlobals();
            if (hm && hm->currentSave) hm->currentSave->superEmeralds |= (uint8)(1 << id);
        } else {
            saveRAM->collectedEmeralds |= (uint8)(1 << id);
            saveRAM->nextSpecialStage = (id + 1) % 7;
            if (GameProgress_GiveEmerald_fn && globals->saveSlotID != NO_SAVE_SLOT) GameProgress_GiveEmerald_fn(id);
        }
        if (SaveGame_SaveGameState_fn) SaveGame_SaveGameState_fn();
    }

    bssRouteActive = false;
    bssRouteIsSuper = false;
    bssRouteStage = 0;
}

static bool32 SpecialRing_State_Warp_HOOK(bool32 skippedState) {
    (void)skippedState;
    RSDK_THIS(SpecialRing);

    SaveRAM *saveRAM = SaveGame_GetSaveRAM_fn ? SaveGame_GetSaveRAM_fn() : NULL;
    if (!saveRAM || self->id <= 0) return false;

    const bool32 chaosComplete = saveRAM->collectedEmeralds == 0x7F;
    const bool32 superComplete = HyperManiaSuperEmeraldsComplete();

    if (chaosComplete && HyperManiaAvailable() && !superComplete) {
        bssRouteActive = true;
        bssRouteIsSuper = true;
        bssRouteStage = ClampStageID(self->id - 1);
    } else if (!chaosComplete) {
        bssRouteActive = true;
        bssRouteIsSuper = false;
        bssRouteStage = ClampStageID(saveRAM->nextSpecialStage);
    } else {
        return false;
    }

    if (SaveGame_SaveGameState_fn) SaveGame_SaveGameState_fn();
    RSDK.PlaySfx(RSDK.GetSfx("Special/SSExit.wav"), false, 0xFE);

    saveRAM->storedStageID = SceneInfo->listPos;
    RSDK.SetScene("Blue Spheres", "");
    SceneInfo->listPos += bssRouteStage;
    if (Zone_StartFadeOut_fn) Zone_StartFadeOut_fn(10, 0xF0F0F0);
    else RSDK.LoadScene();
    Music_Stop();

    self->active = ACTIVE_DISABLED;
    return true;
}

#if RETRO_USE_MOD_LOADER
DLLExport bool32 LinkModLogic(EngineInfo *info, const char *id) {
#if MANIA_USE_PLUS
    LinkGameLogicDLL(info);
#else
    LinkGameLogicDLL(*info);
#endif
    globals = Mod.GetGlobals();
    modID = id;

    SaveGame_GetSaveRAM_fn = Mod.GetPublicFunction(NULL, "SaveGame_GetSaveRAM");
    SaveGame_SaveGameState_fn = Mod.GetPublicFunction(NULL, "SaveGame_SaveGameState");
    GameProgress_GiveEmerald_fn = Mod.GetPublicFunction(NULL, "GameProgress_GiveEmerald");
    Zone_StartFadeOut_fn = Mod.GetPublicFunction(NULL, "Zone_StartFadeOut");
    HMAPI_GetGlobals_fn = Mod.GetPublicFunction(NULL, "HMAPI_GetGlobals");

    void (*warpState)(void) = Mod.GetPublicFunction(NULL, "SpecialRing_State_Warp");
    if (warpState) Mod.RegisterStateHook(warpState, SpecialRing_State_Warp_HOOK, 1);

    void (*emeraldState)(void) = Mod.GetPublicFunction(NULL, "BSS_Setup_State_GlobeEmerald");
    if (emeraldState) Mod.RegisterStateHook(emeraldState, BSS_Setup_State_GlobeEmerald_HOOK, 1);

    // This object registration resolves the runtime BSS_Setup pointer.
    MOD_REGISTER_OBJECT_HOOK(BSS_Setup);

    Mod.AddModCallback(MODCB_ONSTAGEUNLOAD, BSSSpecial_StageUnload);
    return true;
}
#endif
