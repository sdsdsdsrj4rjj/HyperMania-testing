#include "bss_result.h"

ObjectBSSStandaloneResult *BSSStandaloneResult;

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
} SaveRAM_ResultCompat;

typedef struct {
    uint8 transferedEmeralds;
    uint8 padding1[3];
    uint8 superEmeralds;
    uint8 padding2[3];
} HM_SaveRAM_ResultCompat;

static SaveRAM_ResultCompat *(*SaveGame_GetSaveRAM_result)(void);
static void (*SaveGame_SaveGameState_result)(void);
static void (*GameProgress_ShuffleBSSID_result)(void);

typedef struct {
    HM_SaveRAM_ResultCompat *currentSave;
} HM_Global_ResultCompat;

static HM_Global_ResultCompat *(*HMAPI_GetGlobals_result)(void);

static void BSSResult_ResolveFunctions(void)
{
    if (!SaveGame_GetSaveRAM_result)
        SaveGame_GetSaveRAM_result =
            Mod.GetPublicFunction(NULL, "SaveGame_GetSaveRAM");
    if (!SaveGame_SaveGameState_result)
        SaveGame_SaveGameState_result =
            Mod.GetPublicFunction(NULL, "SaveGame_SaveGameState");
    if (!GameProgress_ShuffleBSSID_result)
        GameProgress_ShuffleBSSID_result =
            Mod.GetPublicFunction(NULL, "GameProgress_ShuffleBSSID");
    if (!HMAPI_GetGlobals_result) {
        HMAPI_GetGlobals_result =
            Mod.GetPublicFunction("HYPERMANIA", "HMAPI_GetGlobals");
        if (!HMAPI_GetGlobals_result)
            HMAPI_GetGlobals_result =
                Mod.GetPublicFunction("HyperMania", "HMAPI_GetGlobals");
    }
}

static void BSSResult_DrawNumbers(EntityBSSStandaloneResult *self,
                                   Vector2 *pos, int32 value)
{
    int32 cnt = value;
    int32 digitCount = value ? 0 : 1;

    while (cnt > 0) {
        ++digitCount;
        cnt /= 10;
    }

    int32 digit = 1;
    while (digitCount--) {
        self->numbersAnimator.frameID = value / digit % 10;
        RSDK.DrawSprite(&self->numbersAnimator, pos, true);
        digit *= 10;
        pos->x -= 0x90000;
    }
}

static void BSSResult_State_Setup(void);
static void BSSResult_State_EnterText(void);
static void BSSResult_State_AdjustText(void);
static void BSSResult_State_EnterBonuses(void);
static void BSSResult_State_ScoreDelay(void);
static void BSSResult_State_Tally(void);
static void BSSResult_State_ShowTotal(void);
static void BSSResult_State_Exit(void);

