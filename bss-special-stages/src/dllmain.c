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
static void (*BSS_Setup_State_GlobeEmerald_fn)(void);
static void (*SpecialRing_State_Flash_fn)(void);
static void (*SpecialRing_State_HPZ_Warp_fn)(void);
static void (*SpecialClear_State_TallyScore_fn)(void);
static void (*SpecialClear_State_ShowTotalScore_Continues_fn)(void);
static void (*SpecialClear_State_ShowTotalScore_NoContinues_fn)(void);
static void (*SpecialClear_State_ExitResults_fn)(void);
static void (*SpecialClear_State_ExitFinishMessage_fn)(void);
static void (*SpecialClear_State_ExitFadeOut_fn)(void);
static void (*SpecialClear_StageLoad_fn)(void);
static void (*SpecialClear_DrawNumbers_fn)(Vector2 *pos, int32 value);

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

    // The regular HyperMania package uses HYPERMANIA as its mod ID.
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

static bool32 HyperManiaDetected(void) {
    ResolveHyperManiaAPI();
    if (HMAPI_GetGlobals_fn)
        return true;

    // SHC/older HyperMania builds have these objects even when API lookup
    // happens before HyperMania finishes registering its public functions.
    if (Mod.FindObject("HPZIntro"))
        return true;
    if (Mod.FindObject("HPZSetup"))
        return true;
    if (Mod.FindObject("HPZEmerald"))
        return true;

    return false;
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

typedef struct ObjectClass_Compat {
    RSDK_OBJECT
} ObjectClass_Compat;

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

typedef EntitySpecialClear_Compat EntitySpecialClear;

#define SC_MSG_SPECIALCLEAR 0
#define SC_MSG_GOTEMERALD 1
#define SC_MSG_SUPER 3

static bool32 bssRewardGiven;
static bool32 bssResultStarted;
static uint16 bssEmeraldResultFrames = (uint16)-1;
static Animator bssEmeraldResultAnimator;

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

static void ReplaceFinishTarget(void) {}
// HandleCollectableMovement copies playField[] into each BSS_Collectable's
// type field. Changing only playField therefore leaves the visible finish
// object as a silver/gold medal until the next rebuild. Replace the already
// spawned collectable too so its renderer uses the emerald animator.

/*
 * Runs after the normal entity updates and before drawing. This is important:
 * BSS_Setup writes the finish medal during SetupFinishSequence and then rebuilds
 * the visible collectables from playField. Doing the replacement in late update
 * means the final frame uses the emerald type without replacing any engine hook.
 */
static void BSS_Collectable_Draw_HOOK(void) {
    EntityBSS_Collectable *self = (EntityBSS_Collectable *)SceneInfo->entity;
    Vector2 drawPos;

    if (!BSS_Collectable)
        BSS_Collectable = (ObjectBSS_Collectable *)Mod.FindObject("BSS_Collectable");
    if (!BSS_Collectable)
        return;

    // Only reinterpret the finish types while this UFO->BSS route is active.
    // Outside this route, type 18 is still the vanilla silver medal.
    if (bssRouteActive && !bssRouteIsSuper && self->type == 18) {
        // Use the game's already-colored Chaos Emerald art instead of the
        // white BSS placeholder palette.
        if (bssEmeraldResultFrames == (uint16)-1)
            bssEmeraldResultFrames = RSDK.LoadSpriteAnimation("Special/Results.bin", SCOPE_STAGE);

        if (bssEmeraldResultFrames != (uint16)-1) {
            RSDK.SetSpriteAnimation(bssEmeraldResultFrames, 7, &bssEmeraldResultAnimator, true,
                                    ClampStageID(bssRouteStage));
            bssEmeraldResultAnimator.frameID = ClampStageID(bssRouteStage);
            RSDK.DrawSprite(&bssEmeraldResultAnimator, NULL, true);
            return;
        }

        BSS_Collectable->sphereAnimator[16].frameID = self->animator.frameID >> 1;
        RSDK.DrawSprite(&BSS_Collectable->sphereAnimator[16], NULL, true);
        return;
    }

    // Type 17 is the Super Emerald, used only by the HyperMania route.
    if (bssRouteActive && bssRouteIsSuper && self->type == 17) {
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


static void PatchBSSFinishTile(void) {
    if (!bssRouteActive)
        return;
    if (!BSS_Setup)
        BSS_Setup = (ObjectBSS_Setup *)Mod.FindObject("BSS_Setup");
    if (!BSS_Setup)
        return;

    const uint16 target = bssRouteIsSuper ? BSS_EMERALD_SUPER : BSS_MEDAL_SILVER;
    for (int32 i = 0; i < 0x400; ++i) {
        if (BSS_Setup->playField[i] == BSS_MEDAL_SILVER ||
            BSS_Setup->playField[i] == BSS_MEDAL_GOLD) {
            BSS_Setup->playField[i] = target;
            break;
        }
    }
}

static bool32 BSS_Setup_State_GlobeEmerald_HOOK(bool32 skippedState) {
    if (skippedState || !BSS_Setup_State_GlobeEmerald_fn)
        return skippedState;

    if (!BSS_Setup)
        BSS_Setup = (ObjectBSS_Setup *)Mod.FindObject("BSS_Setup");

    // Keep the contextual Chaos reward as type 18 while preparing the field,
    // then translate that one tile to the engine's native Chaos type (16)
    // immediately before native collision handling. This prevents the vanilla
    // silver-medal branch from awarding a medal.
    PatchBSSFinishTile();

    if (bssRouteActive && !bssRouteIsSuper && BSS_Setup) {
        for (int32 i = 0; i < 0x400; ++i) {
            if (BSS_Setup->playField[i] == BSS_MEDAL_SILVER) {
                BSS_Setup->playField[i] = BSS_EMERALD_CHAOS;
                break;
            }
        }
    }

    BSS_Setup_State_GlobeEmerald_fn();
    return true;
}

typedef struct EntityBSS_Message_Compat {
    RSDK_ENTITY
    StateMachine(state);
    int32 timer;
    int32 messageFinishTimer;
    bool32 fadeEnabled;
    int32 color;
    bool32 saveInProgress;
    Animator leftAnimator;
    Animator rightAnimator;
} EntityBSS_Message_Compat;

static void BSS_OnLateUpdate(void *data) { (void)data; }
// BSS normally returns directly to Mania Mode after its black finish fade.
// Instead, hand the completed stage to the built-in SpecialClear result screen.
static bool32 BSS_Message_State_SaveGameProgress_HOOK(bool32 skippedState) {
    // Reaching BSS_Message_State_SaveGameProgress already means the BSS stage
    // reached its completed/finish state. Do not wait for specialCleared here;
    // that flag may only be set by GameProgress tracking that we are replacing.
    if (skippedState || !bssRouteActive || bssResultStarted)
        return skippedState;

    AwardBSSReward();
    bssResultStarted = true;

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

    // SpecialClear normally gets its aniFrames/sfx initialized by its
    // scene StageLoad. Blue Spheres does not normally display this object,
    // so initialize those static resources before creating it.
    if (SpecialClear && SpecialClear_StageLoad_fn)
        SpecialClear_StageLoad_fn();

    if (SpecialClear && SpecialClear->classID) {
        RSDK.ResetEntitySlot(1, SpecialClear->classID, NULL);
        RSDK.AddDrawListRef(DRAWGROUP_COUNT - 2, 1);

        EntitySpecialClear_Compat *result =
            (EntitySpecialClear_Compat *)RSDK.GetEntity(1);
        SaveRAM_Compat *saveRAM =
            SaveGame_GetSaveRAM_fn ? SaveGame_GetSaveRAM_fn() : NULL;

        if (result && result->classID == SpecialClear->classID) {
            result->isBSS = true;
            result->messageType = bssRouteIsSuper ? SC_MSG_SUPER : SC_MSG_GOTEMERALD;
            result->hasContinues = false;
            result->score = saveRAM ? saveRAM->score : 0;
            result->score1UP = saveRAM ? saveRAM->score1UP : 0;
            result->lives = saveRAM ? saveRAM->lives : 0;
        }
    }

    if (current) {
        current->visible = false;
        ((EntityBSS_Message_Compat *)current)->state = StateMachine_None;
    }

    return true;
}

// HyperMania's SpecialClear tally hook is low priority and is intended for its
// HPZ result flow. For our BSS result, execute the normal Mania tally state
// ourselves and skip the main state so that HPZ-only low-priority code is skipped.
static bool32 SpecialClear_State_TallyScore_BSS_HOOK(bool32 skippedState) {
    if (!bssResultStarted || !bssRouteActive)
        return skippedState;

    EntitySpecialClear_Compat *self = (EntitySpecialClear_Compat *)SceneInfo->entity;
    if (!self || !self->isBSS || !SpecialClear_State_TallyScore_fn)
        return skippedState;

    SpecialClear_State_TallyScore_fn();
    return true;
}

// HyperMania's ShowTotalScore hooks can switch a BSS result into its private
// HPZ result states. Run this hook after that processing and force the normal
// result-screen fade-out path for our standalone BSS mod.
static bool32 SpecialClear_State_ShowTotalScore_BSS_HOOK(bool32 skippedState) {
    if (!bssResultStarted || !bssRouteActive)
        return skippedState;

    EntitySpecialClear_Compat *self = (EntitySpecialClear_Compat *)SceneInfo->entity;
    if (!self || !self->isBSS)
        return skippedState;

    // Do not jump straight to ExitResults here. Run the normal Mania
    // ShowTotalScore state so the result screen remains on-screen and the
    // normal score/fade timing is preserved. This also keeps HyperMania's
    // HPZ-only result transition out of this BSS route.
    if (self->hasContinues) {
        if (SpecialClear_State_ShowTotalScore_Continues_fn)
            SpecialClear_State_ShowTotalScore_Continues_fn();
    }
    else {
        if (SpecialClear_State_ShowTotalScore_NoContinues_fn)
            SpecialClear_State_ShowTotalScore_NoContinues_fn();
    }

    return true;
}

static bool32 SpecialClear_State_ExitFinishMessage_BSS_HOOK(bool32 skippedState) {
    if (!bssResultStarted || !bssRouteActive)
        return skippedState;

    EntitySpecialClear_Compat *self = (EntitySpecialClear_Compat *)SceneInfo->entity;
    if (!self || !self->isBSS)
        return skippedState;

    self->timer = 0;
    self->showFade = true;
    if (SpecialClear && SpecialClear->sfxSpecialWarp)
        RSDK.PlaySfx(SpecialClear->sfxSpecialWarp, false, 0xFF);
    if (SpecialClear_State_ExitResults_fn)
        self->state = SpecialClear_State_ExitResults_fn;

    return true;
}

static bool32 SpecialClear_State_ExitFadeOut_BSS_HOOK(bool32 skippedState) {
    if (!skippedState && bssRouteActive && bssResultStarted && SpecialClear_State_ExitFadeOut_fn)
        SpecialClear_State_ExitFadeOut_fn();

    if (bssResultStarted) {
        bssRouteActive = false;
        bssRouteIsSuper = false;
        bssRouteStage = 0;
        bssRewardGiven = false;
        bssResultStarted = false;
    }

    return true;
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

static void SpecialRing_State_BSSSuperWarp(void) {
    EntitySpecialRing_Compat *self = (EntitySpecialRing_Compat *)SceneInfo->entity;

    SaveRAM_Compat *saveRAM = SaveGame_GetSaveRAM_fn ? SaveGame_GetSaveRAM_fn() : NULL;
    if (!saveRAM || self->id <= 0)
        return;

    bssRouteActive = true;
    bssRouteIsSuper = true;
    bssRouteStage = ClampStageID(self->id - 1);
    bssRewardGiven = false;
    bssResultStarted = false;
    bssEmeraldResultFrames = (uint16)-1;
    memset(&bssEmeraldResultAnimator, 0, sizeof(bssEmeraldResultAnimator));

    if (SaveGame_SaveGameState_fn)
        SaveGame_SaveGameState_fn();

    RSDK.PlaySfx(RSDK.GetSfx("Global/SpecialWarp.wav"), false, 0xFE);
    destroyEntity(self);

    saveRAM->storedStageID = SceneInfo->listPos;
    RSDK.SetScene("Blue Spheres", "");
    SceneInfo->listPos += bssRouteStage;
    if (Zone_StartFadeOut_fn)
        Zone_StartFadeOut_fn(10, 0xF0F0F0);
    if (Music_Stop_fn)
        Music_Stop_fn();
}

// HyperMania's high-priority Flash hook normally sends the completed-Chaos
// route to its private HPZ warp. This low-priority hook runs afterward and
// redirects that state into the BSS Super Emerald route.
static bool32 SpecialRing_State_Flash_BSS_HOOK(bool32 skippedState) {
    // HyperMania's own Flash hook can report the state as skipped after
    // redirecting it toward Hidden Palace. We still need to inspect and
    // override that transition for the standalone BSS Super Emerald route.
    if (!HyperManiaDetected())
        return false;

    EntitySpecialRing_Compat *self = (EntitySpecialRing_Compat *)SceneInfo->entity;
    SaveRAM_Compat *saveRAM = SaveGame_GetSaveRAM_fn ? SaveGame_GetSaveRAM_fn() : NULL;
    if (!saveRAM || self->id <= 0)
        return false;

    const bool32 chaosComplete = saveRAM->chaosEmeralds == 0x7F;
    const bool32 superComplete = HyperManiaSuperEmeraldsComplete();

    if (chaosComplete && !superComplete) {
        bssRouteActive = true;
        bssRouteIsSuper = true;
        bssRouteStage = ClampStageID(self->id - 1);
        bssRewardGiven = false;
        bssResultStarted = false;
        self->warpTimer = 0;
        self->state = SpecialRing_State_BSSSuperWarp;
        // The HyperMania hook normally changes this state to HPZ_Warp.
        // Returning true prevents the original Flash state from overwriting
        // our BSS state after this hook has selected it.
        return true;
    }

    return false;
}

static bool32 SpecialRing_State_HPZ_Warp_BSS_HOOK(bool32 skippedState) {
    (void)skippedState;

    if (!HyperManiaDetected())
        return false;

    SaveRAM_Compat *saveRAM = SaveGame_GetSaveRAM_fn ? SaveGame_GetSaveRAM_fn() : NULL;
    if (!saveRAM)
        return false;

    const bool32 chaosComplete = saveRAM->chaosEmeralds == 0x7F;
    const bool32 superComplete = HyperManiaSuperEmeraldsComplete();

    if (chaosComplete && !superComplete) {
        EntitySpecialRing_Compat *self = (EntitySpecialRing_Compat *)SceneInfo->entity;
        if (self && self->id > 0) {
            bssRouteActive = true;
            bssRouteIsSuper = true;
            bssRouteStage = ClampStageID(self->id - 1);
            bssRewardGiven = false;
            bssResultStarted = false;
            bssEmeraldResultFrames = (uint16)-1;
            memset(&bssEmeraldResultAnimator, 0, sizeof(bssEmeraldResultAnimator));
            self->warpTimer = 0;
            self->state = SpecialRing_State_BSSSuperWarp;
            return true;
        }
    }

    return false;
}

static bool32 SpecialRing_State_Warp_HOOK(bool32 skippedState) {
    (void)skippedState;
    EntitySpecialRing_Compat *self = (EntitySpecialRing_Compat *)SceneInfo->entity;

    SaveRAM_Compat *saveRAM = SaveGame_GetSaveRAM_fn ? SaveGame_GetSaveRAM_fn() : NULL;
    if (!saveRAM || self->id <= 0) return false;

    const bool32 chaosComplete = saveRAM->chaosEmeralds == 0x7F;
    const bool32 superComplete = HyperManiaSuperEmeraldsComplete();

    if (chaosComplete && HyperManiaDetected() && !superComplete) {
        bssRouteActive = true;
        bssRouteIsSuper = true;
        bssRouteStage = ClampStageID(self->id - 1);
    }
    else if (!chaosComplete) {
        bssRouteActive = true;
        bssRouteIsSuper = false;
        bssRouteStage = ClampStageID(saveRAM->nextSpecialStage);
        bssRewardGiven = false;
        bssResultStarted = false;
        bssEmeraldResultFrames = (uint16)-1;
        memset(&bssEmeraldResultAnimator, 0, sizeof(bssEmeraldResultAnimator));
    }
    else {
        return false;
    }

    // Match the engine's normal Special Ring/Star Post transition sequence.
    if (SaveGame_SaveGameState_fn) SaveGame_SaveGameState_fn();
    RSDK.PlaySfx(RSDK.GetSfx("Global/SpecialWarp.wav"), false, 0xFE);


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

    // Resolve the built-in BSS objects used by the finish-tile and renderer hooks.
    BSS_Collectable = (ObjectBSS_Collectable *)Mod.FindObject("BSS_Collectable");
    BSS_Setup       = (ObjectBSS_Setup *)Mod.FindObject("BSS_Setup");

    SaveGame_GetSaveRAM_fn = Mod.GetPublicFunction(NULL, "SaveGame_GetSaveRAM");
    SaveGame_SaveGameState_fn = Mod.GetPublicFunction(NULL, "SaveGame_SaveGameState");
    GameProgress_GiveEmerald_fn = Mod.GetPublicFunction(NULL, "GameProgress_GiveEmerald");
    SaveGame_SetEmerald_fn = Mod.GetPublicFunction(NULL, "SaveGame_SetEmerald");
    Zone_StartFadeOut_fn = Mod.GetPublicFunction(NULL, "Zone_StartFadeOut");
    Music_Stop_fn = Mod.GetPublicFunction(NULL, "Music_Stop");
    ResolveHyperManiaAPI();
    SpecialRing_State_Flash_fn = Mod.GetPublicFunction(NULL, "SpecialRing_State_Flash");
    SpecialRing_State_HPZ_Warp_fn = Mod.GetPublicFunction("HYPERMANIA", "SpecialRing_State_HPZ_Warp");
    if (!SpecialRing_State_HPZ_Warp_fn)
        SpecialRing_State_HPZ_Warp_fn = Mod.GetPublicFunction("HyperMania", "SpecialRing_State_HPZ_Warp");
    if (!SpecialRing_State_HPZ_Warp_fn)
        SpecialRing_State_HPZ_Warp_fn = Mod.GetPublicFunction(NULL, "SpecialRing_State_HPZ_Warp");
    BSS_Message_State_SaveGameProgress_fn =
        Mod.GetPublicFunction(NULL, "BSS_Message_State_SaveGameProgress");
    BSS_Setup_State_GlobeEmerald_fn =
        Mod.GetPublicFunction(NULL, "BSS_Setup_State_GlobeEmerald");
    SpecialClear_State_TallyScore_fn =
        Mod.GetPublicFunction(NULL, "SpecialClear_State_TallyScore");
    SpecialClear_State_ShowTotalScore_Continues_fn =
        Mod.GetPublicFunction(NULL, "SpecialClear_State_ShowTotalScore_Continues");
    SpecialClear_State_ShowTotalScore_NoContinues_fn =
        Mod.GetPublicFunction(NULL, "SpecialClear_State_ShowTotalScore_NoContinues");
    SpecialClear_State_ExitResults_fn =
        Mod.GetPublicFunction(NULL, "SpecialClear_State_ExitResults");
    SpecialClear_State_ExitFinishMessage_fn =
        Mod.GetPublicFunction(NULL, "SpecialClear_State_ExitFinishMessage");
    SpecialClear_State_ExitFadeOut_fn =
        Mod.GetPublicFunction(NULL, "SpecialClear_State_ExitFadeOut");
    SpecialClear_StageLoad_fn =
        Mod.GetPublicFunction(NULL, "SpecialClear_StageLoad");
    SpecialClear_DrawNumbers_fn =
        Mod.GetPublicFunction(NULL, "SpecialClear_DrawNumbers");

    void (*warpState)(void) = Mod.GetPublicFunction(NULL, "SpecialRing_State_Warp");
    if (warpState)
        Mod.RegisterStateHook(warpState, SpecialRing_State_Warp_HOOK, 1);
    if (SpecialRing_State_Flash_fn)
        Mod.RegisterStateHook(SpecialRing_State_Flash_fn, SpecialRing_State_Flash_BSS_HOOK, 0);
    if (SpecialRing_State_HPZ_Warp_fn)
        Mod.RegisterStateHook(SpecialRing_State_HPZ_Warp_fn, SpecialRing_State_HPZ_Warp_BSS_HOOK, 0);
    if (BSS_Setup_State_GlobeEmerald_fn)
        Mod.RegisterStateHook(BSS_Setup_State_GlobeEmerald_fn, BSS_Setup_State_GlobeEmerald_HOOK, 1);
    if (BSS_Message_State_SaveGameProgress_fn)
        Mod.RegisterStateHook(BSS_Message_State_SaveGameProgress_fn,
                              BSS_Message_State_SaveGameProgress_HOOK, 1);

    if (SpecialClear_State_TallyScore_fn)
        Mod.RegisterStateHook(SpecialClear_State_TallyScore_fn,
                              SpecialClear_State_TallyScore_BSS_HOOK, 1);
    if (SpecialClear_State_ShowTotalScore_Continues_fn)
        Mod.RegisterStateHook(SpecialClear_State_ShowTotalScore_Continues_fn,
                              SpecialClear_State_ShowTotalScore_BSS_HOOK, 0);
    if (SpecialClear_State_ShowTotalScore_NoContinues_fn)
        Mod.RegisterStateHook(SpecialClear_State_ShowTotalScore_NoContinues_fn,
                              SpecialClear_State_ShowTotalScore_BSS_HOOK, 0);
    if (SpecialClear_State_ExitFinishMessage_fn)
        Mod.RegisterStateHook(SpecialClear_State_ExitFinishMessage_fn, SpecialClear_State_ExitFinishMessage_BSS_HOOK, 1);
    if (SpecialClear_State_ExitFadeOut_fn)
        Mod.RegisterStateHook(SpecialClear_State_ExitFadeOut_fn, SpecialClear_State_ExitFadeOut_BSS_HOOK, 1);
    MOD_REGISTER_OBJ_OVERLOAD(BSS_Collectable, NULL, NULL, NULL, BSS_Collectable_Draw_HOOK, NULL, NULL, NULL, NULL, NULL);

    Mod.AddModCallback(MODCB_ONLATEUPDATE, BSS_OnLateUpdate);
    return true;
}
#endif
