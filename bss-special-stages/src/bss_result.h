#ifndef BSS_STANDALONE_RESULT_H
#define BSS_STANDALONE_RESULT_H

#include "Game.h"

typedef struct ObjectBSSStandaloneResult {
    RSDK_OBJECT
} ObjectBSSStandaloneResult;

typedef struct EntityBSSStandaloneResult {
    RSDK_ENTITY
    StateMachine(state);
    bool32 isSuper;
    int32 timer;
    bool32 showFade;
    int32 fillColor;
    int32 score;
    int32 score1UP;
    int32 lives;
    int32 ringBonus;
    int32 perfectBonus;
    Vector2 messagePos1;
    Vector2 messagePos2;
    Vector2 scoreBonusPos;
    Vector2 ringBonusPos;
    Vector2 perfectBonusPos;
    int32 emeraldPositions[7];
    int32 emeraldSpeeds[7];
    Animator textAnimator;
    Animator bonusAnimator;
    Animator numbersAnimator;
    Animator emeraldsAnimator;
    uint16 aniFrames;
    uint16 sfxScoreAdd;
    uint16 sfxScoreTotal;
    uint16 sfxSpecialWarp;
    uint16 sfxEmerald;
} EntityBSSStandaloneResult;

extern ObjectBSSStandaloneResult *BSSStandaloneResult;

void BSSStandaloneResult_Update(void);
void BSSStandaloneResult_Draw(void);
void BSSStandaloneResult_Create(void *data);
void BSSStandaloneResult_Serialize(void);

#endif
