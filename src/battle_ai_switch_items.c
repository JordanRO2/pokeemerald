#include "global.h"
#include "battle.h"
#include "battle_ai_script_commands.h"
#include "battle_anim.h"
#include "battle_controllers.h"
#include "battle_main.h"
#include "data.h"
#include "pokemon.h"
#include "random.h"
#include "util.h"
#include "constants/abilities.h"
#include "constants/item_effects.h"
#include "constants/items.h"
#include "constants/moves.h"

// Forward declarations
static bool8 HasViableSwitch(void);
static bool8 IsStatusMoveThatAffectsWonderGuard(u16 move);
static bool8 FindMonThatAbsorbsOpponentsMove(void);
static bool8 IsPredictedMoveSuperEffectiveAgainst(u8 battler, u8 potentialSwitchIn, u16 predictedMove);
static u16 PredictOpponentMove(u8 opponent);
static s32 EstimateOpponentDamage(u8 battler, u8 opponent);
static bool8 ShouldHealConsideringBattleContext(void);
static bool8 HasSuperEffectiveMoveAgainstOpponents(bool8 noRng);
static bool8 FindMonWithFlagsAndSuperEffective(u8 flags, u8 moduloPercent);
static bool8 ShouldUseItem(void);
static bool8 IsMoveEffectiveAgainstAbility(u16 move, u8 battler, u16 itemId); // Added third parameter itemId
static u8 TypeEffectiveness(u8 moveType, u8 targetType); // Changed return type to u8
static bool8 IsHazardousSwitch(u8 battler);
static bool8 ShouldSwitchToRapidSpinUserIfHazards(void);
static s32 CalculateTypeAdvantageScore(u8 monId, u8 opposingBattler); // Added parameters monId, opposingBattler
static s32 CalculateAbilityAdvantageScore(u8 monId, u8 opposingBattler); // Added parameters monId, opposingBattler
static bool8 WillTakeSignificantHazardDamage(u8 monId); // Changed parameter to u8 monId
static bool8 HasStatBoosts(u8 monId); // Changed parameter to u8 monId
static bool8 IsValidSwitchTarget(u8 monId, u8 battlerIn1, u8 battlerIn2); // New forward declaration, parameters added
static bool8 EvaluateConditionCure(const u8 *itemEffects); // Added const u8* itemEffects as parameter
static int EvaluateXStatItem(const u8 *itemEffects); // Added const u8* itemEffects as parameter
static int ShouldUseGuardSpec(void);
static u16 CalculateHealAmount(u16 currentHp, u16 maxHp); // Added currentHp and maxHp parameters
static bool8 HasPriorityMove(u8 opponent); // Changed parameter to u8 opponent
static bool8 IsMoveIndirectDamage(u16 move);
static bool8 UsingFullRestore(void);
static bool8 UsingHyperPotion(void);
static bool8 UsingSuperPotion(void);
static bool8 HasPriorityMove(u8 opponent);
static u8 GetAI_ItemType(u16 itemId, const u8 *itemEffect)



static bool8 IsSemiInvulnerableMove(u8 battler)
{
    u16 lastMove = gLastUsedMoves[battler];
    return (lastMove == MOVE_FLY || lastMove == MOVE_DIG || lastMove == MOVE_DIVE);
}

static bool8 ShouldSwitchIfPerishSong(void)
{
    // Check if the active Pokémon is affected by Perish Song and the timer is running out.
    if ((gStatuses3[gActiveBattler] & STATUS3_PERISH_SONG)
        && gDisableStructs[gActiveBattler].perishSongTimer <= 1) // Switch when 1 or fewer turns are left
    {
        // Check if the last used move is a semi-invulnerable move.
        if (IsSemiInvulnerableMove(gActiveBattler))
            return FALSE;

        // Check if Protect or Detect is active, as it may protect from a final Perish Song faint.
        if (gProtectStructs[gActiveBattler].protected)
            return FALSE;

        // Ensure that the Pokémon is not trapped by Shadow Tag, Mean Look, or similar effects.
        if (!(gBattleMons[gActiveBattler].status2 & STATUS2_ESCAPE_PREVENTION) 
            && !(ABILITY_ON_OPPOSING_FIELD(gActiveBattler, ABILITY_SHADOW_TAG))
            && !(ABILITY_ON_OPPOSING_FIELD(gActiveBattler, ABILITY_ARENA_TRAP)
                && !IS_BATTLER_OF_TYPE(gActiveBattler, TYPE_FLYING)
                && !ABILITY_PRESENT(gActiveBattler, ABILITY_LEVITATE)))
        {
            // Check if the AI has a viable Pokémon to switch into.
            if (HasViableSwitch())
            {
                // Mark the switch action for the AI.
                *(gBattleStruct->AI_monToSwitchIntoId + gActiveBattler) = PARTY_SIZE;
                BtlController_EmitTwoReturnValues(BUFFER_B, B_ACTION_SWITCH, 0);
                return TRUE;
            }
        }
    }
    return FALSE;
}

static bool8 HasViableSwitch(void)
{
    s32 firstId, lastId;
    struct Pokemon *party;
    s32 i;

    if (GetBattlerSide(gActiveBattler) == B_SIDE_PLAYER)
        party = gPlayerParty;
    else
        party = gEnemyParty;

    firstId = 0;
    lastId = PARTY_SIZE;

    for (i = firstId; i < lastId; i++)
    {
        if (GetMonData(&party[i], MON_DATA_HP) > 0
            && GetMonData(&party[i], MON_DATA_SPECIES_OR_EGG) != SPECIES_NONE
            && GetMonData(&party[i], MON_DATA_SPECIES_OR_EGG) != SPECIES_EGG
            && i != gBattlerPartyIndexes[gActiveBattler])
        {
            return TRUE;
        }
    }
    return FALSE;
}

static bool8 ShouldSwitchIfWonderGuard(void)
{
    u8 opposingPosition;
    u8 opposingBattler;
    u8 moveFlags;
    s32 i, j;
    s32 firstId, lastId;
    struct Pokemon *party = NULL;
    u16 move;

    // Allow consideration in double battles if Wonder Guard is blocking moves
    if (!(gBattleTypeFlags & BATTLE_TYPE_DOUBLE) && !(gBattleTypeFlags & BATTLE_TYPE_ARENA))
    {
        opposingPosition = BATTLE_OPPOSITE(GetBattlerPosition(gActiveBattler));

        // Check if the opposing Pokémon has Wonder Guard.
        if (gBattleMons[GetBattlerAtPosition(opposingPosition)].ability != ABILITY_WONDER_GUARD)
            return FALSE;

        // Identify moves on the current Pokémon that could bypass Wonder Guard.
        for (opposingBattler = GetBattlerAtPosition(opposingPosition), i = 0; i < MAX_MON_MOVES; i++)
        {
            move = gBattleMons[gActiveBattler].moves[i];
            if (move == MOVE_NONE)
                continue;

            moveFlags = AI_TypeCalc(move, gBattleMons[opposingBattler].species, gBattleMons[opposingBattler].ability);
            
            // If the current Pokémon has a super-effective move, don't switch.
            if (moveFlags & MOVE_RESULT_SUPER_EFFECTIVE)
                return FALSE;

            // Additional check: look for moves that bypass Wonder Guard through indirect damage.
            if (IsMoveIndirectDamage(move) || IsStatusMoveThatAffectsWonderGuard(move))
                return FALSE;
        }
    }

    // Get the party range based on the battle type.
    if (gBattleTypeFlags & (BATTLE_TYPE_TWO_OPPONENTS | BATTLE_TYPE_TOWER_LINK_MULTI))
    {
        if ((gActiveBattler & BIT_FLANK) == B_FLANK_LEFT)
            firstId = 0, lastId = PARTY_SIZE / 2;
        else
            firstId = PARTY_SIZE / 2, lastId = PARTY_SIZE;
    }
    else
    {
        firstId = 0, lastId = PARTY_SIZE;
    }

    // Determine the party based on the battler's side.
    if (GetBattlerSide(gActiveBattler) == B_SIDE_PLAYER)
        party = gPlayerParty;
    else
        party = gEnemyParty;

    // Look for a Pokémon in the party that has a super-effective or indirect move.
    for (i = firstId; i < lastId; i++)
    {
        // Skip fainted, eggs, or already active Pokémon.
        if (GetMonData(&party[i], MON_DATA_HP) == 0)
            continue;
        if (GetMonData(&party[i], MON_DATA_SPECIES_OR_EGG) == SPECIES_NONE)
            continue;
        if (GetMonData(&party[i], MON_DATA_SPECIES_OR_EGG) == SPECIES_EGG)
            continue;
        if (i == gBattlerPartyIndexes[gActiveBattler])
            continue;

        // Check moves on this party Pokémon.
        for (opposingBattler = GetBattlerAtPosition(opposingPosition), j = 0; j < MAX_MON_MOVES; j++)
        {
            move = GetMonData(&party[i], MON_DATA_MOVE1 + j);
            if (move == MOVE_NONE)
                continue;

            moveFlags = AI_TypeCalc(move, gBattleMons[opposingBattler].species, gBattleMons[opposingBattler].ability);
            
            // Prioritize a Pokémon with a super-effective move or one that can bypass Wonder Guard.
            if ((moveFlags & MOVE_RESULT_SUPER_EFFECTIVE) || IsMoveIndirectDamage(move) || IsStatusMoveThatAffectsWonderGuard(move))
            {
                // Randomize switch decision slightly to add variation.
                if (Random() % 3 < 2)
                {
                    // Set the AI's choice to switch to this Pokémon.
                    *(gBattleStruct->AI_monToSwitchIntoId + gActiveBattler) = i;
                    BtlController_EmitTwoReturnValues(BUFFER_B, B_ACTION_SWITCH, 0);
                    return TRUE;
                }
            }
        }
    }

    return FALSE; // No viable Pokémon with a way to bypass Wonder Guard.
}

