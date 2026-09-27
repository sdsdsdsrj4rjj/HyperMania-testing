#include "SpecialBS.h"

// -----------------------------------------------------------------------------
// HyperMania Blue Sphere special-stage bridge.
// Uses Mania's existing SpecialBS stages 1-7 and the built-in Super Emerald
// collectable instead of adding or replacing any game assets.

bool32 HM_BSS_SpecialStage = false;
int32 HM_BSS_SpecialStageID = 0;

bool32 BSS_Setup_Update_HOOK(bool32 skippedState) {
	RSDK_THIS(BSS_Setup);

	Mod.Super(BSS_Setup->classID, SUPER_UPDATE, NULL);

	// The normal Mania finish sequence places a silver/gold medal on the
	// playfield. For a HyperMania Super Emerald stage, turn that same target
	// into the existing Super Emerald collectable.
	if (HM_BSS_SpecialStage && !globals->specialCleared) {
		for (int32 x = 0; x < BSS_PLAYFIELD_W; ++x) {
			for (int32 y = 0; y < BSS_PLAYFIELD_H; ++y) {
				int32 pos = x * BSS_PLAYFIELD_H + y;
				if (BSS_Setup->playField[pos] == BSS_MEDAL_SILVER || BSS_Setup->playField[pos] == BSS_MEDAL_GOLD)
					BSS_Setup->playField[pos] = BSS_EMERALD_SUPER;
			}
		}
	}

	return false;
}

void HM_BSS_ResetStageState(void) {
	HM_BSS_SpecialStage = false;
	HM_BSS_SpecialStageID = 0;
}
