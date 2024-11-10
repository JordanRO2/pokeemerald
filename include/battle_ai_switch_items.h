#ifndef GUARD_BATTLE_AI_SWITCH_ITEMS_H
#define GUARD_BATTLE_AI_SWITCH_ITEMS_H

// AI Item Types
enum
{
    AI_ITEM_FULL_RESTORE = 1,
    AI_ITEM_HEAL_HP,
    AI_ITEM_CURE_CONDITION,
    AI_ITEM_CURE_CONFUSION,   // Added
    AI_ITEM_CURE_PARALYSIS,  // Added
    AI_ITEM_CURE_FREEZE,     // Added
    AI_ITEM_CURE_BURN,       // Added
    AI_ITEM_CURE_POISON,     // Added
    AI_ITEM_CURE_SLEEP,      // Added
    AI_ITEM_X_STAT,
    AI_ITEM_X_ATTACK,
    AI_ITEM_X_DEFEND,
    AI_ITEM_X_SPEED,
    AI_ITEM_X_SPATK,
    AI_ITEM_X_SPDEF,
    AI_ITEM_X_ACCURACY,      // Added if used
    AI_ITEM_X_EVASION,       // Added if used
    AI_ITEM_GUARD_SPEC,
    AI_ITEM_DIRE_HIT,        // Added
    AI_ITEM_NOT_RECOGNIZABLE
};

// AI Heal Conditions
enum {
    AI_HEAL_CONFUSION,
    AI_HEAL_PARALYSIS,
    AI_HEAL_FREEZE,
    AI_HEAL_BURN,
    AI_HEAL_POISON,
    AI_HEAL_SLEEP,
};

// AI X-Stat Items
enum {
    AI_X_ATTACK,
    AI_X_DEFEND,
    AI_X_SPEED,
    AI_X_SPATK,
    AI_X_SPDEF, // Unused
    AI_X_ACCURACY,
    AI_X_EVASION, // Unused
    AI_DIRE_HIT,
};

// Function Declarations
void AI_TrySwitchOrUseItem(void);
u8 GetMostSuitableMonToSwitchInto(void);
u8 GetAI_ItemType(void); // Added if used

#endif // GUARD_BATTLE_AI_SWITCH_ITEMS_H