static bool8 IsStatusMoveThatAffectsWonderGuard(u16 move)
{
    // Returns TRUE if the move is a status move that Wonder Guard cannot block.
    return move == MOVE_GROWL || move == MOVE_SCREECH || move == MOVE_TAIL_WHIP
        || move == MOVE_METAL_SOUND || move == MOVE_ROAR || move == MOVE_WHIRLWIND;
}

static bool8 FindMonThatAbsorbsOpponentsMove(void)
{
    u8 battlerIn1, battlerIn2;
    u8 absorbingTypeAbility = ABILITY_NONE;
    s32 firstId, lastId;
    struct Pokemon *party;
    s32 i;

    // If the active Pokémon already has a super-effective move, limit switching to add variety.
    if (HasSuperEffectiveMoveAgainstOpponents(TRUE) && Random() % 3 != 0)
        return FALSE;

    // Check if the last move used by the opponent is valid for absorption.
    if (gLastLandedMoves[gActiveBattler] == MOVE_NONE || gLastLandedMoves[gActiveBattler] == MOVE_UNAVAILABLE)
        return FALSE;
    if (gBattleMoves[gLastLandedMoves[gActiveBattler]].power == 0) // Non-damaging moves not worth absorbing.
        return FALSE;

    // Determine the battlers in double or single battle scenarios.
    if (gBattleTypeFlags & BATTLE_TYPE_DOUBLE)
    {
        battlerIn1 = gActiveBattler;
        if (gAbsentBattlerFlags & gBitTable[GetBattlerAtPosition(BATTLE_PARTNER(GetBattlerPosition(gActiveBattler)))] == 0)
            battlerIn2 = GetBattlerAtPosition(BATTLE_PARTNER(GetBattlerPosition(gActiveBattler)));
        else
            battlerIn2 = gActiveBattler;
    }
    else
    {
        battlerIn1 = gActiveBattler;
        battlerIn2 = gActiveBattler;
    }

    // Identify the absorb ability based on the opponent's last move type.
    switch (gBattleMoves[gLastLandedMoves[gActiveBattler]].type)
    {
        case TYPE_FIRE:
            absorbingTypeAbility = ABILITY_FLASH_FIRE;
            break;
        case TYPE_WATER:
            absorbingTypeAbility = ABILITY_WATER_ABSORB;
            break;
        case TYPE_ELECTRIC:
            absorbingTypeAbility = ABILITY_VOLT_ABSORB;
            break;
        default:
            return FALSE; // If the move type isn’t absorbable, exit early.
    }

    // If the current Pokémon has the needed absorb ability, there’s no need to switch.
    if (gBattleMons[gActiveBattler].ability == absorbingTypeAbility)
        return FALSE;

    // Define the party range based on battle type (multi-battles, etc.).
    if (gBattleTypeFlags & (BATTLE_TYPE_TWO_OPPONENTS | BATTLE_TYPE_TOWER_LINK_MULTI))
    {
        if ((gActiveBattler & BIT_FLANK) == B_FLANK_LEFT)
            firstId = 0, lastId = PARTY_SIZE / 2;
        else
            firstId = PARTY_SIZE / 2, lastId = PARTY_SIZE;
    }
    else
    {
        firstId = 0, lastId = PARTY_SIZE;
    }

    // Set the party pointer based on the active battler’s side.
    if (GetBattlerSide(gActiveBattler) == B_SIDE_PLAYER)
        party = gPlayerParty;
    else
        party = gEnemyParty;

    // Search for a suitable switch-in with the absorb ability.
    for (i = firstId; i < lastId; i++)
    {
        u16 species;
        u8 monAbility;

        if (GetMonData(&party[i], MON_DATA_HP) == 0)
            continue; // Skip fainted Pokémon.
        if (GetMonData(&party[i], MON_DATA_SPECIES_OR_EGG) == SPECIES_NONE || GetMonData(&party[i], MON_DATA_SPECIES_OR_EGG) == SPECIES_EGG)
            continue; // Skip non-combatants and eggs.
        if (i == gBattlerPartyIndexes[battlerIn1] || i == gBattlerPartyIndexes[battlerIn2])
            continue; // Skip currently active battlers.
        if (i == *(gBattleStruct->monToSwitchIntoId + battlerIn1) || i == *(gBattleStruct->monToSwitchIntoId + battlerIn2))
            continue; // Skip Pokémon already planned to switch in.

        // Get the species and ability of the candidate Pokémon.
        species = GetMonData(&party[i], MON_DATA_SPECIES);
        if (GetMonData(&party[i], MON_DATA_ABILITY_NUM) != 0)
            monAbility = gSpeciesInfo[species].abilities[1];
        else
            monAbility = gSpeciesInfo[species].abilities[0];

        // Check if the Pokémon has the desired absorb ability and is healthy enough to switch in.
        if (absorbingTypeAbility == monAbility && GetMonData(&party[i], MON_DATA_HP) > (GetMonData(&party[i], MON_DATA_MAX_HP) / 2))
        {
            // Switch to the Pokémon with the absorb ability, adding some variability.
            if (Random() % 2 == 0)
            {
                *(gBattleStruct->AI_monToSwitchIntoId + gActiveBattler) = i;
                BtlController_EmitTwoReturnValues(BUFFER_B, B_ACTION_SWITCH, 0);
                return TRUE;
            }
        }
    }

    return FALSE; // No suitable Pokémon found with an absorb ability.
}

static bool8 HasSuperEffectiveMoveAgainstOpponents(bool8 noRng)
{
    u8 opposingPosition;
    u8 opposingBattler;
    s32 i;
    u8 moveFlags;
    u16 move;

    // Get the position and battler of the primary opponent.
    opposingPosition = BATTLE_OPPOSITE(GetBattlerPosition(gActiveBattler));
    opposingBattler = GetBattlerAtPosition(opposingPosition);

    // Check for super-effective moves against the primary opponent.
    if (!(gAbsentBattlerFlags & gBitTable[opposingBattler]))
    {
        for (i = 0; i < MAX_MON_MOVES; i++)
        {
            move = gBattleMons[gActiveBattler].moves[i];
            if (move == MOVE_NONE)
                continue;

            // Calculate type effectiveness and ability interaction.
            moveFlags = AI_TypeCalc(move, gBattleMons[opposingBattler].species, gBattleMons[opposingBattler].ability);
            if ((moveFlags & MOVE_RESULT_SUPER_EFFECTIVE) && 
                IsMoveEffectiveAgainstAbility(move, gBattleMons[opposingBattler].ability, gBattleMons[opposingBattler].item))
            {
                if (noRng)
                    return TRUE;
                if (Random() % 10 != 0)
                    return TRUE;
            }
        }
    }

    // If it's a single battle, return FALSE if no super-effective moves found for the primary opponent.
    if (!(gBattleTypeFlags & BATTLE_TYPE_DOUBLE))
        return FALSE;

    // Handle double battle case: check for super-effective moves against the secondary opponent.
    opposingBattler = GetBattlerAtPosition(BATTLE_PARTNER(opposingPosition));

    if (!(gAbsentBattlerFlags & gBitTable[opposingBattler]))
    {
        for (i = 0; i < MAX_MON_MOVES; i++)
        {
            move = gBattleMons[gActiveBattler].moves[i];
            if (move == MOVE_NONE)
                continue;

            // Calculate type effectiveness and ability interaction.
            moveFlags = AI_TypeCalc(move, gBattleMons[opposingBattler].species, gBattleMons[opposingBattler].ability);
            if ((moveFlags & MOVE_RESULT_SUPER_EFFECTIVE) && 
                IsMoveEffectiveAgainstAbility(move, gBattleMons[opposingBattler].ability, gBattleMons[opposingBattler].item))
            {
                if (noRng)
                    return TRUE;
                if (Random() % 10 != 0)
                    return TRUE;
            }
        }
    }

    return FALSE; // No super-effective moves found against either opponent.
}

