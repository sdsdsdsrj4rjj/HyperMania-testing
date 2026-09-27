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


// BSS_Collectable is not exposed by GameAPI, so mirror its public object/entity
// layout here for the standalone mod hook.
typedef struct ObjectBSS_Collectable {
    RSDK_OBJECT
    Animator sphereAnimator[24];
    uint8 initializedTables;
    int32 ringScaleTableX[32];
    int32 ringScaleTableY[32];
    int32 medalScaleTable[32];
    int32 screenYValues[32];
    int32 medalScreenYVals[32];
    uint16 aniFrames;
    uint16 ringFrames;
} ObjectBSS_Collectable;

typedef struct EntityBSS_Collectable {
    RSDK_ENTITY
    int32 type;
    Animator animator;
} EntityBSS_Collectable;

typedef enum {
    BSS_NONE          = 0,
    BSS_SPHERE_BLUE   = 1,
    BSS_SPHERE_RED    = 2,
    BSS_SPHERE_BUMPER = 3,
    BSS_SPHERE_YELLOW = 4,
    BSS_SPHERE_GREEN  = 5,
    BSS_SPHERE_PINK   = 6,
    BSS_RING          = 7,
    BSS_SPAWN_UP      = 8,
    BSS_SPAWN_RIGHT   = 9,
    BSS_SPAWN_DOWN    = 10,
    BSS_SPAWN_LEFT    = 11,
    BSS_UNUSED_1      = 12,
    BSS_UNUSED_2      = 13,
    BSS_UNUSED_3      = 14,
    BSS_RING_SPARKLE  = 15,
    BSS_EMERALD_CHAOS = 16,
    BSS_EMERALD_SUPER = 17,
    BSS_MEDAL_SILVER  = 18,
    BSS_MEDAL_GOLD    = 19,
    BSS_UNUSED_4      = 20,
    BSS_UNUSED_5      = 21,
    BSS_UNUSED_6      = 22,
    BSS_UNUSED_7      = 23,
    BSS_SPHERE_GREEN_STOOD = 0x80 | 1,
    BSS_BLUE_STOOD         = 0x80 | 2,
    BSS_SPHERE_PINK_STOOD  = 0x80 | 6,
} BSSCollectableTypes;

static ObjectBSS_Collectable *BSS_Collectable;

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
static void (*BSS_Message_State_SaveGameProgress_fn)(void);

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

static void ResolveHyperManiaAPI(void) {
    if (HMAPI_GetGlobals_fn)
        return;

    HMAPI_GetGlobals_fn = Mod.GetPublicFunction("HYPERMANIA", "HMAPI_GetGlobals");
    if (!HMAPI_GetGlobals_fn)
        HMAPI_GetGlobals_fn = Mod.GetPublicFunction("HyperMania", "HMAPI_GetGlobals");
    if (!HMAPI_GetGlobals_fn)
        HMAPI_GetGlobals_fn = Mod.GetPublicFunction(NULL, "HMAPI_GetGlobals");
}

static bool32 HyperManiaAvailable(void) {
    ResolveHyperManiaAPI();
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
    uint16 sphereChainTable[0x400];
    uint16 sphereCollectedTable[0x400];
    uint16 sfxBlueSphere;
    uint16 sfxSSExit;
    uint16 sfxBumper;
    uint16 sfxSpring;
    uint16 sfxRing;
    uint16 sfxLoseRings;
    uint16 sfxSSJettison;
    uint16 sfxEmerald;
    uint16 sfxEvent;
    uint16 sfxMedal;
    uint16 sfxMedalCaught;
    uint16 sfxTeleport;
};\n

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

static bool32 bssRewardGiven;
static bool32 bssResultStarted;

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

static void ReplaceFinishTarget(void) {
    if (!bssRouteActive || !BSS_Setup)
        return;

    // Type 18 is repurposed by this mod as the Chaos Emerald finish.
    // Type 17 remains the Super Emerald finish and is used only when the
    // HyperMania Super Emerald route is active.
    const uint16 target = bssRouteIsSuper ? 17 : 18;

    for (int32 x = 0; x < 32; ++x) {
        for (int32 y = 0; y < 32; ++y) {
            const int32 pos = (x * 32) + y;
            const uint16 tile = BSS_Setup->playField[pos];

            // Vanilla SetupFinishSequence writes silver/gold here.
            // Replace the actual finish tile before HandleSteppedObjects
            // can award a medal.
            if (tile == 18 || tile == 19)
                BSS_Setup->playField[pos] = target;
        }
    }
}

