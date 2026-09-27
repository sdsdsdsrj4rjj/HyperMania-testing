#include "Game.h"

typedef struct {
    uint8 padding[0x58];
    int32 saveState;
    int32 characterID;
    int32 zoneID;
    int32 lives;
    int32 score;
    int32 score1UP;
    int32 chaosEmeralds;
    int32 continues;
    int32 storedStageID;
    int32 nextSpecialStage;
    int32 collectedSpecialRings;
    int32 medalMods;
#if MANIA_USE_PLUS
    int32 zoneTimes[32];
    int32 characterFlags;
    int32 stock;
    int32 playerID;
#endif
} SaveRAM_Compat;

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

static SaveRAM_Compat *(*SaveGame_GetSaveRAM_fn)(void);
static void (*SaveGame_SaveGameState_fn)(void);
static void (*GameProgress_GiveEmerald_fn)(int32 emeraldID);
static void (*SaveGame_SetEmerald_fn)(uint8 emeraldID);
static void (*Zone_StartFadeOut_fn)(int32 speed, color colorValue);
static void (*Music_Stop_fn)(void);
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

// BSS_Setup's static object layout is mirrored through a compatibility struct.
// This lets the standalone DLL access the built-in 0x20x0x20 playfield without
// importing HyperMania's implementation.
typedef struct ObjectBSS_Setup {
    RSDK_OBJECT
    uint8 randomNumbers[4];
    int32 sphereCount;
    int32 pinkSphereCount;
    int32 rings;
    int32 ringPan;
    int32 ringCount;
    int32 ringID;
    uint16 bgLayer;
    uint16 globeLayer;
    uint16 frustum1Layer;
    uint16 frustum2Layer;
    uint16 playFieldLayer;
    uint16 ringCountLayer;
    uint16 globeFrames;
    int32 globeFrameTable[0x0F];
    int32 globeDirTableL[0x0F];
    int32 globeDirTableR[0x0F];
    int32 screenYTable[0x70];
    int32 divisorTable[0x70];
    int32 xMultiplierTable[0x70];
    int32 frameTable[0x80];
    Vector2 offsetTable[0x100];
    int32 offsetRadiusTable[0x100];
    int32 frustumCount[2];
    int32 frustumOffset[2];
    int32 unused1;
    uint16 playField[0x400];
} ObjectBSS_Setup;

static ObjectBSS_Setup *BSS_Setup;

typedef struct EntityBSS_Setup {
    RSDK_ENTITY
    StateMachine(state);
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
    Animator globeSpinAnimator;
    Animator shadowAnimator;
} EntityBSS_Setup;

typedef struct ObjectSpecialClear {
    RSDK_OBJECT
    uint16 aniFrames;
#if !MANIA_USE_PLUS
    uint16 continueFrames;
#endif
    uint16 sfxScoreAdd;
    uint16 sfxScoreTotal;
    uint16 sfxEvent;
    uint16 sfxSpecialWarp;
    uint16 sfxContinue;
    uint16 sfxEmerald;
} ObjectSpecialClear;

static ObjectSpecialClear *SpecialClear;

typedef struct EntitySpecialClear_Compat {
    RSDK_ENTITY
    StateMachine(state);
    bool32 isBSS;
    int32 messageType;
    int32 timer;
    bool32 showFade;
    bool32 continueIconVisible;
    bool32 hasContinues;
    int32 fillColor;
    int32 score;
    int32 score1UP;
    int32 lives;
    int32 ringBonus;
    int32 perfectBonus;
    int32 machBonus;
    Vector2 messagePos1;
    Vector2 messagePos2;
    Vector2 scoreBonusPos;
    Vector2 ringBonusPos;
    Vector2 perfectBonusPos;
    Vector2 machBonusPos;
    Vector2 continuePos;
    int32 emeraldPositions[7];
    int32 emeraldSpeeds[7];
    int32 unused1;
    int32 unused2;
    int32 unused3;
    int32 unused4;
    int32 unused5;
    int32 unused6;
    int32 unused7;
    int32 unused8;
    bool32 saveInProgress;
    Animator playerNameAnimator;
    Animator bonusAnimator;
    Animator numbersAnimator;
    Animator emeraldsAnimator;
    Animator continueAnimator;
} EntitySpecialClear_Compat;

#define SC_MSG_GOTEMERALD 1
#define SC_MSG_SUPER 3
#define SLOT_SPECIALCLEAR 1

static bool32 bssRewardGiven;
static void (*SpecialClear_State_SetupDelay_fn)(void);

static void ReplaceFinishTarget(void);

static void AwardBSSReward(void) {
    if (!bssRouteActive || bssRewardGiven) return;

    SaveRAM_Compat *saveRAM = SaveGame_GetSaveRAM_fn ? SaveGame_GetSaveRAM_fn() : NULL;
    if (!saveRAM) return;

    const int32 id = ClampStageID(bssRouteStage);

    if (bssRouteIsSuper) {
        HM_Global_Compat *hm = GetHyperManiaGlobals();
        if (hm && hm->currentSave)
            hm->currentSave->superEmeralds |= (uint8)(1 << id);
    }
    else {
        if (SaveGame_SetEmerald_fn)
            SaveGame_SetEmerald_fn((uint8)id);
        else
            saveRAM->chaosEmeralds |= (1 << id);

        if (GameProgress_GiveEmerald_fn && globals->saveSlotID != NO_SAVE_SLOT)
            GameProgress_GiveEmerald_fn(id);

        saveRAM->nextSpecialStage = (id + 1) % 7;
    }

    if (SaveGame_SaveGameState_fn)
        SaveGame_SaveGameState_fn();

    bssRewardGiven = true;
}