static bool8 IsMoveEffectiveAgainstAbility(u16 move, u8 ability, u16 itemId)
{
    u8 moveType = gBattleMoves[move].type;

    // Check for ability-based immunities
    switch (ability)
    {
        case ABILITY_LEVITATE:
            if (moveType == TYPE_GROUND)
                return FALSE; // Immune to Ground-type moves
            break;

        case ABILITY_FLASH_FIRE:
            if (moveType == TYPE_FIRE)
                return FALSE; // Absorbs Fire-type moves
            break;

        case ABILITY_WATER_ABSORB:
            if (moveType == TYPE_WATER)
                return FALSE; // Absorbs Water-type moves
            break;

        case ABILITY_VOLT_ABSORB:
        case ABILITY_LIGHTNING_ROD:
            if (moveType == TYPE_ELECTRIC)
                return FALSE; // Absorbs Electric-type moves
            break;

        case ABILITY_THICK_FAT:
            if (moveType == TYPE_FIRE || moveType == TYPE_ICE)
                return FALSE; // Reduces Fire and Ice damage
            break;

        case ABILITY_STURDY:
            if (gBattleMons[gActiveBattler].hp == gBattleMons[gActiveBattler].maxHP)
                return FALSE; // Prevents OHKO if at full HP
            break;

        default:
            break;
    }

    // Check for item-based effects
    switch (itemId)
    {
        case ITEM_FOCUS_BAND:
            if (Random() % 10 == 0) // 10% chance to survive a KO hit
                return FALSE;
            break;

        default:
            break;
    }

    return TRUE; // Move is effective unless blocked by ability or item
}

static bool8 AreStatsRaised(void)
{
    u8 buffedStatsValue = 0;
    s32 i;

    // Determine the priority of stats for the current battler's role.
    bool8 isAttacker = (gBattleMons[gActiveBattler].attack > gBattleMons[gActiveBattler].defense);
    bool8 isSpecialAttacker = (gBattleMons[gActiveBattler].spAttack > gBattleMons[gActiveBattler].spDefense);

    for (i = 0; i < NUM_BATTLE_STATS; i++)
    {
        // Check if stat stage is above the default and add the difference.
        if (gBattleMons[gActiveBattler].statStages[i] > DEFAULT_STAT_STAGE)
        {
            u8 statIncrease = gBattleMons[gActiveBattler].statStages[i] - DEFAULT_STAT_STAGE;

            // Give higher weight to relevant stats based on the battler’s role.
            switch (i)
            {
                case STAT_ATK:
                    buffedStatsValue += (isAttacker ? statIncrease * 2 : statIncrease);
                    break;
                case STAT_SPATK:
                    buffedStatsValue += (isSpecialAttacker ? statIncrease * 2 : statIncrease);
                    break;
                case STAT_SPEED:
                    buffedStatsValue += statIncrease; // Speed is generally valuable in most cases.
                    break;
                case STAT_DEF:
                case STAT_SPDEF:
                    buffedStatsValue += (isAttacker || isSpecialAttacker ? statIncrease : statIncrease * 2);
                    break;
                default:
                    buffedStatsValue += statIncrease;
                    break;
            }
        }
    }

    // Adjust threshold based on role and cumulative stat boost.
    u8 threshold = isAttacker || isSpecialAttacker ? 4 : 3;

    return (buffedStatsValue >= threshold);
}

static bool8 FindMonWithFlagsAndSuperEffective(u8 flags, u8 moduloPercent)
{
    u8 battlerIn1, battlerIn2;
    s32 firstId, lastId;
    struct Pokemon *party;
    s32 i, j;
    u16 move;
    u8 moveFlags;

    // Check if the last move used on the active battler is valid.
    if (gLastLandedMoves[gActiveBattler] == MOVE_NONE || gLastLandedMoves[gActiveBattler] == MOVE_UNAVAILABLE)
        return FALSE;
    if (gLastHitBy[gActiveBattler] == 0xFF)
        return FALSE;
    if (gBattleMoves[gLastLandedMoves[gActiveBattler]].power == 0) // Ignore non-damaging moves.
        return FALSE;

    // Identify active battlers in double battles.
    if (gBattleTypeFlags & BATTLE_TYPE_DOUBLE)
    {
        battlerIn1 = gActiveBattler;
        battlerIn2 = (gAbsentBattlerFlags & gBitTable[GetBattlerAtPosition(BATTLE_PARTNER(GetBattlerPosition(gActiveBattler)))] == 0)
                      ? GetBattlerAtPosition(BATTLE_PARTNER(GetBattlerPosition(gActiveBattler)))
                      : gActiveBattler;
    }
    else
    {
        battlerIn1 = gActiveBattler;
        battlerIn2 = gActiveBattler;
    }

    // Set the range of the party to search within, depending on battle type.
    if (gBattleTypeFlags & (BATTLE_TYPE_TWO_OPPONENTS | BATTLE_TYPE_TOWER_LINK_MULTI))
    {
        if ((gActiveBattler & BIT_FLANK) == 0)
            firstId = 0, lastId = PARTY_SIZE / 2;
        else
            firstId = PARTY_SIZE / 2, lastId = PARTY_SIZE;
    }
    else
    {
        firstId = 0, lastId = PARTY_SIZE;
    }

    // Determine the party of the active battler.
    party = (GetBattlerSide(gActiveBattler) == B_SIDE_PLAYER) ? gPlayerParty : gEnemyParty;

    // Search for a Pokémon with the specified conditions.
    for (i = firstId; i < lastId; i++)
    {
        u16 species;
        u8 monAbility;

        // Skip fainted Pokémon, eggs, or active battlers already on the field.
        if (GetMonData(&party[i], MON_DATA_HP) == 0 || GetMonData(&party[i], MON_DATA_SPECIES_OR_EGG) == SPECIES_NONE || GetMonData(&party[i], MON_DATA_SPECIES_OR_EGG) == SPECIES_EGG)
            continue;
        if (i == gBattlerPartyIndexes[battlerIn1] || i == gBattlerPartyIndexes[battlerIn2])
            continue;
        if (i == *(gBattleStruct->AI_monToSwitchIntoId + battlerIn1) || i == *(gBattleStruct->AI_monToSwitchIntoId + battlerIn2))
            continue;

        // Retrieve species and ability data for the current Pokémon.
        species = GetMonData(&party[i], MON_DATA_SPECIES);
        monAbility = (GetMonData(&party[i], MON_DATA_ABILITY_NUM) != 0) ? gSpeciesInfo[species].abilities[1] : gSpeciesInfo[species].abilities[0];

        // Check if the last landed move on the active battler matches the specified flags.
        moveFlags = AI_TypeCalc(gLastLandedMoves[gActiveBattler], species, monAbility);
        if (moveFlags & flags)
        {
            battlerIn1 = gLastHitBy[gActiveBattler];

            // Check each move of this party Pokémon to see if it’s super-effective against the opponent.
            for (j = 0; j < MAX_MON_MOVES; j++)
            {
                move = GetMonData(&party[i], MON_DATA_MOVE1 + j);
                if (move == MOVE_NONE)
                    continue;

                // Calculate the effectiveness of the move against the opponent.
                moveFlags = AI_TypeCalc(move, gBattleMons[battlerIn1].species, gBattleMons[battlerIn1].ability);

                // Check if the move is super-effective and apply randomization with modulo.
                if ((moveFlags & MOVE_RESULT_SUPER_EFFECTIVE) && Random() % moduloPercent == 0)
                {
                    // Set the AI to switch to this Pokémon.
                    *(gBattleStruct->AI_monToSwitchIntoId + gActiveBattler) = i;
                    BtlController_EmitTwoReturnValues(BUFFER_B, B_ACTION_SWITCH, 0);
                    return TRUE;
                }
            }
        }
    }

    return FALSE; // No suitable Pokémon found with the required conditions.
}