// HandleCollectableMovement copies playField[] into each BSS_Collectable's
// type field. Changing only playField therefore leaves the visible finish
// object as a silver/gold medal until the next rebuild. Replace the already
// spawned collectable too so its renderer uses the emerald animator.
static void ReplaceFinishCollectables(void) {
    if (!bssRouteActive)
        return;

    const int32 target = bssRouteIsSuper ? 17 : 18;

    for (int32 slot = RESERVE_ENTITY_COUNT; slot < RESERVE_ENTITY_COUNT + 0x80; ++slot) {
        EntityBSS_Collectable *collectable = (EntityBSS_Collectable *)RSDK.GetEntity(slot);

        if (!collectable || !collectable->classID)
            continue;

        if (collectable->type == 18 || collectable->type == 19)
            collectable->type = target;
    }
}

/*
 * Runs after the normal entity updates and before drawing. This is important:
 * BSS_Setup writes the finish medal during SetupFinishSequence and then rebuilds
 * the visible collectables from playField. Doing the replacement in late update
 * means the final frame uses the emerald type without replacing any engine hook.
 */
static void BSS_Collectable_Draw_HOOK(void) {
    EntityBSS_Collectable *self = (EntityBSS_Collectable *)SceneInfo->entity;
    Vector2 drawPos;

    // Type 18 is repurposed by this mod as the Chaos Emerald.
    // Use the built-in Chaos Emerald animator instead of the medal animator.
    if (self->type == 18) {
        BSS_Collectable->sphereAnimator[16].frameID = self->animator.frameID >> 1;
        RSDK.DrawSprite(&BSS_Collectable->sphereAnimator[16], NULL, true);
        return;
    }

    // Type 17 is the Super Emerald, used only by the HyperMania route.
    if (self->type == 17) {
        BSS_Collectable->sphereAnimator[17].frameID = self->animator.frameID >> 1;
        RSDK.DrawSprite(&BSS_Collectable->sphereAnimator[17], NULL, true);
        return;
    }

    // Vanilla BSS drawing for every other collectable type.
    switch (self->type) {
        case BSS_RING:
            self->drawFX    = FX_FLIP | FX_SCALE;
            self->scale.x   = BSS_Collectable->ringScaleTableX[self->animator.frameID];
            self->scale.y   = BSS_Collectable->ringScaleTableY[self->animator.frameID];
            self->direction = BSS_Collectable->sphereAnimator[self->type].frameID > 8;
            drawPos.x       = self->position.x;
            drawPos.y       = self->position.y;
            drawPos.y -= BSS_Collectable->screenYValues[self->animator.frameID];
            RSDK.DrawSprite(&BSS_Collectable->sphereAnimator[self->type], &drawPos, true);

            self->drawFX = FX_NONE;
            return;

        case BSS_RING_SPARKLE:
            RSDK.DrawSprite(&BSS_Collectable->sphereAnimator[self->type], NULL, true);
            return;

        case BSS_EMERALD_CHAOS:
        case BSS_EMERALD_SUPER:
            BSS_Collectable->sphereAnimator[self->type].frameID = self->animator.frameID >> 1;
            RSDK.DrawSprite(&BSS_Collectable->sphereAnimator[self->type], NULL, true);
            return;

        case BSS_MEDAL_SILVER:
        case BSS_MEDAL_GOLD:
            self->drawFX  = FX_SCALE;
            self->scale.x = BSS_Collectable->medalScaleTable[self->animator.frameID];
            self->scale.y = BSS_Collectable->medalScaleTable[self->animator.frameID];
            drawPos.x     = self->position.x;
            drawPos.y     = self->position.y;
            drawPos.y -= BSS_Collectable->screenYValues[self->animator.frameID];
            RSDK.DrawSprite(&BSS_Collectable->sphereAnimator[self->type], &drawPos, true);

            self->drawFX = FX_NONE;
            return;

        case BSS_SPHERE_GREEN_STOOD:
            BSS_Collectable->sphereAnimator[BSS_SPHERE_GREEN].frameID = self->animator.frameID;
            self->alpha                                               = 0x80;
            self->inkEffect                                           = INK_ALPHA;
            RSDK.DrawSprite(&BSS_Collectable->sphereAnimator[BSS_SPHERE_GREEN], NULL, true);

            self->inkEffect = INK_NONE;
            return;

        case BSS_BLUE_STOOD:
            BSS_Collectable->sphereAnimator[BSS_SPHERE_BLUE].frameID = self->animator.frameID;
            self->alpha                                              = 0x80;
            self->inkEffect                                          = INK_ALPHA;
            RSDK.DrawSprite(&BSS_Collectable->sphereAnimator[BSS_SPHERE_BLUE], NULL, true);

            self->inkEffect = INK_NONE;
            return;

        case BSS_SPHERE_PINK_STOOD:
            BSS_Collectable->sphereAnimator[BSS_SPHERE_PINK].frameID = self->animator.frameID;
            self->alpha                                              = 0x80;
            self->inkEffect                                          = INK_ALPHA;
            RSDK.DrawSprite(&BSS_Collectable->sphereAnimator[BSS_SPHERE_PINK], NULL, true);

            self->inkEffect = INK_NONE;
            return;

        default:
            BSS_Collectable->sphereAnimator[self->type].frameID = self->animator.frameID;
            RSDK.DrawSprite(&BSS_Collectable->sphereAnimator[self->type], NULL, true);
            return;
    }
}