void BSSStandaloneResult_Create(void *data)
{
    RSDK_THIS(BSSStandaloneResult);

    BSSResult_ResolveFunctions();

    self->active = ACTIVE_NORMAL;
    self->visible = true;
    self->drawGroup = 14;
    self->timer = 512;
    self->fillColor = 0xF0F0F0;
    self->showFade = true;
    self->isSuper = data != NULL;

    SaveRAM_ResultCompat *saveRAM =
        SaveGame_GetSaveRAM_result ? SaveGame_GetSaveRAM_result() : NULL;

    self->score = saveRAM ? saveRAM->score : 0;
    self->score1UP = saveRAM ? saveRAM->score1UP : 0;
    self->lives = saveRAM ? saveRAM->lives : 0;

    typedef struct {
        RSDK_OBJECT
        int32 sphereCount;
        int32 pinkSphereCount;
        int32 rings;
    } BSSSetupResultCompat;

    BSSSetupResultCompat *bssSetup =
        (BSSSetupResultCompat *)Mod.FindObject("BSS_Setup");
    self->ringBonus = bssSetup ? 100 * bssSetup->rings : 0;
    self->perfectBonus = 50000;

    self->messagePos1.x = 0x1400000;
    self->messagePos1.y = 0x580000;
    self->messagePos2.x = -0x1400000;
    self->messagePos2.y = 0x700000;

    self->scoreBonusPos.x = 0x1E80000;
    self->scoreBonusPos.y = 0x8C0000;
    self->ringBonusPos.x = 0x3080000;
    self->ringBonusPos.y = 0xAC0000;
    self->perfectBonusPos.x = 0x4280000;
    self->perfectBonusPos.y = 0xBC0000;

    self->aniFrames =
        RSDK.LoadSpriteAnimation("Special/Results.bin", SCOPE_STAGE);

    self->sfxScoreAdd = RSDK.GetSfx("Global/ScoreAdd.wav");
    self->sfxScoreTotal = RSDK.GetSfx("Global/ScoreTotal.wav");
    self->sfxSpecialWarp = RSDK.GetSfx("Global/SpecialWarp.wav");
    self->sfxEmerald = RSDK.GetSfx("Special/Emerald.wav");

    RSDK.SetSpriteAnimation(self->aniFrames, 0, &self->textAnimator,
                            true, 0);
    RSDK.SetSpriteAnimation(self->aniFrames, 5, &self->bonusAnimator,
                            true, 0);
    RSDK.SetSpriteAnimation(self->aniFrames, 6, &self->numbersAnimator,
                            true, 0);
    RSDK.SetSpriteAnimation(self->aniFrames, 7, &self->emeraldsAnimator,
                            true, 0);

    for (int32 i = 0; i < 7; ++i) {
        self->emeraldPositions[i] = 0x1100000 + i * 0x200000;
        self->emeraldSpeeds[i] = -0xA0000 + i * -0xA000;
    }

    self->state = BSSResult_State_Setup;
}

static void BSSResult_DrawMessage(EntityBSSStandaloneResult *self,
                                   Vector2 *drawPos, int32 centerX)
{
    Vector2 verts[4];

    drawPos->x = centerX + self->messagePos1.x;
    drawPos->y = self->messagePos1.y;

    verts[0].x = drawPos->x - 0x740000;
    verts[0].y = drawPos->y - 0x140000;
    verts[1].x = drawPos->x + 0x680000;
    verts[1].y = verts[0].y;
    verts[2].x = drawPos->x + 0x780000;
    verts[2].y = drawPos->y - 0x40000;
    verts[3].x = drawPos->x - 0x640000;
    verts[3].y = verts[2].y;

    RSDK.DrawFace(verts, 4, 0, 0, 0, 0xFF, INK_NONE);

    if (self->isSuper) {
        self->textAnimator.frameID = 7;
        RSDK.DrawSprite(&self->textAnimator, drawPos, true);
        self->textAnimator.frameID = 8;
        RSDK.DrawSprite(&self->textAnimator, drawPos, true);
        self->textAnimator.frameID = 9;
        RSDK.DrawSprite(&self->textAnimator, drawPos, true);

        drawPos->x = centerX + self->messagePos2.x;
        drawPos->y = self->messagePos2.y;
        self->textAnimator.frameID = 10;
        RSDK.DrawSprite(&self->textAnimator, drawPos, true);
        self->textAnimator.frameID = 11;
        RSDK.DrawSprite(&self->textAnimator, drawPos, true);
        self->textAnimator.frameID = 13;
        RSDK.DrawSprite(&self->textAnimator, drawPos, true);
    }
    else {
        self->textAnimator.frameID = 1;
        RSDK.DrawSprite(&self->textAnimator, drawPos, true);
        self->textAnimator.frameID = 2;
        RSDK.DrawSprite(&self->textAnimator, drawPos, true);

        drawPos->x = centerX + self->messagePos2.x;
        drawPos->y = self->messagePos2.y;
        self->textAnimator.frameID = 3;
        RSDK.DrawSprite(&self->textAnimator, drawPos, true);
    }
}