static bool8 ShouldSwitch(void)
{
    u8 battlerIn1, battlerIn2;
    u8 *activeBattlerPtr = &gActiveBattler; // Pointer to active battler.
    s32 firstId, lastId;
    struct Pokemon *party;
    s32 i;
    s32 availableToSwitch;
    u16 predictedMove;

    // Prevent switching if the Pokémon is trapped or rooted.
    if (gBattleMons[*activeBattlerPtr].status2 & (STATUS2_WRAPPED | STATUS2_ESCAPE_PREVENTION))
        return FALSE;
    if (gStatuses3[gActiveBattler] & STATUS3_ROOTED)
        return FALSE;

    // Check for opponent abilities that trap, with type-specific checks for Arena Trap.
    if (ABILITY_ON_OPPOSING_FIELD(gActiveBattler, ABILITY_SHADOW_TAG))
        return FALSE;
    if (ABILITY_ON_OPPOSING_FIELD(gActiveBattler, ABILITY_ARENA_TRAP))
    {
        if (!IS_BATTLER_OF_TYPE(gActiveBattler, TYPE_FLYING) && !ABILITY_PRESENT(gActiveBattler, ABILITY_LEVITATE))
            return FALSE;
    }
    if (ABILITY_ON_FIELD2(ABILITY_MAGNET_PULL))
    {
        if (IS_BATTLER_OF_TYPE(gActiveBattler, TYPE_STEEL))
            return FALSE;
    }

    // Avoid switching in Arena battles.
    if (gBattleTypeFlags & BATTLE_TYPE_ARENA)
        return FALSE;

    // Predict the opponent's next move based on previous moves.
    predictedMove = PredictOpponentMove(BATTLE_OPPOSITE(gActiveBattler));

    // Identify current battlers in double battles.
    availableToSwitch = 0;
    if (gBattleTypeFlags & BATTLE_TYPE_DOUBLE)
    {
        battlerIn1 = *activeBattlerPtr;
        if (!(gAbsentBattlerFlags & gBitTable[GetBattlerAtPosition(BATTLE_PARTNER(GetBattlerPosition(*activeBattlerPtr)))]))
            battlerIn2 = GetBattlerAtPosition(BATTLE_PARTNER(GetBattlerPosition(*activeBattlerPtr)));
        else
            battlerIn2 = *activeBattlerPtr;
    }
    else
    {
        battlerIn1 = *activeBattlerPtr;
        battlerIn2 = *activeBattlerPtr;
    }

    // Determine party slots for multi-battles.
    if (gBattleTypeFlags & (BATTLE_TYPE_TWO_OPPONENTS | BATTLE_TYPE_TOWER_LINK_MULTI))
    {
        if ((gActiveBattler & BIT_FLANK) == B_FLANK_LEFT)
            firstId = 0, lastId = PARTY_SIZE / 2;
        else
            firstId = PARTY_SIZE / 2, lastId = PARTY_SIZE;
    }
    else
    {
        firstId = 0, lastId = PARTY_SIZE;
    }

    // Set party based on battler's side.
    party = (GetBattlerSide(gActiveBattler) == B_SIDE_PLAYER) ? gPlayerParty : gEnemyParty;

    // Check available Pokémon for potential switches.
    for (i = firstId; i < lastId; i++)
    {
        if (GetMonData(&party[i], MON_DATA_HP) == 0)
            continue;
        if (GetMonData(&party[i], MON_DATA_SPECIES_OR_EGG) == SPECIES_NONE || GetMonData(&party[i], MON_DATA_SPECIES_OR_EGG) == SPECIES_EGG)
            continue;
        if (i == gBattlerPartyIndexes[battlerIn1] || i == gBattlerPartyIndexes[battlerIn2])
            continue;
        if (i == *(gBattleStruct->AI_monToSwitchIntoId + battlerIn1) || i == *(gBattleStruct->AI_monToSwitchIntoId + battlerIn2))
            continue;

        // Check if the predicted move would be super-effective against the potential switch-in.
        if (IsPredictedMoveSuperEffectiveAgainst(gActiveBattler, i, predictedMove))
            continue; // Skip this Pokémon as it could be KO’d by the predicted move

        availableToSwitch++;
    }

    // No switch possible if no viable Pokémon remain.
    if (availableToSwitch == 0)
        return FALSE;

    // New Strategy Check Implementations

    // 1. Switch if HP is low and there’s a viable alternative with more HP.
    if (gBattleMons[gActiveBattler].hp < gBattleMons[gActiveBattler].maxHP / 4 && availableToSwitch > 0)
        return TRUE;

    // 2. Check for entry hazards and avoid switching if it risks fainting due to hazards.
    if (IsHazardousSwitch(gActiveBattler))
        return FALSE;

    // 3. Switch if under Perish Song status and not trapped.
    if (ShouldSwitchIfPerishSong())
        return TRUE;

    // 4. Switch if Wonder Guard would protect against the opponent's moves.
    if (ShouldSwitchIfWonderGuard())
        return TRUE;

    // 5. Switch if a Pokémon with an Absorb Ability (Water Absorb, Volt Absorb, Flash Fire) is available and counters the opponent’s type.
    if (FindMonThatAbsorbsOpponentsMove())
        return TRUE;

    // 6. Switch if the Pokémon has Natural Cure and a status condition that would be healed upon switching.
    if (ShouldSwitchIfNaturalCure())
        return TRUE;

    // 7. Check if there’s a teammate with a super-effective move, especially beneficial if switching out to counter the opponent.
    if (HasSuperEffectiveMoveAgainstOpponents(FALSE))
        return FALSE;

    // 8. Avoid switching if stats are already raised, maintaining boosts on the field.
    if (AreStatsRaised())
        return FALSE;

    // 9. Switch if there’s a teammate with a more favorable type matchup (checks for non-effective and not-very-effective moves).
    if (FindMonWithFlagsAndSuperEffective(MOVE_RESULT_DOESNT_AFFECT_FOE, 2)
        || FindMonWithFlagsAndSuperEffective(MOVE_RESULT_NOT_VERY_EFFECTIVE, 3))
        return TRUE;

    // 10: Switch to Rapid Spin user if entry hazards are active.
    if (ShouldSwitchToRapidSpinUserIfHazards())
        return TRUE;

    return FALSE;
}

// Predict the opponent’s next move based on previous moves and move characteristics
static u16 PredictOpponentMove(u8 opponent)
{
    u16 predictedMove = MOVE_NONE;
    s32 bestScore = -1; // Start with a low score to ensure any move will be better
    s32 moveScore;
    u8 i;

    for (i = 0; i < MAX_MON_MOVES; i++)
    {
        u16 move = gBattleMons[opponent].moves[i];
        if (move == MOVE_NONE)
            continue;

        // Initialize move score based on base power
        moveScore = gBattleMoves[move].power;

        // Factor in type effectiveness
        u8 moveType = gBattleMoves[move].type;
        u8 effectiveness = AI_TypeCalc(move, gBattleMons[gActiveBattler].species, gBattleMons[gActiveBattler].ability);

        if (effectiveness & MOVE_RESULT_SUPER_EFFECTIVE)
            moveScore += 20; // Higher score for super-effective moves
        else if (effectiveness & MOVE_RESULT_NOT_VERY_EFFECTIVE)
            moveScore -= 10; // Lower score for not-very-effective moves

        // Consider accuracy; less accurate moves are less reliable
        u8 accuracy = gBattleMoves[move].accuracy;
        if (accuracy < 100)
            moveScore -= (100 - accuracy) / 5; // Deduct more points for lower accuracy

        // Add score for priority moves
        if (gBattleMoves[move].priority > 0)
            moveScore += 10; // Prioritize moves with a priority boost

        // Check if the move has remaining PP; if not, skip this move
        if (gBattleMons[opponent].pp[i] == 0)
            continue;

        // Compare score with the best score found so far
        if (moveScore > bestScore)
        {
            bestScore = moveScore;
            predictedMove = move;
        }
    }

    return predictedMove;
}

