#pragma once

extern bool32 HM_BSS_SpecialStage;
extern int32 HM_BSS_SpecialStageID;
extern bool32 HM_BSS_SuperEmerald;

extern void (*BSS_Message_State_LoadPrevScene)(void);
extern void (*UFO_Setup_State_ShowStartMessage)(void);

bool32 BSS_Message_State_LoadPrevScene_HOOK(bool32 skippedState);
bool32 UFO_Setup_State_ShowStartMessage_HOOK(bool32 skippedState);

void HM_BSS_ResetStageState(void);

#define OBJ_BSS_RESULTS_SETUP \
    IMPORT_PUBLIC_FUNC(BSS_Message_State_LoadPrevScene); \
    IMPORT_PUBLIC_FUNC(UFO_Setup_State_ShowStartMessage); \
    HOOK_IMPORTED_STATE(BSS_Message_State_LoadPrevScene, 1); \
    HOOK_IMPORTED_STATE(UFO_Setup_State_ShowStartMessage, 1)