static bool32 BSS_Message_State_SaveGameProgress_HOOK(bool32 skippedState) {
    (void)skippedState;

    if (!bssRouteActive || !globals->specialCleared)
        return false;

    AwardBSSReward();

    const uint16 specialClearClass = SpecialClear ? SpecialClear->classID : 0;
    if (specialClearClass && SpecialClear_State_SetupDelay_fn) {
        RSDK.ResetEntitySlot(SLOT_SPECIALCLEAR, specialClearClass, NULL);

        EntitySpecialClear_Compat *result =
            (EntitySpecialClear_Compat *)RSDK.GetEntity(SLOT_SPECIALCLEAR);
        SaveRAM_Compat *saveRAM =
            SaveGame_GetSaveRAM_fn ? SaveGame_GetSaveRAM_fn() : NULL;

        if (result && result->classID == specialClearClass) {
            result->isBSS        = true;
            result->messageType  = bssRouteIsSuper ? SC_MSG_SUPER : SC_MSG_GOTEMERALD;
            result->hasContinues = false;
            result->score        = saveRAM ? saveRAM->score : 0;
            result->score1UP     = saveRAM ? saveRAM->score1UP : 0;
            result->lives        = saveRAM ? saveRAM->lives : 0;
            result->state        = SpecialClear_State_SetupDelay_fn;
        }
    }

    if (SceneInfo->entity)
        destroyEntity(SceneInfo->entity);

    bssRouteActive = false;
    bssRouteIsSuper = false;
    bssRouteStage = 0;
    return true;
}

static void BSS_Setup_Update_HOOK(void) {
    if (bssRouteActive)
        ReplaceFinishTarget();

    if (BSS_Setup && BSS_Setup->classID)
        Mod.Super(BSS_Setup->classID, SUPER_UPDATE, NULL);
}


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

static void BSSSpecial_StageUnload(void *data) {
    (void)data;

    if (bssRouteActive && globals->specialCleared)
        AwardBSSReward();

    bssRouteActive = false;
    bssRouteIsSuper = false;
    bssRouteStage = 0;
    bssRewardGiven = false;
}

typedef struct {
    RSDK_ENTITY
    StateMachine(state);
    int32 id;
    int32 planeFilter;
    int32 warpTimer;
    int32 sparkleRadius;
    Animator warpAnimator;
    int32 angleZ;
    int32 angleY;
    bool32 enabled;
    Matrix matTempRot;
    Matrix matTransform;
    Matrix matWorld;
    Matrix matNormal;
} EntitySpecialRing_Compat;

static bool32 SpecialRing_State_Warp_HOOK(bool32 skippedState) {
    (void)skippedState;
    EntitySpecialRing_Compat *self = (EntitySpecialRing_Compat *)SceneInfo->entity;

    SaveRAM_Compat *saveRAM = SaveGame_GetSaveRAM_fn ? SaveGame_GetSaveRAM_fn() : NULL;
    if (!saveRAM || self->id <= 0) return false;

    const bool32 chaosComplete = saveRAM->chaosEmeralds == 0x7F;
    const bool32 superComplete = HyperManiaSuperEmeraldsComplete();

    if (chaosComplete && HyperManiaAvailable() && !superComplete) {
        bssRouteActive = true;
        bssRouteIsSuper = true;
        bssRouteStage = ClampStageID(self->id - 1);
    }
    else if (!chaosComplete) {
        bssRouteActive = true;
        bssRouteIsSuper = false;
        bssRouteStage = ClampStageID(saveRAM->nextSpecialStage);
    }
    else {
        return false;
    }

    // Match the engine's normal Special Ring/Star Post transition sequence.
    if (SaveGame_SaveGameState_fn) SaveGame_SaveGameState_fn();
    RSDK.PlaySfx(RSDK.GetSfx("Global/SpecialWarp.wav"), false, 0xFE);

    // Freeze gameplay while the white fade performs the scene change.
    RSDK.SetEngineState(ENGINESTATE_FROZEN);

    // The original warp state destroys the ring before starting the fade.
    destroyEntity(self);

    saveRAM->storedStageID = SceneInfo->listPos;
    RSDK.SetScene("Blue Spheres", "");
    SceneInfo->listPos += bssRouteStage;
    if (Zone_StartFadeOut_fn)
        Zone_StartFadeOut_fn(10, 0xF0F0F0);

    if (Music_Stop_fn) Music_Stop_fn();

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
    SaveGame_SetEmerald_fn = Mod.GetPublicFunction(NULL, "SaveGame_SetEmerald");
    Zone_StartFadeOut_fn = Mod.GetPublicFunction(NULL, "Zone_StartFadeOut");
    Music_Stop_fn = Mod.GetPublicFunction(NULL, "Music_Stop");
    HMAPI_GetGlobals_fn = Mod.GetPublicFunction(NULL, "HMAPI_GetGlobals");

    void (*warpState)(void) = Mod.GetPublicFunction(NULL, "SpecialRing_State_Warp");
    if (warpState) Mod.RegisterStateHook(warpState, SpecialRing_State_Warp_HOOK, 1);

    SpecialClear_State_SetupDelay_fn =
        Mod.GetPublicFunction(NULL, "SpecialClear_State_SetupDelay");

    void (*bssMessageSave)(void) =
        Mod.GetPublicFunction(NULL, "BSS_Message_State_SaveGameProgress");
    if (bssMessageSave)
        Mod.RegisterStateHook(bssMessageSave, BSS_Message_State_SaveGameProgress_HOOK, 1);

    MOD_REGISTER_OBJ_OVERLOAD(BSS_Setup, BSS_Setup_Update_HOOK, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    MOD_REGISTER_OBJECT_HOOK(SpecialClear);

    Mod.AddModCallback(MODCB_ONSTAGEUNLOAD, BSSSpecial_StageUnload);
    return true;
}
#endif