// Check if the predicted move would be super-effective against the potential switch-in
static bool8 IsPredictedMoveSuperEffectiveAgainst(u8 battler, u8 potentialSwitchIn, u16 predictedMove)
{
    u8 moveType;
    u8 potentialSwitchInType1, potentialSwitchInType2;

    if (predictedMove == MOVE_NONE)
        return FALSE;

    // Get move type and target Pokémon's types
    moveType = gBattleMoves[predictedMove].type;
    potentialSwitchInType1 = gSpeciesInfo[GetMonData(&gPlayerParty[potentialSwitchIn], MON_DATA_SPECIES)].types[0];
    potentialSwitchInType2 = gSpeciesInfo[GetMonData(&gPlayerParty[potentialSwitchIn], MON_DATA_SPECIES)].types[1];

    // Calculate type effectiveness
    if (TypeEffectiveness(moveType, potentialSwitchInType1) == TYPE_MUL_SUPER_EFFECTIVE
        || TypeEffectiveness(moveType, potentialSwitchInType2) == TYPE_MUL_SUPER_EFFECTIVE)
        return TRUE;

    return FALSE;
}

// Helper function to calculate type effectiveness
static u8 TypeEffectiveness(u8 atkType, u8 defType)
{
    u8 effectiveness = TYPE_MUL_NORMAL;
    s32 i = 0;

    while (TYPE_EFFECT_ATK_TYPE(i) != TYPE_ENDTABLE)
    {
        if (TYPE_EFFECT_ATK_TYPE(i) == atkType && TYPE_EFFECT_DEF_TYPE(i) == defType)
        {
            effectiveness = TYPE_EFFECT_MULTIPLIER(i);
            break;
        }
        i += 3;
    }

    return effectiveness;
}

static bool8 IsHazardousSwitch(u8 battler)
{
    // Check if Spikes are set and estimate the damage based on the number of layers.
    if (gSideStatuses[GetBattlerSide(battler)] & SIDE_STATUS_SPIKES)
    {
        // Calculate Spikes damage based on the number of layers.
        u8 spikesLayers = gSideTimers[GetBattlerSide(battler)].spikesAmount;
        u16 spikesDamage = (spikesLayers * gBattleMons[battler].maxHP) / 8; // Each layer adds 1/8 of max HP

        // Avoid switching if Spikes damage could cause fainting.
        if (gBattleMons[battler].hp <= spikesDamage)
            return TRUE;
    }

    return FALSE; // Safe to switch
}

void AI_TrySwitchOrUseItem(void)
{
    struct Pokemon *party;
    u8 battlerIn1, battlerIn2;
    s32 firstId, lastId;
    s32 monToSwitchId;
    u8 battlerIdentity = GetBattlerPosition(gActiveBattler);

    // Determine the party side.
    party = (GetBattlerSide(gActiveBattler) == B_SIDE_PLAYER) ? gPlayerParty : gEnemyParty;

    // Trainer AI Logic
    if (gBattleTypeFlags & BATTLE_TYPE_TRAINER)
    {
        // Priority 1: Check if the AI should switch Pokémon.
        if (ShouldSwitch())
        {
            // If no predefined switch target, use GetMostSuitableMonToSwitchInto.
            if (*(gBattleStruct->AI_monToSwitchIntoId + gActiveBattler) == PARTY_SIZE)
            {
                monToSwitchId = GetMostSuitableMonToSwitchInto();
                
                if (monToSwitchId == PARTY_SIZE)  // Fallback if no suitable mon is identified.
                {
                    // Set up battlers for double or single battle situations.
                    if (gBattleTypeFlags & BATTLE_TYPE_DOUBLE)
                    {
                        battlerIn1 = GetBattlerAtPosition(battlerIdentity);
                        battlerIn2 = GetBattlerAtPosition(BATTLE_PARTNER(battlerIdentity));
                    }
                    else
                    {
                        battlerIn1 = battlerIn2 = GetBattlerAtPosition(battlerIdentity);
                    }

                    // Determine firstId and lastId for multi-battles or standard battles.
                    if (gBattleTypeFlags & (BATTLE_TYPE_TWO_OPPONENTS | BATTLE_TYPE_TOWER_LINK_MULTI))
                    {
                        if ((gActiveBattler & BIT_FLANK) == B_FLANK_LEFT)
                            firstId = 0, lastId = PARTY_SIZE / 2;
                        else
                            firstId = PARTY_SIZE / 2, lastId = PARTY_SIZE;
                    }
                    else
                    {
                        firstId = 0, lastId = PARTY_SIZE;
                    }

                    // Find the first available Pokémon to switch into within the allowed range.
                    for (monToSwitchId = firstId; monToSwitchId < lastId; monToSwitchId++)
                    {
                        if (GetMonData(&party[monToSwitchId], MON_DATA_HP) == 0)
                            continue;
                        if (monToSwitchId == gBattlerPartyIndexes[battlerIn1] || monToSwitchId == gBattlerPartyIndexes[battlerIn2])
                            continue;
                        if (monToSwitchId == *(gBattleStruct->monToSwitchIntoId + battlerIn1) || monToSwitchId == *(gBattleStruct->monToSwitchIntoId + battlerIn2))
                            continue;
                        break;
                    }
                }

                // Set AI's chosen mon to switch into.
                *(gBattleStruct->AI_monToSwitchIntoId + gActiveBattler) = monToSwitchId;
            }

            // Confirm the switch by setting it in battle struct.
            *(gBattleStruct->monToSwitchIntoId + gActiveBattler) = *(gBattleStruct->AI_monToSwitchIntoId + gActiveBattler);
            BtlController_EmitTwoReturnValues(BUFFER_B, B_ACTION_SWITCH, 0);
            return;
        }
        // Priority 2: If switching isn’t ideal, attempt to use an item if possible.
        else if (ShouldUseItem())
        {
            return;  // Item use will be processed.
        }
    }

    // Default Action: Use a move targeting the opponent if no switch or item use is triggered.
    BtlController_EmitTwoReturnValues(BUFFER_B, B_ACTION_USE_MOVE, BATTLE_OPPOSITE(gActiveBattler) << 8);
}

static void ModulateByTypeEffectiveness(u8 atkType, u8 defType1, u8 defType2, u8 *var)
{
    s32 i = 0;
    bool8 type1Checked = FALSE;
    bool8 type2Checked = FALSE;

    // Iterate through the type effectiveness table.
    while (TYPE_EFFECT_ATK_TYPE(i) != TYPE_ENDTABLE)
    {
        // Skip "Foresight" type entry as it's not relevant to type effectiveness.
        if (TYPE_EFFECT_ATK_TYPE(i) == TYPE_FORESIGHT)
        {
            i += 3;
            continue;
        }

        // If the attacking type matches the current entry's type
        if (TYPE_EFFECT_ATK_TYPE(i) == atkType)
        {
            // Apply effectiveness multiplier to defType1, if it hasn't been applied already.
            if (!type1Checked && TYPE_EFFECT_DEF_TYPE(i) == defType1)
            {
                *var = (*var * TYPE_EFFECT_MULTIPLIER(i)) / TYPE_MUL_NORMAL;
                type1Checked = TRUE; // Mark as checked to avoid duplicate calculations.
            }

            // Apply effectiveness multiplier to defType2, if it hasn't been applied and is different from defType1.
            if (!type2Checked && defType1 != defType2 && TYPE_EFFECT_DEF_TYPE(i) == defType2)
            {
                *var = (*var * TYPE_EFFECT_MULTIPLIER(i)) / TYPE_MUL_NORMAL;
                type2Checked = TRUE; // Mark as checked to avoid duplicate calculations.
            }

            // If both types have been checked, we can exit early to save time.
            if (type1Checked && type2Checked)
                break;
        }
        
        // Move to the next entry in the type effectiveness table.
        i += 3;
    }
}