static void BSS_OnLateUpdate(void *data) {
    (void)data;

    if (!bssRouteActive)
        return;

    // Keep the BSS finish sequence, but make both of its built-in medal sounds
    // use the emerald collection sound for this mod's emerald routes.
    if (BSS_Setup && BSS_Setup->sfxEmerald) {
        BSS_Setup->sfxMedal      = BSS_Setup->sfxEmerald;
        BSS_Setup->sfxMedalCaught = BSS_Setup->sfxEmerald;
    }

    ReplaceFinishTarget();
    ReplaceFinishCollectables();
}
 
// BSS normally fades out through BSS_Message and returns directly to Mania Mode.
// For this mod, intercept that final state and hand the scene to the built-in
// SpecialClear result screen instead.
static bool32 BSS_Message_State_SaveGameProgress_HOOK(bool32 skippedState) {
    if (skippedState || !bssRouteActive || !globals->specialCleared || bssResultStarted)
        return skippedState;

    AwardBSSReward();
    bssResultStarted = true;

    // Match the normal UFO result-screen handoff: hide stage layers and clear
    // stage entities before placing the ACTCLEAR result object.
    for (int32 l = 0; l < LAYER_COUNT; ++l) {
        TileLayer *layer = RSDK.GetTileLayer(l);
        if (layer)
            layer->drawGroup[0] = DRAWGROUP_COUNT;
    }

    Entity *current = SceneInfo->entity;
    for (int32 l = 0; l < SCENEENTITY_COUNT; ++l) {
        Entity *entity = RSDK_GET_ENTITY_GEN(l);
        if (entity->classID && entity != current)
            destroyEntity(entity);
    }

    ObjectClass_Compat *uiBackground = (ObjectClass_Compat *)Mod.FindObject("UIBackground");
    if (uiBackground && uiBackground->classID)
        RSDK.ResetEntitySlot(0, uiBackground->classID, NULL);

    if (!SpecialClear)
        SpecialClear = (ObjectSpecialClear *)Mod.FindObject("SpecialClear");

    if (SpecialClear && SpecialClear->classID) {
        RSDK.ResetEntitySlot(SLOT_ACTCLEAR, SpecialClear->classID, NULL);
        RSDK.AddDrawListRef(DRAWGROUP_COUNT - 2, SLOT_ACTCLEAR);

        EntitySpecialClear_Compat *result =
            (EntitySpecialClear_Compat *)RSDK.GetEntity(SLOT_ACTCLEAR);
        SaveRAM_Compat *saveRAM =
            SaveGame_GetSaveRAM_fn ? SaveGame_GetSaveRAM_fn() : NULL;

        if (result && result->classID == SpecialClear->classID) {
            result->isBSS        = true;
            result->messageType  = bssRouteIsSuper ? SC_MSG_SUPER : SC_MSG_GOTEMERALD;
            result->hasContinues = false;
            result->score        = saveRAM ? saveRAM->score : 0;
            result->score1UP     = saveRAM ? saveRAM->score1UP : 0;
            result->lives        = saveRAM ? saveRAM->lives : 0;
        }
    }

    // The original BSS message is the state currently being hooked, so hide
    // and stop it after the replacement result screen has been installed.
    if (current) {
        current->visible = false;
        current->state = StateMachine_None;
    }

    // Skip vanilla BSS_Message_State_SaveGameProgress so it cannot immediately
    // load Mania Mode over the result screen.
    return true;
}