void BSSStandaloneResult_Draw(void)
{
    RSDK_THIS(BSSStandaloneResult);

    BSSResult_ResolveFunctions();

    const int32 centerX = ScreenInfo->center.x << 16;
    Vector2 drawPos;

    SaveRAM_ResultCompat *saveRAM =
        SaveGame_GetSaveRAM_result ? SaveGame_GetSaveRAM_result() : NULL;

    HM_Global_ResultCompat *hm =
        self->isSuper && HMAPI_GetGlobals_result
            ? HMAPI_GetGlobals_result()
            : NULL;

    for (int32 i = 0; i < 7; ++i) {
        bool32 collected = false;

        if (self->isSuper) {
            if (hm && hm->currentSave)
                collected =
                    (hm->currentSave->superEmeralds & (1 << i)) != 0;
        }
        else if (saveRAM) {
            collected =
                (saveRAM->chaosEmeralds & (1 << i)) != 0;
        }

        self->emeraldsAnimator.frameID = collected ? i : 7;
        drawPos.x = centerX - 0x600000 + i * 0x200000;
        drawPos.y = self->emeraldPositions[i];
        RSDK.DrawSprite(&self->emeraldsAnimator, &drawPos, true);
    }

    BSSResult_DrawMessage(self, &drawPos, centerX);

    drawPos.x = centerX + self->scoreBonusPos.x - 0x560000;
    drawPos.y = self->scoreBonusPos.y;
    self->bonusAnimator.frameID = 4;
    RSDK.DrawSprite(&self->bonusAnimator, &drawPos, true);
    self->bonusAnimator.frameID = 6;
    drawPos.x += 0x660000;
    RSDK.DrawSprite(&self->bonusAnimator, &drawPos, true);
    drawPos.x += 0x430000;
    BSSResult_DrawNumbers(self, &drawPos, self->score);

    drawPos.x = centerX + self->ringBonusPos.x - 0x560000;
    drawPos.y = self->ringBonusPos.y;
    self->bonusAnimator.frameID = 0;
    RSDK.DrawSprite(&self->bonusAnimator, &drawPos, true);
    drawPos.x += 0x320000;
    self->bonusAnimator.frameID = 3;
    RSDK.DrawSprite(&self->bonusAnimator, &drawPos, true);
    self->bonusAnimator.frameID = 6;
    drawPos.x += 0x340000;
    RSDK.DrawSprite(&self->bonusAnimator, &drawPos, true);
    drawPos.x += 0x430000;
    BSSResult_DrawNumbers(self, &drawPos, self->ringBonus);

    drawPos.x = centerX + self->perfectBonusPos.x - 0x560000;
    drawPos.y = self->perfectBonusPos.y;
    self->bonusAnimator.frameID = 1;
    RSDK.DrawSprite(&self->bonusAnimator, &drawPos, true);
    drawPos.x += 0x320000;
    self->bonusAnimator.frameID = 3;
    RSDK.DrawSprite(&self->bonusAnimator, &drawPos, true);
    self->bonusAnimator.frameID = 6;
    drawPos.x += 0x340000;
    RSDK.DrawSprite(&self->bonusAnimator, &drawPos, true);
    drawPos.x += 0x430000;
    BSSResult_DrawNumbers(self, &drawPos, self->perfectBonus);

    if (self->showFade)
        RSDK.FillScreen(self->fillColor, self->timer,
                        self->timer - 128, self->timer - 256);
}

void BSSStandaloneResult_Update(void)
{
    RSDK_THIS(BSSStandaloneResult);
    StateMachine_Run(self->state);
}

static void BSSStandaloneResult_StateSetup(void)
{
    RSDK_THIS(BSSStandaloneResult);

    if (self->timer <= 0) {
        self->timer = 0;
        self->showFade = false;
        self->state = BSSResult_State_EnterText;
        Music_PlayTrack(TRACK_ACTCLEAR);
    }
    else {
        self->timer -= 16;
    }
}

static void BSSResult_State_EnterText(void)
{
    RSDK_THIS(BSSStandaloneResult);

    if (self->messagePos1.x > 0)
        self->messagePos1.x -= 0x100000;
    if (self->messagePos2.x < 0)
        self->messagePos2.x += 0x100000;

    if (++self->timer >= 48) {
        self->timer = 0;
        self->state = BSSResult_State_AdjustText;
    }
}