u8 GetMostSuitableMonToSwitchInto(void)
{
    u8 bestMonId = PARTY_SIZE;
    s32 bestScore = -10000; // Start with a very low score to ensure any valid Pokémon will have a higher score.
    struct Pokemon *party = (GetBattlerSide(gActiveBattler) == B_SIDE_PLAYER) ? gPlayerParty : gEnemyParty;
    u8 battlerIn1, battlerIn2, opposingBattler;
    s32 firstId, lastId;

    // Setup for double battles and multi-battles
    SetBattleParticipants(&battlerIn1, &battlerIn2, &opposingBattler, &firstId, &lastId);

    // Iterate through each Pokémon in the available range.
    for (s32 i = firstId; i < lastId; i++)
    {
        s32 score = 0;
        u16 species = GetMonData(&party[i], MON_DATA_SPECIES);
        u16 hp = GetMonData(&party[i], MON_DATA_HP);

        // Skip invalid Pokémon
        if (species == SPECIES_NONE || hp == 0 || !IsValidSwitchTarget(i, battlerIn1, battlerIn2))
            continue;

        // Add score based on type advantage and super-effective moves
        score += CalculateTypeAdvantageScore(i, opposingBattler);

        // Add score based on Pokémon’s health level
        if (hp > (gBattleMons[gActiveBattler].maxHP / 2))
            score += 10;
        else if (hp < (gBattleMons[gActiveBattler].maxHP / 4))
            score -= 10;

        // Add score for beneficial abilities
        score += CalculateAbilityAdvantageScore(i, opposingBattler);

        // Subtract points if the Pokémon is vulnerable to entry hazards
        if (WillTakeSignificantHazardDamage(i))
            score -= 15;

        // Add score if the Pokémon has stat boosts
        if (HasStatBoosts(i))
            score += 20;

        // Update best score and best Pokémon ID if the current score is higher
        if (score > bestScore)
        {
            bestScore = score;
            bestMonId = i;
        }
    }

    return bestMonId;
}

// Helper Function Definitions

static void SetBattleParticipants(u8 *battlerIn1, u8 *battlerIn2, u8 *opposingBattler, s32 *firstId, s32 *lastId)
{
    if (gBattleTypeFlags & BATTLE_TYPE_DOUBLE)
    {
        *battlerIn1 = gActiveBattler;
        *battlerIn2 = (gAbsentBattlerFlags & gBitTable[GetBattlerAtPosition(BATTLE_PARTNER(GetBattlerPosition(gActiveBattler)))] == 0)
                        ? GetBattlerAtPosition(BATTLE_PARTNER(GetBattlerPosition(gActiveBattler)))
                        : gActiveBattler;
        
        // Randomly select an opponent in double battles.
        *opposingBattler = Random() & BIT_FLANK;
        if (gAbsentBattlerFlags & gBitTable[*opposingBattler])
            *opposingBattler ^= BIT_FLANK;
    }
    else
    {
        *opposingBattler = GetBattlerAtPosition(BATTLE_OPPOSITE(GetBattlerPosition(gActiveBattler)));
        *battlerIn1 = *battlerIn2 = gActiveBattler;
    }

    // Set party range for multi-battles or standard battles.
    if (gBattleTypeFlags & (BATTLE_TYPE_TWO_OPPONENTS | BATTLE_TYPE_TOWER_LINK_MULTI))
    {
        if ((gActiveBattler & BIT_FLANK) == B_FLANK_LEFT)
        {
            *firstId = 0;
            *lastId = PARTY_SIZE / 2;
        }
        else
        {
            *firstId = PARTY_SIZE / 2;
            *lastId = PARTY_SIZE;
        }
    }
    else
    {
        *firstId = 0;
        *lastId = PARTY_SIZE;
    }
}

static s32 CalculateTypeAdvantageScore(u8 monId, u8 opposingBattler)
{
    s32 score = 0;
    u16 move;
    u16 species = GetMonData(&gPlayerParty[monId], MON_DATA_SPECIES);
    u8 monType1 = gSpeciesInfo[species].types[0];
    u8 monType2 = gSpeciesInfo[species].types[1];

    for (u8 i = 0; i < MAX_MON_MOVES; i++)
    {
        move = GetMonData(&gPlayerParty[monId], MON_DATA_MOVE1 + i);
        if (move == MOVE_NONE)
            continue;

        u8 moveType = gBattleMoves[move].type;
        u8 movePower = gBattleMoves[move].power;
        u8 moveFlags = AI_TypeCalc(move, gBattleMons[opposingBattler].species, gBattleMons[opposingBattler].ability);

        // Score for move effectiveness
        if (moveFlags & MOVE_RESULT_SUPER_EFFECTIVE)
            score += 15; // Super-effective move bonus
        else if (moveFlags & MOVE_RESULT_NOT_VERY_EFFECTIVE)
            score -= 10; // Not-very-effective move penalty

        // STAB Bonus
        if (moveType == monType1 || moveType == monType2)
        {
            score += 5;
            score += movePower / 20;
        }

        if ((moveFlags & MOVE_RESULT_SUPER_EFFECTIVE) && (moveType == monType1 || moveType == monType2))
        {
            score += 10;
            score += movePower / 10;
        }

        if (IsMoveIndirectDamage(move))
        {
            score += 5;
        }
        if (gBattleMoves[move].priority > 0)
        {
            score += 8;
        }
    }

    return score;
}

// Helper function to identify indirect moves
static bool8 IsMoveIndirectDamage(u16 move)
{
    return (move == MOVE_TOXIC || move == MOVE_WILL_O_WISP || move == MOVE_LEECH_SEED || 
            move == MOVE_SANDSTORM || move == MOVE_HAIL || move == MOVE_POISON_POWDER);
}

static s32 CalculateAbilityAdvantageScore(u8 monId, u8 opposingBattler)
{
    s32 score = 0;
    u16 species = GetMonData(&gPlayerParty[monId], MON_DATA_SPECIES);
    u8 abilitySlot = GetMonData(&gPlayerParty[monId], MON_DATA_ABILITY_NUM);
    u8 monAbility = abilitySlot == 0 ? gSpeciesInfo[species].abilities[0] : gSpeciesInfo[species].abilities[1];

    switch (monAbility)
    {
        case ABILITY_LEVITATE:
            if (gBattleMons[opposingBattler].types[0] == TYPE_GROUND || gBattleMons[opposingBattler].types[1] == TYPE_GROUND)
                score += 10;
            break;
        case ABILITY_FLASH_FIRE:
            if (gBattleMons[opposingBattler].types[0] == TYPE_FIRE || gBattleMons[opposingBattler].types[1] == TYPE_FIRE)
                score += 10;
            break;
        case ABILITY_WATER_ABSORB:
            if (gBattleMons[opposingBattler].types[0] == TYPE_WATER || gBattleMons[opposingBattler].types[1] == TYPE_WATER)
                score += 10;
            break;
    }
    return score;
}

static bool8 WillTakeSignificantHazardDamage(u8 monId)
{
    u8 type1 = gSpeciesInfo[gPlayerParty[monId].species].types[0];
    u8 type2 = gSpeciesInfo[gPlayerParty[monId].species].types[1];
    u16 maxHp = GetMonData(&gPlayerParty[monId], MON_DATA_MAX_HP);
    u16 hazardDamage = 0;

    if (gSideStatuses[GetBattlerSide(gActiveBattler)] & SIDE_STATUS_SPIKES)
    {
        u8 spikesLayers = gSideTimers[GetBattlerSide(gActiveBattler)].spikesAmount;
        hazardDamage += (spikesLayers * maxHp) / 8; // Each layer adds 1/8 of max HP
    }

    return hazardDamage > (maxHp / 4); // Arbitrary threshold for significant damage
}


static bool8 HasStatBoosts(u8 monId)
{
    for (u8 i = 0; i < NUM_BATTLE_STATS; i++)
    {
        if (gBattleMons[monId].statStages[i] > DEFAULT_STAT_STAGE)
            return TRUE;
    }
    return FALSE;
}

static bool8 IsValidSwitchTarget(u8 monId, u8 battlerIn1, u8 battlerIn2)
{
    if (GetMonData(&gPlayerParty[monId], MON_DATA_HP) == 0)
        return FALSE; // Pokémon is fainted
    if (gBattlerPartyIndexes[battlerIn1] == monId || gBattlerPartyIndexes[battlerIn2] == monId)
        return FALSE; // Pokémon is already in battle
    return TRUE;
}


