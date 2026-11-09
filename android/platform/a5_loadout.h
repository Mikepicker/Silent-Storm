/*
 *  a5_loadout.h -- the randomised, level-scaled enemy loadout tables.
 *
 *  Retail replaced this snapshot's fixed CRPGPers::pWeapon with a template
 *  system that was added to the engine after January 2003, so no code here
 *  reads it.  Generic enemy templates (AxisSoldier, AxisRifleman, ...) have
 *  WeaponID null and spawn unarmed, which leaves the AI with no weapon, no
 *  shot and -- because MoveToEnemy() is gated on HasAnyWeapon() too -- no
 *  action at all.  The chain in game.db is:
 *
 *      Pers.WeaponInHand / WeaponInBackpack
 *        -> 0x6A   weapon-set templates   ("handRifles", "handPistols", ...)
 *        -> 0x6C   weighted, level-banded bundles for that template
 *        -> 0x6B   the bundle's contents  (ChestID -> item, Quantity..MaxQuantity)
 *
 *  e.g. template 104 "handRifles": level 1 gives Rifle_33_40 + a Mauser clip,
 *  level 2 a Mauser_M98, level 12 an FG42 with FG42 clips.
 *
 *  None of those three tables is registered by this source, so db_retail.cpp
 *  captures the rows it needs while the raw tables are still loaded.
 */
#ifndef A5_LOADOUT_H
#define A5_LOADOUT_H

struct SA5LoadoutItem
{
    int nItemID;   /* RPGItems record id */
    int nCount;
};

/*  Fill pOut with the loadout for this person at this XP level, drawing one
 *  bundle per template (hand, then backpack).  Returns how many entries were
 *  written, 0 when the person has no loadout templates.  nLevel picks the
 *  highest band at or below it; weights break ties.  Deterministic for a given
 *  (nPersID, nLevel, nSeed) so a reload produces the same squad.            */
int a5_loadout_pick( int nPersID, int nLevel, int nSeed, SA5LoadoutItem *pOut, int nMax );

/*  Is there any loadout data at all?  False when game.db predates the system. */
bool a5_loadout_available();

#endif