static void BSSResult_State_AdjustText(void)
{
    RSDK_THIS(BSSStandaloneResult);

    self->messagePos1.y -= 0x10000;
    self->messagePos2.y -= 0x10000;

    if (++self->timer >= 48) {
        self->timer = 0;
        self->state = BSSResult_State_EnterBonuses;
    }
}

static void BSSResult_State_EnterBonuses(void)
{
    RSDK_THIS(BSSStandaloneResult);

    if (self->scoreBonusPos.x > 0)
        self->scoreBonusPos.x -= 0x100000;
    if (self->ringBonusPos.x > 0)
        self->ringBonusPos.x -= 0x100000;
    if (self->perfectBonusPos.x > 0)
        self->perfectBonusPos.x -= 0x100000;

    for (int32 i = 0; i < 7; ++i) {
        self->emeraldSpeeds[i] += 0x4000;
        self->emeraldPositions[i] += self->emeraldSpeeds[i];
        if (self->emeraldSpeeds[i] >= 0 &&
            self->emeraldPositions[i] > 0x700000) {
            self->emeraldPositions[i] = 0x700000;
            self->emeraldSpeeds[i] = -(self->emeraldSpeeds[i] >> 1);
        }
    }

    if (self->perfectBonusPos.x <= 0) {
        self->timer = 0;
        self->state = BSSResult_State_ScoreDelay;
    }
}

static void BSSResult_State_ScoreDelay(void)
{
    RSDK_THIS(BSSStandaloneResult);

    if (++self->timer >= 120) {
        self->timer = 0;
        if (BSSStandaloneResult->sfxEmerald)
            RSDK.PlaySfx(BSSStandaloneResult->sfxEmerald, false, 0xFF);
        self->state = BSSResult_State_Tally;
    }
}

static void BSSResult_State_Tally(void)
{
    RSDK_THIS(BSSStandaloneResult);

    if (self->ringBonus > 0) {
        self->ringBonus -= 100;
        self->score += 100;
    }

    if (self->perfectBonus > 0) {
        self->perfectBonus -= 100;
        self->score += 100;
    }

    if (ControllerInfo->keyA.press || ControllerInfo->keyStart.press) {
        self->score += self->ringBonus + self->perfectBonus;
        self->ringBonus = 0;
        self->perfectBonus = 0;
    }

    if (self->ringBonus + self->perfectBonus <= 0) {
        self->timer = 0;
        if (BSSStandaloneResult->sfxScoreTotal)
            RSDK.PlaySfx(BSSStandaloneResult->sfxScoreTotal, false, 0xFF);
        self->state = BSSResult_State_ShowTotal;
    }
}

static void BSSResult_State_ShowTotal(void)
{
    RSDK_THIS(BSSStandaloneResult);

    if (++self->timer >= 180) {
        self->timer = 0;
        self->showFade = true;
        self->state = BSSResult_State_Exit;
    }
}

static void BSSResult_State_Exit(void)
{
    RSDK_THIS(BSSStandaloneResult);

    if (self->timer >= 768) {
        SaveRAM_ResultCompat *saveRAM =
            SaveGame_GetSaveRAM_result ? SaveGame_GetSaveRAM_result() : NULL;

        if (saveRAM)
            saveRAM->score = self->score;

        if (GameProgress_ShuffleBSSID_result)
            GameProgress_ShuffleBSSID_result();

        if (SaveGame_SaveGameState_result)
            SaveGame_SaveGameState_result();

        if (globals)
            globals->blueSpheresInit = true;

#if MANIA_USE_PLUS
        if (globals && globals->gameMode == MODE_ENCORE)
            RSDK.SetScene("Encore Mode", "");
        else
            RSDK.SetScene("Mania Mode", "");
#else
        RSDK.SetScene("Mania Mode", "");
#endif

        if (saveRAM)
            SceneInfo->listPos = saveRAM->storedStageID;

        BSSStandaloneResult_Finished();
        bssResultEntity = NULL;
        RSDK.LoadScene();
        return;
    }

    self->timer += 8;
}

void BSSStandaloneResult_Serialize(void)
{
}