static u8 GetAI_ItemType(u16 itemId, const u8 *itemEffect)
{
    // Check if the item is a Full Restore, which heals HP and status conditions.
    if (itemId == ITEM_FULL_RESTORE)
        return AI_ITEM_FULL_RESTORE;

    // Check if the item heals HP.
    else if (itemEffect[4] & ITEM4_HEAL_HP)
    {
        // Separate types for different HP-healing items.
        if (itemEffect[4] & ITEM4_REVIVE)
            return AI_ITEM_REVIVE; // Revive item
        else if (itemEffect[4] & ITEM4_HEAL_HP)
            return AI_ITEM_HEAL_HP; // Standard HP recovery item
    }

    // Check if the item cures status conditions.
    else if (itemEffect[3] & ITEM3_STATUS_ALL)
    {
        // Check for items that cure specific conditions, if possible.
        if (itemEffect[3] & ITEM3_SLEEP)
            return AI_ITEM_CURE_SLEEP;
        else if (itemEffect[3] & ITEM3_POISON)
            return AI_ITEM_CURE_POISON;
        else if (itemEffect[3] & ITEM3_BURN)
            return AI_ITEM_CURE_BURN;
        else if (itemEffect[3] & ITEM3_FREEZE)
            return AI_ITEM_CURE_FREEZE;
        else if (itemEffect[3] & ITEM3_PARALYSIS)
            return AI_ITEM_CURE_PARALYSIS;
        else if (itemEffect[3] & ITEM3_CONFUSION)
            return AI_ITEM_CURE_CONFUSION;

        // If none of the above conditions, assume it cures all conditions.
        return AI_ITEM_CURE_CONDITION;
    }

    // Check if the item boosts stats during battle (X-stat items).
    else if ((itemEffect[0] & (ITEM0_DIRE_HIT | ITEM0_X_ATTACK)) || itemEffect[1] != 0 || itemEffect[2] != 0)
    {
        // Identify specific X-stat items if possible.
        if (itemEffect[0] & ITEM0_DIRE_HIT)
            return AI_ITEM_DIRE_HIT;
        else if (itemEffect[0] & ITEM0_X_ATTACK)
            return AI_ITEM_X_ATTACK;
        else if (itemEffect[1] & ITEM1_X_DEFEND)
            return AI_ITEM_X_DEFEND;
        else if (itemEffect[1] & ITEM1_X_SPEED)
            return AI_ITEM_X_SPEED;
        else if (itemEffect[2] & ITEM2_X_SPATK)
            return AI_ITEM_X_SPATK;
        else if (itemEffect[2] & ITEM2_X_SPDEF)
            return AI_ITEM_X_SPDEF;

        // General X-stat category if no specific match.
        return AI_ITEM_X_STAT;
    }

    // Check if the item is Guard Spec, which prevents stat reduction.
    else if (itemEffect[3] & ITEM3_GUARD_SPEC)
        return AI_ITEM_GUARD_SPEC;

    // If none of the above categories are matched, return an unrecognized type.
    else
        return AI_ITEM_NOT_RECOGNIZABLE;
}

// Function to predict opponent's potential damage to AI's active Pokémon
static s32 EstimateOpponentDamage(u8 battler, u8 opponent)
{
    s32 i;
    s32 maxDamage = 0;
    u16 move;
    s32 damage;

    // Iterate through the opponent's moves to find the one with the highest estimated damage
    for (i = 0; i < MAX_MON_MOVES; i++)
    {
        move = gBattleMons[opponent].moves[i];
        if (move == MOVE_NONE)
            continue;

        // Calculate potential damage of this move on the AI Pokémon
        gBattleMoveDamage = 0;
        damage = AI_CalcDmg(opponent, battler, move);

        // Check if the move's calculated damage is higher than any we've found so far
        if (damage > maxDamage)
            maxDamage = damage;
    }

    return maxDamage;
}




// Main function with enhanced dynamic item usage
static bool8 ShouldUseItem(void)
{
    struct Pokemon *party;
    s32 i;
    u8 validMons = 0;
    bool8 shouldUse = FALSE;

    // Prevent the partner from using items in multi-battles.
    if (gBattleTypeFlags & BATTLE_TYPE_INGAME_PARTNER && GetBattlerPosition(gActiveBattler) == B_POSITION_PLAYER_RIGHT)
        return FALSE;

    // Set the party depending on whether the AI is the player or enemy.
    party = (GetBattlerSide(gActiveBattler) == B_SIDE_PLAYER) ? gPlayerParty : gEnemyParty;

    // Count valid Pokémon in the party.
    for (i = 0; i < PARTY_SIZE; i++)
    {
        if (GetMonData(&party[i], MON_DATA_HP) != 0
            && GetMonData(&party[i], MON_DATA_SPECIES_OR_EGG) != SPECIES_NONE
            && GetMonData(&party[i], MON_DATA_SPECIES_OR_EGG) != SPECIES_EGG)
        {
            validMons++;
        }
    }

    // Iterate through the trainer’s items.
    for (i = 0; i < MAX_TRAINER_ITEMS; i++)
    {
        u16 item = gBattleResources->battleHistory->trainerItems[i];
        const u8 *itemEffects;
        u8 paramOffset;

        // Skip item if it's non-existent or has no effects defined.
        if (item == ITEM_NONE || gItemEffectTable[item - ITEM_POTION] == NULL)
            continue;

        // Get item effect data
        itemEffects = (item == ITEM_ENIGMA_BERRY) ? gSaveBlock1Ptr->enigmaBerry.itemEffect : gItemEffectTable[item - ITEM_POTION];
        *(gBattleStruct->AI_itemType + gActiveBattler / 2) = GetAI_ItemType(item, itemEffects);

        // Evaluate item usage based on its type
        switch (*(gBattleStruct->AI_itemType + gActiveBattler / 2))
        {
        case AI_ITEM_FULL_RESTORE:
        case AI_ITEM_HEAL_HP:
            if (ShouldHealConsideringBattleContext())
                shouldUse = TRUE;
            break;

        case AI_ITEM_CURE_CONDITION:
            shouldUse = EvaluateConditionCure(itemEffects);
            break;

        case AI_ITEM_X_STAT:
            shouldUse = EvaluateXStatItem(itemEffects);
            break;

        case AI_ITEM_GUARD_SPEC:
            shouldUse = ShouldUseGuardSpec();
            break;

        case AI_ITEM_NOT_RECOGNIZABLE:
            return FALSE;
        }

        // If an item should be used, initiate item usage.
        if (shouldUse)
        {
            BtlController_EmitTwoReturnValues(BUFFER_B, B_ACTION_USE_ITEM, 0);
            *(gBattleStruct->chosenItem + (gActiveBattler / 2) * 2) = item;
            gBattleResources->battleHistory->trainerItems[i] = ITEM_NONE;
            return TRUE;
        }
    }

    return FALSE;
}

// Determine if healing is beneficial given the opponent's damage potential and type advantage
static bool8 ShouldHealConsideringBattleContext(void)
{
    s32 estimatedDamage;
    s32 estimatedCriticalDamage;
    u8 opposingBattler = BATTLE_OPPOSITE(gActiveBattler);
    s32 remainingHpAfterHealing;
    u16 healAmount;
    
    // Estimate damage the opponent could deal with its strongest move
    estimatedDamage = EstimateOpponentDamage(gActiveBattler, opposingBattler);
    
    // Calculate potential damage from a critical hit
    estimatedCriticalDamage = estimatedDamage * 1.5;

    // Determine healing amount based on item type (assuming we have access to the item data)
    healAmount = CalculateHealAmount(gBattleMons[gActiveBattler].hp, gBattleMons[gActiveBattler].maxHP);

    // Calculate the effective HP after healing
    remainingHpAfterHealing = gBattleMons[gActiveBattler].hp + healAmount;

    // Case 1: If the opponent’s estimated damage or critical hit would KO the Pokémon, avoid healing
    if (estimatedDamage >= remainingHpAfterHealing || estimatedCriticalDamage >= gBattleMons[gActiveBattler].maxHP)
        return FALSE; // Skip healing as it likely won't save the Pokémon

    // Case 2: Evaluate follow-up damage potential (e.g., if the opponent has a priority move)
    if (HasPriorityMove(opposingBattler) && estimatedDamage * 0.5 >= remainingHpAfterHealing)
        return FALSE; // Avoid healing if follow-up damage might KO

    // Case 3: Avoid healing if HP is still relatively high after the estimated damage
    if (gBattleMons[gActiveBattler].hp > gBattleMons[gActiveBattler].maxHP / 2 && estimatedDamage < gBattleMons[gActiveBattler].hp)
        return FALSE; // HP is already sufficient; healing unnecessary

    // Otherwise, healing is beneficial in this context
    return TRUE;
}

// Helper function to calculate the actual healing amount based on the healing item used
static u16 CalculateHealAmount(u16 currentHp, u16 maxHp)
{
    u16 healAmount;
    if (UsingFullRestore())
        healAmount = maxHp;
    else if (UsingHyperPotion())
        healAmount = 200;
    else if (UsingSuperPotion())
        healAmount = 50;
    else
        healAmount = maxHp / 2;
    return (currentHp + healAmount > maxHp) ? maxHp - currentHp : healAmount;
}


// Checks if the item being used is Full Restore
static bool8 UsingFullRestore(void)
{
    return gLastUsedItem == ITEM_FULL_RESTORE;
}

// Checks if the item being used is Hyper Potion
static bool8 UsingHyperPotion(void)
{
    return gLastUsedItem == ITEM_HYPER_POTION;
}

// Checks if the item being used is Super Potion
static bool8 UsingSuperPotion(void)
{
    return gLastUsedItem == ITEM_SUPER_POTION;
}

// Check if the opponent has a priority move that could pose a follow-up KO threat
static bool8 HasPriorityMove(u8 opponent)
{
    for (int i = 0; i < MAX_MON_MOVES; i++)
    {
        u16 move = gBattleMons[opponent].moves[i];
        if (move != MOVE_NONE && gBattleMoves[move].priority > 0)
            return TRUE;
    }
    return FALSE;
}


// Helper functions for item effect evaluations

static bool8 EvaluateConditionCure(const u8 *itemEffects)
{
    u8 statusFlag = 0;
    bool8 shouldUse = FALSE;
    bool8 isPhysicalAttacker = gBattleMons[gActiveBattler].attack > gBattleMons[gActiveBattler].spAttack;
    bool8 isSpecialAttacker = gBattleMons[gActiveBattler].spAttack > gBattleMons[gActiveBattler].attack;

    // Priority curing based on role and status condition
    if (itemEffects[3] & ITEM3_SLEEP && gBattleMons[gActiveBattler].status1 & STATUS1_SLEEP)
    {
        // Sleep is high priority for both roles since it prevents any action.
        statusFlag |= (1 << AI_HEAL_SLEEP);
        shouldUse = TRUE;
    }
    if (itemEffects[3] & ITEM3_PARALYSIS && gBattleMons[gActiveBattler].status1 & STATUS1_PARALYSIS)
    {
        // Paralysis is higher priority for physical attackers due to speed reduction.
        if (isPhysicalAttacker)
            statusFlag |= (1 << AI_HEAL_PARALYSIS) * 2; // Double weight for physical attackers
        else
            statusFlag |= (1 << AI_HEAL_PARALYSIS);
        shouldUse = TRUE;
    }
    if (itemEffects[3] & ITEM3_CONFUSION && gBattleMons[gActiveBattler].status2 & STATUS2_CONFUSION)
    {
        // Confusion is higher priority for special attackers since it interrupts crucial moves.
        if (isSpecialAttacker)
            statusFlag |= (1 << AI_HEAL_CONFUSION) * 2; // Double weight for special attackers
        else
            statusFlag |= (1 << AI_HEAL_CONFUSION);
        shouldUse = TRUE;
    }
    if (itemEffects[3] & ITEM3_BURN && gBattleMons[gActiveBattler].status1 & STATUS1_BURN)
    {
        // Burn is high priority for physical attackers due to the attack reduction.
        if (isPhysicalAttacker)
            statusFlag |= (1 << AI_HEAL_BURN) * 2; // Double weight for physical attackers
        else
            statusFlag |= (1 << AI_HEAL_BURN);
        shouldUse = TRUE;
    }
    if (itemEffects[3] & ITEM3_POISON && (gBattleMons[gActiveBattler].status1 & STATUS1_POISON
                                           || gBattleMons[gActiveBattler].status1 & STATUS1_TOXIC_POISON))
    {
        // Poison is generally lower priority unless the Pokémon is defensive/stall.
        statusFlag |= (1 << AI_HEAL_POISON);
        shouldUse = TRUE;
    }
    if (itemEffects[3] & ITEM3_FREEZE && gBattleMons[gActiveBattler].status1 & STATUS1_FREEZE)
    {
        // Freeze is lower priority unless there’s no other way to thaw out.
        statusFlag |= (1 << AI_HEAL_FREEZE);
        shouldUse = TRUE;
    }

    *(gBattleStruct->AI_itemFlags + gActiveBattler / 2) = statusFlag;
    return shouldUse;
}

static bool8 EvaluateXStatItem(const u8 *itemEffects)
{
    u8 statFlag = 0;

    if (gDisableStructs[gActiveBattler].isFirstTurn == 0)
        return FALSE;

    if (itemEffects[0] & ITEM0_X_ATTACK)
        statFlag |= (1 << AI_X_ATTACK);
    if (itemEffects[1] & ITEM1_X_DEFEND)
        statFlag |= (1 << AI_X_DEFEND);
    if (itemEffects[1] & ITEM1_X_SPEED)
        statFlag |= (1 << AI_X_SPEED);
    if (itemEffects[2] & ITEM2_X_SPATK)
        statFlag |= (1 << AI_X_SPATK);
    if (itemEffects[2] & ITEM2_X_ACCURACY)
        statFlag |= (1 << AI_X_ACCURACY);
    if (itemEffects[0] & ITEM0_DIRE_HIT)
        statFlag |= (1 << AI_DIRE_HIT);

    *(gBattleStruct->AI_itemFlags + gActiveBattler / 2) = statFlag;
    return statFlag != 0;
}

static bool8 ShouldUseGuardSpec(void)
{
    u8 battlerSide = GetBattlerSide(gActiveBattler);
    return (gDisableStructs[gActiveBattler].isFirstTurn != 0 && gSideTimers[battlerSide].mistTimer == 0);
}

// Main function to determine if AI should switch to a Rapid Spin user if hazards are present.
static bool8 ShouldSwitchToRapidSpinUserIfHazards(void)
{
    struct Pokemon *party;
    s32 i;
    u8 battlerSide = GetBattlerSide(gActiveBattler);
    bool8 hasHazards = FALSE;

    // Check for entry hazards on the AI's side of the field (Spikes or Stealth Rock).
    if ((gSideStatuses[battlerSide] & SIDE_STATUS_SPIKES) || (gSideStatuses[battlerSide] & SIDE_STATUS_STEALTH_ROCK))
    {
        hasHazards = TRUE;
    }

    // If there are no hazards, no need to switch.
    if (!hasHazards)
        return FALSE;

    // Determine the party based on the AI's side.
    party = (battlerSide == B_SIDE_PLAYER) ? gPlayerParty : gEnemyParty;

    // Search the party for a viable Pokémon with Rapid Spin.
    for (i = 0; i < PARTY_SIZE; i++)
    {
        // Skip invalid Pokémon (fainted, eggs, already active).
        if (GetMonData(&party[i], MON_DATA_HP) == 0)
            continue;
        if (GetMonData(&party[i], MON_DATA_SPECIES_OR_EGG) == SPECIES_NONE || GetMonData(&party[i], MON_DATA_SPECIES_OR_EGG) == SPECIES_EGG)
            continue;
        if (i == gBattlerPartyIndexes[gActiveBattler])
            continue;

        // Check each move of the Pokémon to see if it knows Rapid Spin.
        for (u8 j = 0; j < MAX_MON_MOVES; j++)
        {
            u16 move = GetMonData(&party[i], MON_DATA_MOVE1 + j);
            if (move == MOVE_RAPID_SPIN)
            {
                // Rapid Spin found, now check for valid conditions to switch.

                // Ensure the Pokémon isn't trapped by opponent's abilities like Shadow Tag or Arena Trap.
                if (IsBattlerTrapped(i))
                    continue;

                // Set the switch target to this Pokémon with Rapid Spin.
                *(gBattleStruct->AI_monToSwitchIntoId + gActiveBattler) = i;
                BtlController_EmitTwoReturnValues(BUFFER_B, B_ACTION_SWITCH, 0);
                return TRUE;
            }
        }
    }

    // No suitable Rapid Spin user found or hazards are not present.
    return FALSE;
}

// Helper function to check if a battler is trapped by abilities or conditions.
static bool8 IsBattlerTrapped(u8 battlerIndex)
{
    // Check for opponent abilities that could trap (Shadow Tag, Arena Trap, Magnet Pull).
    if (ABILITY_ON_OPPOSING_FIELD(battlerIndex, ABILITY_SHADOW_TAG))
        return TRUE;
    if (ABILITY_ON_OPPOSING_FIELD(battlerIndex, ABILITY_ARENA_TRAP))
    {
        if (!IS_BATTLER_OF_TYPE(battlerIndex, TYPE_FLYING) && !ABILITY_PRESENT(battlerIndex, ABILITY_LEVITATE))
            return TRUE;
    }
    if (ABILITY_ON_FIELD2(ABILITY_MAGNET_PULL) && IS_BATTLER_OF_TYPE(battlerIndex, TYPE_STEEL))
        return TRUE;

    // Other potential checks could be added here, like if the battler is wrapped or rooted.

    return FALSE;
}
