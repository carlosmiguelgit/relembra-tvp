
/**
 * The Violet Project - a free and open-source MMORPG server emulator
 * Copyright (C) 2019 Mark Samman <mark.samman@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "otpch.h"

#include "bed.h"
#include "chat.h"
#include "combat.h"
#include "configmanager.h"
#include "creatureevent.h"
#include "events.h"
#include "game.h"
#include "iologindata.h"
#include "monster.h"
#include "movement.h"
#include "scheduler.h"
#include "weapons.h"
#include "database.h"
#include "databasetasks.h"
#include "logger.h"

#include <fmt/format.h>

extern ConfigManager g_config;
extern Game g_game;
extern Chat* g_chat;
extern Vocations g_vocations;
extern MoveEvents* g_moveEvents;
extern Weapons* g_weapons;
extern CreatureEvents* g_creatureEvents;
extern Events* g_events;

MuteCountMap Player::muteCountMap;

uint32_t Player::playerAutoID = 0x10000000;

Player::Player(ProtocolGame_ptr p) :
	Creature(), lastPing(OTSYS_TIME()), lastPong(lastPing), storeInbox(new StoreInbox(ITEM_STORE_INBOX)), client(std::move(p))
{
	storeInbox->setParent(this);
	storeInbox->incrementReferenceCounter();

	battlepass = std::make_unique<BattlePass>();
}

Player::~Player()
{
	for (Item* item : inventory) {
		if (item) {
			item->setParent(nullptr);
			item->decrementReferenceCounter();
		}
	}

	storeInbox->setParent(nullptr);
	storeInbox->decrementReferenceCounter();

	setWriteItem(nullptr);
	setEditHouse(nullptr);
}

bool Player::setVocation(uint16_t vocId)
{
	Vocation* voc = g_vocations.getVocation(vocId);
	if (!voc) {
		return false;
	}

	bool updateSkills = vocation && getVocationId() == VOCATION_NONE && vocId != VOCATION_NONE;

	vocation = voc;

	updateRegeneration();

	// Situaton that happens when moving from Rookgaard to Mainland
	// We have to update skill tries to the actual vocation skill tries to level up
	// TFS has this bugged because nobody uses Rookgaard
	if (updateSkills) {
		for (int32_t i = SKILL_FIRST; i <= SKILL_LAST; i++) {
			uint16_t skillLevel = skills[i].level;
			uint64_t nextSkillTries = vocation->getReqSkillTries(i, skillLevel + 1);
			skills[i].tries = nextSkillTries * (skills[i].percent / 100.f);
		}
	}

	return true;
}

bool Player::isPushable() const
{
	if (hasFlag(PlayerFlag_CannotBePushed)) {
		return false;
	}
	return Creature::isPushable();
}

std::string Player::getDescription(int32_t lookDistance) const
{
	std::ostringstream s;

	if (lookDistance == -1) {
		s << "yourself.";

		if (vocation->getId() != VOCATION_NONE) {
			s << " You are " << vocation->getVocDescription() << '.';
		} else {
			s << " You have no vocation.";
		}
	} else {
		s << name;
		s << " (Level " << level << ')';
		s << '.';

		if (sex == PLAYERSEX_FEMALE) {
			s << " She";
		} else {
			s << " He";
		}

		if (vocation->getId() != VOCATION_NONE) {
			s << " is " << vocation->getVocDescription() << '.';
		} else {
			s << " has no vocation.";
		}
	}

	if (!guild || !guildRank) {
		return s.str();
	}

	if (lookDistance == -1) {
		s << " You are ";
	} else if (sex == PLAYERSEX_FEMALE) {
		s << " She is ";
	} else {
		s << " He is ";
	}

	s << guildRank->name << " of the " << guild->getName();
	if (!guildNick.empty()) {
		s << " (" << guildNick << ')';
	}

	s << '.';
	return s.str();
}

Item* Player::getInventoryItem(slots_t slot) const
{
	if (slot < CONST_SLOT_FIRST || slot > CONST_SLOT_LAST) {
		return nullptr;
	}
	return inventory[slot];
}

void Player::addConditionSuppressions(uint32_t conditions)
{
	conditionSuppressions |= conditions;
}

void Player::removeConditionSuppressions(uint32_t conditions)
{
	conditionSuppressions &= ~conditions;
}

slots_t getSlotTypeA(const ItemType& it)
{
	slots_t slot = CONST_SLOT_RIGHT;
	if (it.weaponType == WeaponType_t::WEAPON_QUIVER)
	{
		slot = CONST_SLOT_RIGHT;
	}
	else if (it.weaponType != WeaponType_t::WEAPON_SHIELD) {
		int32_t slotPosition = it.slotPosition;

		if (slotPosition & SLOTP_HEAD) {
			slot = CONST_SLOT_HEAD;
		}
		else if (slotPosition & SLOTP_NECKLACE) {
			slot = CONST_SLOT_NECKLACE;
		}
		else if (slotPosition & SLOTP_ARMOR) {
			slot = CONST_SLOT_ARMOR;
		}
		else if (slotPosition & SLOTP_LEGS) {
			slot = CONST_SLOT_LEGS;
		}
		else if (slotPosition & SLOTP_FEET) {
			slot = CONST_SLOT_FEET;
		}
		else if (slotPosition & SLOTP_RING) {
			slot = CONST_SLOT_RING;
		}
		else if (slotPosition & SLOTP_HAND) {
			slot = CONST_SLOT_RIGHT;
		}
		else if (slotPosition & SLOTP_AMMO) {
			slot = CONST_SLOT_AMMO;
		}
	}

	return slot;
}

int32_t Player::getCustomItemAttrStat(int32_t stat) const
{
	int32_t total = 0;
	for (int32_t slot = CONST_SLOT_FIRST; slot <= CONST_SLOT_LAST; slot++) {
		Item* item = inventory[slot];
		if (item) {
			int32_t toSlot = getSlotTypeA(Item::items[item->getID()]);
			if (toSlot == CONST_SLOT_RIGHT) {
				if (slot == CONST_SLOT_LEFT || slot == CONST_SLOT_RIGHT) {
					toSlot = slot;
				}
			}
			if (toSlot == slot) {
				total += item->getIntCustomAttribute(this, std::to_string(stat));
			}
		}
	}
	return total;
}

static std::set<uint32_t> IdsSorcDruid{
	1, 2, 5, 6
};

int32_t Player::getCustomItemMaximumAttrStat(int32_t stat) const
{
	int32_t maximum = 0;
	for (int32_t slot = CONST_SLOT_FIRST; slot <= CONST_SLOT_LAST; slot++) {
		Item* item = inventory[slot];
		if (item) {
			int32_t toSlot = getSlotTypeA(Item::items[item->getID()]);
			if (toSlot == CONST_SLOT_RIGHT) {
				if (slot == CONST_SLOT_LEFT || slot == CONST_SLOT_RIGHT) {
					toSlot = slot;
				}
			}

			if (toSlot == slot) {
				int32_t current = item->getIntCustomAttribute(this, std::to_string(stat));

				if (stat == ITEM_RND_CRITICAL)
				{
					WeaponType_t weaponType = item->getWeaponType();
					if (weaponType == WEAPON_AXE || weaponType == WEAPON_CLUB || weaponType == WEAPON_SWORD)
					{
						// knight e ek
						if (getVocationId() != 4 && getVocationId() != 8)
							continue;

					}
					else if (weaponType == WEAPON_DISTANCE || weaponType == WEAPON_POUCH || weaponType == WEAPON_AMMO)
					{
						// paladin e rp
						if (getVocationId() != 7 && getVocationId() != 8)
							continue;

					} else if (weaponType == WEAPON_WAND)
					{
						if (IdsSorcDruid.find(getVocationId()) == IdsSorcDruid.end())
							continue;

						// ed, ms, druid e sorcerer
					}
				}
				
				if (current > maximum) {
					maximum = current;
				}
			}
		}
	}
	return maximum;
}

Item* Player::getWeapon(slots_t slot, bool ignoreAmmo) const
{
	Item* item = inventory[slot];
	if (!item) {
		return nullptr;
	}

	WeaponType_t weaponType = item->getWeaponType();
	if (weaponType == WEAPON_POUCH)
	{
		
		Container* pouch = item->getContainer();
		if (pouch && !pouch->empty())
		{
			for (ContainerIterator containerItem = pouch->iterator(); containerItem.hasNext(); containerItem.advance()) {
				const Weapon* weapon = g_weapons->getWeapon(*containerItem);
				if (weapon) {
					if ((*containerItem)->getWeaponType() == WEAPON_DISTANCE)
						return *containerItem;
				}
			}
		}
	} else if (weaponType == WEAPON_NONE || weaponType == WEAPON_SHIELD || weaponType == WEAPON_AMMO || weaponType == WEAPON_QUIVER) {
		return nullptr;
	}

	if (!ignoreAmmo && weaponType == WEAPON_DISTANCE) {
		const ItemType& it = Item::items[item->getID()];
		if (it.ammoType != AMMO_NONE) {

			// no quiver ammo item was found
			if (Item* ammoItem = slot == CONST_SLOT_RIGHT ? inventory[CONST_SLOT_LEFT] : inventory[CONST_SLOT_RIGHT])
			{
				if (ammoItem->getAmmoType() == it.ammoType)
				{
					const ItemType& ammoItemType = Item::items[ammoItem->getID()];
					if (ammoItemType.isContainer()) {
						Container* quiver = ammoItem->getContainer();
						//check if quiver is equipped
						if (quiver != nullptr && quiver->getWeaponType() == WEAPON_QUIVER) {
							for (ContainerIterator containerItem = quiver->iterator(); containerItem.hasNext(); containerItem.advance()) {
								if (it.ammoType == (*containerItem)->getAmmoType()) {
									const Weapon* weapon = g_weapons->getWeapon(*containerItem);
									if (weapon != nullptr && weapon->ammoCheck(this)) {
										return *containerItem;
									}
								}
							}
						}
					}
				}
			}

			Item* ammoItem = inventory[CONST_SLOT_AMMO];
			// no ammo item was found
			if (!ammoItem || ammoItem->getAmmoType() != it.ammoType) // Não tem ammo, então verificamos se temos quiver
				return nullptr;

			item = ammoItem;
		}
	}
	return item;
}

Item* Player::getWeapon(bool ignoreAmmo/* = false*/) const
{
	Item* item = getWeapon(CONST_SLOT_LEFT, ignoreAmmo);
	if (item) {
		return item;
	}

	item = getWeapon(CONST_SLOT_RIGHT, ignoreAmmo);
	if (item) {
		return item;
	}
	return nullptr;
}

WeaponType_t Player::getWeaponType() const
{
	Item* item = getWeapon();
	if (!item) {
		return WEAPON_NONE;
	}
	return item->getWeaponType();
}

int32_t Player::getWeaponSkill(const Item* item) const
{
	if (!item) {
		return getSkillLevel(SKILL_FIST);
	}

	int32_t attackSkill;

	WeaponType_t weaponType = item->getWeaponType();
	switch (weaponType) {
		case WEAPON_SWORD: {
			attackSkill = getSkillLevel(SKILL_SWORD);
			break;
		}

		case WEAPON_CLUB: {
			attackSkill = getSkillLevel(SKILL_CLUB);
			break;
		}

		case WEAPON_AXE: {
			attackSkill = getSkillLevel(SKILL_AXE);
			break;
		}

		case WEAPON_DISTANCE: {
			attackSkill = getSkillLevel(SKILL_DISTANCE);
			break;
		}

		default: {
			attackSkill = 0;
			break;
		}
	}
	return attackSkill;
}

int32_t Player::getArmor() const
{
	int32_t armor = 0;

	static const slots_t armorSlots[] = { CONST_SLOT_HEAD, CONST_SLOT_NECKLACE, CONST_SLOT_ARMOR, CONST_SLOT_LEGS, CONST_SLOT_FEET, CONST_SLOT_RING };
	for (slots_t slot : armorSlots) {
		Item* inventoryItem = inventory[slot];
		if (inventoryItem) {
			armor += inventoryItem->getArmor();
		}
	}

	armor *= vocation->armorMultiplier;

	if (g_config.getBoolean(ConfigManager::USE_CLASSIC_COMBAT_FORMULAS)) {
		if (armor > 1) {
			armor = rand() % (armor >> 1) + (armor >> 1);
		}
	}

	return armor;
}

void Player::getShieldAndWeapon(const Item*& shield, const Item*& weapon) const
{
	shield = nullptr;
	weapon = nullptr;

	for (uint32_t slot = CONST_SLOT_RIGHT; slot <= CONST_SLOT_LEFT; slot++) {
		Item* item = inventory[slot];
		if (!item) {
			continue;
		}

		switch (item->getWeaponType()) {
		case WEAPON_NONE:
			break;

		case WEAPON_SHIELD: {
			if (!shield || item->getDefense() > shield->getDefense()) {
				shield = item;
			}
			break;
		}

		default: { // weapons that are not shields
			weapon = item;
			break;
		}
		}
	}
}

int32_t Player::getDefense() const
{
	int32_t defenseSkill = getSkillLevel(SKILL_FIST);
	int32_t defenseValue = 7;
	const Item* weapon;
	const Item* shield;
	getShieldAndWeapon(shield, weapon);

	if (weapon) {
		defenseValue = weapon->getDefense() + weapon->getExtraDefense();
		defenseSkill = getWeaponSkill(weapon);
	}

	if (shield) {
		defenseValue = weapon != nullptr ? shield->getDefense() + weapon->getExtraDefense() : shield->getDefense();
		defenseSkill = getSkillLevel(SKILL_SHIELD);
	}

	if (defenseSkill == 0) {
		switch (fightMode) {
		case FIGHTMODE_ATTACK:
		case FIGHTMODE_BALANCED:
			return 1;

		case FIGHTMODE_DEFENSE:
			return 2;
		}
	}

	int32_t totalDefense = 0;

	if (g_config.getBoolean(ConfigManager::USE_CLASSIC_COMBAT_FORMULAS)) {
		totalDefense = defenseValue;

		fightMode_t newFightMode = fightMode;
		if ((followCreature || !attackedCreature) && earliestAttackTime <= OTSYS_TIME()) {
			newFightMode = FIGHTMODE_DEFENSE;
		}
		
		if (newFightMode == FIGHTMODE_DEFENSE) {
			totalDefense += 8 * totalDefense / 10;
		} else if (newFightMode == FIGHTMODE_ATTACK) {
			totalDefense -= 4 * totalDefense / 10;
		}

		totalDefense *= vocation->defenseMultiplier;

		int32_t formula = (5 * (defenseSkill) + 50) * totalDefense;
		int32_t rnd = rand() % 100;
		totalDefense = formula * ((rand() % 100 + rnd) / 2) / 10000;
	} else {
		totalDefense = (defenseSkill / 4. + 2.23)* defenseValue * 0.15 * getDefenseFactor() * vocation->defenseMultiplier;
	}

	return totalDefense;
}

uint32_t Player::getAttackSpeed() const
{
	const Item* weapon = getWeapon(true);
	if (!weapon || weapon->getAttackSpeed() == 0) {
		return vocation->getAttackSpeed() + getSpecialSkill(SPECIALSKILL_ATTACKSPEED);
	}

	return weapon->getAttackSpeed();
}

float Player::getAttackFactor() const
{
	switch (fightMode) {
		case FIGHTMODE_ATTACK: return 1.2f;
		case FIGHTMODE_BALANCED: return 1.0f;
		case FIGHTMODE_DEFENSE: return 0.8f;
		default: return 1.0f;
	}
}

float Player::getDefenseFactor() const
{
	switch (fightMode) {
		case FIGHTMODE_ATTACK: return (OTSYS_TIME() - earliestAttackTime) < getAttackSpeed() ? 0.5f : 1.0f;
		case FIGHTMODE_BALANCED: return (OTSYS_TIME() - earliestAttackTime) < getAttackSpeed() ? 0.75f : 1.0f;
		case FIGHTMODE_DEFENSE: return 1.0f;
		default: return 1.0f;
	}
}

uint16_t Player::getClientIcons() const
{
	uint16_t icons = 0;
	for (Condition* condition : conditions) {
		if (!isSuppress(condition->getType())) {
			icons |= condition->getIcons();
		}
	}

	// Game client debugs with 10 or more icons
	// so let's prevent that from happening.
	std::bitset<20> icon_bitset(static_cast<uint64_t>(icons));
	for (size_t pos = 0, bits_set = icon_bitset.count(); bits_set >= 10; ++pos) {
		if (icon_bitset[pos]) {
			icon_bitset.reset(pos);
			--bits_set;
		}
	}
	return icon_bitset.to_ulong();
}

void Player::updateInventoryWeight()
{
	if (hasFlag(PlayerFlag_HasInfiniteCapacity)) {
		return;
	}

	inventoryWeight = 0;
	for (int i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; ++i) {
		const Item* item = inventory[i];
		if (item) {
			inventoryWeight += item->getWeight();
		}
	}

	//if (StoreInbox* storeInbox = getStoreInbox()) {
	//	inventoryWeight += storeInbox->getWeight();
	//}
}

void Player::addSkillAdvance(skills_t skill, uint64_t count)
{
	uint64_t currReqTries = vocation->getReqSkillTries(skill, skills[skill].level);
	uint64_t nextReqTries = vocation->getReqSkillTries(skill, skills[skill].level + 1);
	if (currReqTries >= nextReqTries) {
		//player has reached max skill
		return;
	}

	g_events->eventPlayerOnGainSkillTries(this, skill, count);
	if (count == 0) {
		return;
	}

	bool sendUpdateSkills = false;
	while ((skills[skill].tries + count) >= nextReqTries) {
		count -= nextReqTries - skills[skill].tries;
		skills[skill].level++;
		skills[skill].tries = 0;
		skills[skill].percent = 0;

		sendTextMessage(MESSAGE_EVENT_ADVANCE, fmt::format("You advanced in {:s}.", getSkillName(skill)));

		g_creatureEvents->playerAdvance(this, skill, (skills[skill].level - 1), skills[skill].level);

		sendUpdateSkills = true;
		currReqTries = nextReqTries;
		nextReqTries = vocation->getReqSkillTries(skill, skills[skill].level + 1);
		if (currReqTries >= nextReqTries) {
			count = 0;
			break;
		}
	}

	skills[skill].tries += count;

	uint32_t newPercent;
	if (nextReqTries > currReqTries) {
		newPercent = Player::getPercentLevel(skills[skill].tries, nextReqTries);
	} else {
		newPercent = 0;
	}

	if (skills[skill].percent != newPercent) {
		skills[skill].percent = newPercent;
		sendUpdateSkills = true;
	}

	if (sendUpdateSkills) {
		sendSkills();
	}
}

void Player::removeSkillTries(skills_t skill, uint64_t count, bool notify/* = false*/)
{
	uint16_t oldLevel = skills[skill].level;
	uint8_t oldPercent = skills[skill].percent;

	while (count > skills[skill].tries) {
		count -= skills[skill].tries;

		if (skills[skill].level <= 10) {
			skills[skill].level = 10;
			skills[skill].tries = 0;
			count = 0;
			break;
		}

		skills[skill].tries = vocation->getReqSkillTries(skill, skills[skill].level);
		skills[skill].level--;
	}

	skills[skill].tries = std::max<int32_t>(0, skills[skill].tries - count);
	skills[skill].percent = Player::getPercentLevel(skills[skill].tries, vocation->getReqSkillTries(skill, skills[skill].level));

	if (notify) {
		bool sendUpdateSkills = false;
		if (oldLevel != skills[skill].level) {
			sendTextMessage(MESSAGE_EVENT_ADVANCE, fmt::format("You were downgraded to {:s} level {:d}.", getSkillName(skill), skills[skill].level));
			sendUpdateSkills = true;
		}

		if (sendUpdateSkills || oldPercent != skills[skill].percent) {
			sendSkills();
		}
	}
}

void Player::setVarStats(stats_t stat, int32_t modifier)
{
	varStats[stat] += modifier;

	switch (stat) {
		case STAT_MAXHITPOINTS: {
			if (getHealth() > getMaxHealth()) {
				health = getMaxHealth();
				g_game.addCreatureHealth(this);
			} else {
				g_game.addCreatureHealth(this);
			}
			break;
		}

		case STAT_MAXMANAPOINTS: {
			if (getMana() > getMaxMana()) {
				changeMana(getMaxMana() - getMana());
			}
			break;
		}

		default: {
			break;
		}
	}
}

int32_t Player::getDefaultStats(stats_t stat) const
{
	switch (stat) {
		case STAT_MAXHITPOINTS: return healthMax;
		case STAT_MAXMANAPOINTS: return manaMax;
		case STAT_MAGICPOINTS: return getBaseMagicLevel();
		default: return 0;
	}
}

void Player::addContainer(uint8_t cid, Container* container)
{
	if (cid > 0xF) {
		return;
	}

	auto it = openContainers.find(cid);
	if (it != openContainers.end()) {
		OpenContainer& openContainer = it->second;
		openContainer.container = container;
		openContainer.index = 0;
	} else {
		OpenContainer openContainer;
		openContainer.container = container;
		openContainer.index = 0;
		openContainers[cid] = openContainer;
	}
}

void Player::closeContainer(uint8_t cid)
{
	auto it = openContainers.find(cid);
	if (it == openContainers.end()) {
		return;
	}

	OpenContainer openContainer = it->second;
	openContainers.erase(it);
}

void Player::setContainerIndex(uint8_t cid, uint16_t index)
{
	auto it = openContainers.find(cid);
	if (it == openContainers.end()) {
		return;
	}
	it->second.index = index;
}

Container* Player::getContainerByID(uint8_t cid)
{
	auto it = openContainers.find(cid);
	if (it == openContainers.end()) {
		return nullptr;
	}
	return it->second.container;
}

int8_t Player::getContainerID(const Container* container) const
{
	for (const auto& it : openContainers) {
		if (it.second.container == container) {
			return it.first;
		}
	}
	return -1;
}

uint16_t Player::getContainerIndex(uint8_t cid) const
{
	auto it = openContainers.find(cid);
	if (it == openContainers.end()) {
		return 0;
	}
	return it->second.index;
}

bool Player::canOpenCorpse(uint32_t ownerId) const
{
	return getID() == ownerId || (party && party->canOpenCorpse(ownerId));
}

uint16_t Player::getLookCorpse() const
{
	if (sex == PLAYERSEX_FEMALE) {
		return ITEM_FEMALE_CORPSE;
	} else {
		return ITEM_MALE_CORPSE;
	}
}

void Player::addStorageValue(const uint32_t key, const int32_t value, const bool /*isLogin = false */ )
{
	if (IS_IN_KEYRANGE(key, RESERVED_RANGE)) {
		if (IS_IN_KEYRANGE(key, OUTFITS_RANGE)) {
			outfits.emplace_back(
				value >> 16,
				value & 0xFF
			);
			return;
			} else if (IS_IN_KEYRANGE(key, WINGS_RANGE) || IS_IN_KEYRANGE(key, AURAS_RANGE)) {
		} else {
			std::cout << "Warning: unknown reserved key: " << key << " player: " << getName() << std::endl;
			return;
		}
	}

	if (value != -1) {
		int32_t oldValue;
		getStorageValue(key, oldValue);

		storageMap[key] = value;
	} else {
		storageMap.erase(key);
	}
}

bool Player::getStorageValue(const uint32_t key, int32_t& value) const
{
	auto it = storageMap.find(key);
	if (it == storageMap.end()) {
		value = -1;
		return false;
	}

	value = it->second;
	return true;
}

bool Player::canSeeCreature(const Creature* creature) const
{
	if (creature == this) {
		return true;
	}

	if (creature->isInGhostMode() && !canSeeGhostMode(creature)) {
		return false;
	}

	if (!creature->getPlayer() && !canSeeInvisibility() && creature->isInvisible()) {
		return false;
	}
	return true;
}

bool Player::canSeeGhostMode(const Creature*) const
{
	return group->access;
}

void Player::onReceiveMail() const
{
	if (isNearDepotBox()) {
		sendTextMessage(MESSAGE_EVENT_ADVANCE, "New mail has arrived.");
	}
}

bool Player::isNearDepotBox(int32_t depotId/* = -1*/) const
{
	const Position& pos = getPosition();
	for (int32_t cx = -1; cx <= 1; ++cx) {
		for (int32_t cy = -1; cy <= 1; ++cy) {
			Tile* tile = g_game.map.getTile(pos.x + cx, pos.y + cy, pos.z);
			if (!tile) {
				continue;
			}

			if (depotId == -1 && tile->hasFlag(TILESTATE_DEPOT)) {
				return true;
			}

			if (const TileItemVector* items = tile->getItemList()) {
				for (Item* item : *items) {
					const ItemType& it = Item::items[item->getID()];
					if (it.type == ITEM_TYPE_DEPOT) {
						DepotLocker* depotLocker = dynamic_cast<DepotLocker*>(item);
						if (depotLocker) {
							if (depotLocker->getDepotId() == depotId) {
								return true;
							}
						}
					}
				}
			}
		}
	}
	return false;
}

DepotLocker* Player::getDepotLocker(uint32_t depotId, bool autoCreate)
{
	auto it = depotLockerMap.find(depotId);
	if (it != depotLockerMap.end()) {
		return it->second.get();
	}

	if (autoCreate) {
		it = depotLockerMap.emplace(depotId, new DepotLocker(ITEM_LOCKER1)).first;
		it->second->setDepotId(depotId);
		it->second->setMaxDepotItems(getMaxDepotItems());
		return it->second.get();
	}

	return nullptr;
}

void Player::sendCancelMessage(ReturnValue message) const
{
	sendCancelMessage(getReturnMessage(message));
}

void Player::sendStats()
{
	if (client) {
		client->sendStats();
	}
}

void Player::sendPing()
{
	int64_t timeNow = OTSYS_TIME();

	bool hasLostConnection = false;
	if ((timeNow - lastPing) >= 5000) {
		lastPing = timeNow;
		if (client) {
			client->sendPing();
		} else {
			hasLostConnection = true;
		}
	}

	int64_t noPongTime = timeNow - lastPong;
	if (hasLostConnection && noPongTime >= 60 * 1000 && attackedCreature) {
		setAttackedCreature(nullptr);
	}

	int32_t noPongKickTime = vocation->getNoPongKickTime();
	if (pzLocked && noPongKickTime < 60000) {
		noPongKickTime = 60000;
	}

	if (noPongTime >= noPongKickTime) {
		if (isConnecting || getTile()->hasFlag(TILESTATE_NOLOGOUT) || pzLocked) {
			return;
		}

		isLoggingOut = true;
		g_game.executeRemoveCreature(this);
	}
}

void Player::autoOpenContainers()
{
	for (int32_t i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; i++) {
		Item* item = inventory[i];
		if (!item) {
			continue;
		}

		if (Container* container = item->getContainer()) {
			if (container->getAutoOpen() >= 0) {
				addContainer(container->getAutoOpen(), container);
				onSendContainer(container);
			}
			for (ContainerIterator it = container->iterator(); it.hasNext(); it.advance()) {
				if (Container* subContainer = (*it)->getContainer()) {
					if (subContainer->getAutoOpen() >= 0) {
						addContainer(subContainer->getAutoOpen(), subContainer);
						onSendContainer(subContainer);
					}
				}
			}
		}
	}
}

Item* Player::getWriteItem(uint32_t& windowTextId, uint16_t& maxWriteLen)
{
	windowTextId = this->windowTextId;
	maxWriteLen = this->maxWriteLen;
	return writeItem;
}

void Player::setWriteItem(Item* item, uint16_t maxWriteLen /*= 0*/)
{
	windowTextId++;

	if (writeItem) {
		writeItem->decrementReferenceCounter();
	}

	if (item) {
		writeItem = item;
		this->maxWriteLen = maxWriteLen;
		writeItem->incrementReferenceCounter();
	} else {
		writeItem = nullptr;
		this->maxWriteLen = 0;
	}
}

House* Player::getEditHouse(uint32_t& windowTextId, uint32_t& listId)
{
	windowTextId = this->windowTextId;
	listId = this->editListId;
	return editHouse;
}

void Player::setEditHouse(House* house, uint32_t listId /*= 0*/)
{
	windowTextId++;
	editHouse = house;
	editListId = listId;
}

void Player::sendHouseWindow(House* house, uint32_t listId) const
{
	if (!client) {
		return;
	}

	std::string text;
	if (house->getAccessList(listId, text)) {
		client->sendHouseWindow(windowTextId, text);
	}
}

//container
void Player::sendAddContainerItem(const Container* container, const Item* item)
{
	if (!client) {
		return;
	}

	for (const auto& it : openContainers) {
		const OpenContainer& openContainer = it.second;
		if (openContainer.container != container) {
			continue;
		}

		client->sendAddContainerItem(it.first, item);
	}
}

void Player::sendUpdateContainerItem(const Container* container, uint16_t slot, const Item* newItem)
{
	if (!client) {
		return;
	}

	for (const auto& it : openContainers) {
		const OpenContainer& openContainer = it.second;
		if (openContainer.container != container) {
			continue;
		}

		client->sendUpdateContainerItem(it.first, slot, newItem);
	}
}

void Player::sendRemoveContainerItem(const Container* container, uint16_t slot)
{
	if (!client) {
		return;
	}

	for (auto& it : openContainers) {
		OpenContainer& openContainer = it.second;
		if (openContainer.container != container) {
			continue;
		}

		client->sendRemoveContainerItem(it.first, slot);
	}
}

void Player::onUpdateTileItem(const Tile* tile, const Position& pos, const Item* oldItem,
                              const ItemType& oldType, const Item* newItem, const ItemType& newType)
{
	Creature::onUpdateTileItem(tile, pos, oldItem, oldType, newItem, newType);

	if (oldItem != newItem) {
		onRemoveTileItem(tile, pos, oldType, oldItem);
	}

	if (tradeState != TRADE_TRANSFER) {
		if (tradeItem && oldItem == tradeItem) {
			g_game.internalCloseTrade(this);
		}
	}
}

void Player::onRemoveTileItem(const Tile* tile, const Position& pos, const ItemType& iType,
                              const Item* item)
{
	Creature::onRemoveTileItem(tile, pos, iType, item);

	if (tradeState != TRADE_TRANSFER) {
		checkTradeState(item);

		if (tradeItem) {
			const Container* container = item->getContainer();
			if (container && container->isHoldingItem(tradeItem)) {
				g_game.internalCloseTrade(this);
			}
		}
	}
}

void Player::onCreatureAppear(Creature* creature, bool isLogin)
{
	Creature::onCreatureAppear(creature, isLogin);

	if (isLogin && creature == this) {
		for (int32_t slot = CONST_SLOT_FIRST; slot <= CONST_SLOT_LAST; ++slot) {
			Item* item = inventory[slot];
			if (item) {
				item->startDecaying();
				g_moveEvents->onPlayerEquip(this, item, static_cast<slots_t>(slot), false);
			}
		}

		for (Condition* condition : storedConditionList) {
			addCondition(condition);
		}
		storedConditionList.clear();

		Condition* conditionAttr = getCondition(CONDITION_REGENERATION, CONDITIONID_ITEM_ATTR);
		if (!conditionAttr)
			addCondition(Condition::createCondition(CONDITIONID_ITEM_ATTR, CONDITION_REGENERATION, -1, 0));
		
		updateRegeneration();

		BedItem* bed = g_game.getBedBySleeper(guid);
		if (bed) {
			bed->wakeUp(this);
		}

		Account account = IOLoginData::loadAccount(accountNumber);

		if (g_config.getBoolean(ConfigManager::PLAYER_CONSOLE_LOGS)) {
			g_logger.gameLog(spdlog::level::info, fmt::format("{:s} has logged in from IP {:s}.", name, convertIPToString(getIP())));
		}

		if (guild) {
			guild->addMember(this);
		}

		int32_t offlineTime;
		if (getLastLogout() != 0) {
			// Not counting more than 21 days to prevent overflow when multiplying with 1000 (for milliseconds).
			offlineTime = std::min<int32_t>(time(nullptr) - getLastLogout(), 86400 * 21);
		} else {
			offlineTime = 0;
		}

		for (Condition* condition : getMuteConditions()) {
			condition->setTicks(condition->getTicks() - (offlineTime * 1000));
			if (condition->getTicks() <= 0) {
				removeCondition(condition);
			}
		}

		g_game.checkPlayersRecord();
		IOLoginData::updateOnlineStatus(guid, true);
	}
}

void Player::onAttackedCreatureDisappear(bool isLogout)
{
	sendCancelTarget();

	if (!isLogout) {
		sendTextMessage(MESSAGE_STATUS_SMALL, "Target lost.");
	}
}

void Player::onFollowCreatureDisappear(bool isLogout)
{
	sendCancelTarget();

	if (!isLogout) {
		sendTextMessage(MESSAGE_STATUS_SMALL, "Target lost.");
	}
}

void Player::onChangeZone(ZoneType_t zone)
{
	if (zone == ZONE_PROTECTION) {
		if (attackedCreature && !hasFlag(PlayerFlag_IgnoreProtectionZone)) {
			setAttackedCreature(nullptr);
			onAttackedCreatureDisappear(false);
		}
	}

	sendIcons();
}

void Player::onAttackedCreatureChangeZone(ZoneType_t zone)
{
	if (zone == ZONE_PROTECTION) {
		if (!hasFlag(PlayerFlag_IgnoreProtectionZone)) {
			setAttackedCreature(nullptr);
			onAttackedCreatureDisappear(false);
		}
	} else if (zone == ZONE_NOPVP) {
		if (attackedCreature->getPlayer()) {
			if (!hasFlag(PlayerFlag_IgnoreProtectionZone)) {
				setAttackedCreature(nullptr);
				onAttackedCreatureDisappear(false);
			}
		}
	} else if (zone == ZONE_NORMAL) {
		//attackedCreature can leave a pvp zone if not pzlocked
		if (g_game.getWorldType() == WORLD_TYPE_NO_PVP) {
			if (attackedCreature->getPlayer()) {
				setAttackedCreature(nullptr);
				onAttackedCreatureDisappear(false);
			}
		}
	}
}

//player normal consegue atravessar o charlover pela storage que o script scripts/charlover.lua armazena
bool Player::canWalkthrough(const Creature* creature) const
{
    if (!creature) {
        return false;
    }

    const Player* other = creature->getPlayer();
    if (other) {
        int32_t value = 0;
        if (other->getStorageValue(4500, value)) {
            if (value == 1) {
                return true;
            }
        }
    }

    return false;
}

void Player::onRemoveCreature(Creature* creature, bool isLogout)
{
	Creature::onRemoveCreature(creature, isLogout);

	if (creature == this) {
		g_creatureEvents->playerLeaveGame(this);

		if (isLogout) {
			loginPosition = getPosition();
		}

		lastLogout = time(nullptr);

		setFollowCreature(nullptr);

		if (tradePartner) {
			g_game.internalCloseTrade(this);
		}

		closeShopWindow();

		clearPartyInvitations();

		if (party) {
			party->leaveParty(this, true);
		}

		g_chat->removeUserFromAllChannels(*this);

		if (g_config.getBoolean(ConfigManager::PLAYER_CONSOLE_LOGS)) {
			g_logger.gameLog(spdlog::level::info, fmt::format("{:s} has logged out from IP {:s}.", name, convertIPToString(getIP())));
		}

		if (guild) {
			guild->removeMember(this);
		}

		// Restore Maximum Mana to Default
		if (increasedMana > 0) {
			manaMax -= increasedMana;
		}

		if (increasedHP > 0) {
			healthMax -= increasedHP;
		}

		if (perseveranceMP > 0) {
			manaMax -= perseveranceMP;
		}

		if (perseveranceHP > 0) {
			healthMax -= perseveranceHP;
		}

		perseveranceHP = 0;
		perseveranceMP = 0;

		increasedMana = 0;
		increasedHP = 0;

		IOLoginData::updateOnlineStatus(guid, false);

		g_game.closeRuleViolationReport(this);

		bool saved = false;
		for (uint32_t tries = 0; tries < 3; ++tries) {
			if (IOLoginData::savePlayer(this)) {
				saved = true;
				break;
			}
		}

		if (!saved) {
			std::cout << "Error while saving player: " << getName() << std::endl;
		}
	}
}

void Player::openShopWindow(Npc* npc, const std::list<ShopInfo>& shop)
{
	shopItemList = shop;
	sendShop(npc);
	sendSaleItemList(npc->halloweenCoins);
}

bool Player::closeShopWindow(bool sendCloseShopWindow /*= true*/)
{
	//unreference callbacks
	int32_t onBuy;
	int32_t onSell;

	Npc* npc = getShopOwner(onBuy, onSell);
	if (!npc) {
		shopItemList.clear();
		return false;
	}

	setShopOwner(nullptr, -1, -1);
	npc->onPlayerEndTrade(this, purchaseCallback, saleCallback);

	if (sendCloseShopWindow) {
		sendCloseShop();
	}

	shopItemList.clear();
	return true;
}

void Player::onCreatureMove(Creature* creature, const Tile* newTile, const Position& newPos,
                            const Tile* oldTile, const Position& oldPos, bool teleport)
{
	Creature::onCreatureMove(creature, newTile, newPos, oldTile, oldPos, teleport);

	if (creature == attackedCreature && chaseMode) {
		if (isExecuting && OTSYS_TIME() < earliestAttackTime && earliestAttackTime - OTSYS_TIME() > 200) {
			if (!Position::areInRange<1, 1>(getPosition(), newPos)) {
				if (clearToDo()) {
					sendCancelWalk();
				}

				std::vector<Direction> dirList;
				if (getPathTo(newPos, dirList, 0, 1, true, true, 10)) {
					addWalkToDo(dirList, 3);
					addWaitToDo(100);
				}

				addAttackToDo();
				startToDo();
			}
		}
	}

	if (creature != this) {
		return;
	}

	if (tradeState != TRADE_TRANSFER) {
		//check if we should close trade
		if (tradeItem && !Position::areInRange<1, 1, 0>(tradeItem->getPosition(), getPosition())) {
			g_game.internalCloseTrade(this);
		}

		if (tradePartner && !Position::areInRange<2, 2, 0>(tradePartner->getPosition(), getPosition())) {
			g_game.internalCloseTrade(this);
		}
	}

	if (party) {
		party->updateSharedExperience();
	}
}

//container
void Player::onAddContainerItem(const Item* item)
{
	checkTradeState(item);
}

void Player::onUpdateContainerItem(const Container* container, const Item* oldItem, const Item* newItem)
{
	if (oldItem != newItem) {
		onRemoveContainerItem(container, oldItem);
	}

	if (tradeState != TRADE_TRANSFER) {
		checkTradeState(oldItem);
	}
}

void Player::onRemoveContainerItem(const Container* container, const Item* item)
{
	if (tradeState != TRADE_TRANSFER) {
		checkTradeState(item);

		if (tradeItem) {
			if (tradeItem->getParent() != container && container->isHoldingItem(tradeItem)) {
				g_game.internalCloseTrade(this);
			}
		}
	}
}

void Player::onCloseContainer(const Container* container)
{
	if (!client) {
		return;
	}

	for (const auto& it : openContainers) {
		if (it.second.container == container) {
			client->sendCloseContainer(it.first);
		}
	}
}

void Player::onSendContainer(const Container* container)
{
	if (!client) {
		return;
	}

	bool hasParent = container->hasParent();
	for (const auto& it : openContainers) {
		const OpenContainer& openContainer = it.second;
		if (openContainer.container == container) {
			client->sendContainer(it.first, container, hasParent);
		}
	}
}

//inventory
void Player::onUpdateInventoryItem(Item* oldItem, Item* newItem)
{
	if (oldItem != newItem) {
		onRemoveInventoryItem(oldItem);
	}

	if (tradeState != TRADE_TRANSFER) {
		checkTradeState(oldItem);
	}
}

void Player::onRemoveInventoryItem(Item* item)
{
	if (tradeState != TRADE_TRANSFER) {
		checkTradeState(item);

		if (tradeItem) {
			const Container* container = item->getContainer();
			if (container && container->isHoldingItem(tradeItem)) {
				g_game.internalCloseTrade(this);
			}
		}
	}
}

void Player::checkTradeState(const Item* item)
{
	if (!tradeItem || tradeState == TRADE_TRANSFER) {
		return;
	}

	if (tradeItem == item) {
		g_game.internalCloseTrade(this);
	} else {
		const Container* container = dynamic_cast<const Container*>(item->getParent());
		while (container) {
			if (container == tradeItem) {
				g_game.internalCloseTrade(this);
				break;
			}

			container = dynamic_cast<const Container*>(container->getParent());
		}
	}
}

void Player::onIdleStimulus()
{
#ifdef OTC_NEWWALKING
	OTCWalkList.clear();
#endif

	if (followCreature) {
		const Position& myPos = getPosition();
		const Position& targetPos = followCreature->getPosition();

		if (!Position::areInRange<1, 1>(myPos, targetPos)) {
			std::vector<Direction> dirList;
			if (!getPathTo(targetPos, dirList, 0, 1, true, true, 10)) {
				sendCancelMessage(RETURNVALUE_THEREISNOWAY);
				addWaitToDo(100);
				startToDo();
				return;
			}

			addWaitToDo(100);
			addWalkToDo(dirList);
		}

		addWaitToDo(100);
	}

	if (attackedCreature) {
		const Position& myPos = getPosition();
		const Position& targetPos = attackedCreature->getPosition();

		if (!Position::areInRange<1, 1>(myPos, targetPos)) {
			if (chaseMode) {
				std::vector<Direction> dirList;
				if (getPathTo(targetPos, dirList, 0, 1, true, true, 10)) {
					addWalkToDo(dirList, 3);
				}
			}

			addWaitToDo(100); // keep this delay here!
		}

		addAttackToDo();
	}

	startToDo();
}

void Player::onThink(uint32_t interval)
{
	Creature::onThink(interval);

	sendPing();

	onAttacking();

	MessageBufferTicks += interval;
	if (MessageBufferTicks >= 1500) {
		MessageBufferTicks = 0;
		addMessageBuffer();
	}

	if (!getTile()->hasFlag(TILESTATE_NOLOGOUT) && !isAccessPlayer()) {
		idleTime += interval;
		const int32_t kickAfterMinutes = isPremium() ? g_config.getNumber(ConfigManager::PREMIUM_KICK_AFTER_MINUTES) : g_config.getNumber(ConfigManager::KICK_AFTER_MINUTES);
		if (idleTime > (kickAfterMinutes * 60000) + 60000) {
			kickPlayer(true);
		} else if (client && idleTime == 60000 * kickAfterMinutes) {
			client->sendTextMessage(TextMessage(MESSAGE_STATUS_WARNING, fmt::format("You have been idle for {:d} minutes. You will be disconnected in one minute if you are still idle then.", kickAfterMinutes)));
		}
	}

	if (g_game.getWorldType() != WORLD_TYPE_PVP_ENFORCED) {
		checkSkullTicks();
	}
}

uint32_t Player::isMuted() const
{
	if (hasFlag(PlayerFlag_CannotBeMuted)) {
		return 0;
	}

	int32_t muteTicks = 0;
	for (Condition* condition : conditions) {
		if (condition->getType() == CONDITION_MUTED && condition->getTicks() > muteTicks) {
			muteTicks = condition->getTicks();
		}
	}
	return static_cast<uint32_t>(muteTicks) / 1000;
}

void Player::addMessageBuffer()
{
	if (MessageBufferCount > 0 && g_config.getNumber(ConfigManager::MAX_MESSAGEBUFFER) != 0 && !hasFlag(PlayerFlag_CannotBeMuted)) {
		--MessageBufferCount;
	}
}

void Player::removeMessageBuffer()
{
	if (hasFlag(PlayerFlag_CannotBeMuted)) {
		return;
	}

	const int32_t maxMessageBuffer = g_config.getNumber(ConfigManager::MAX_MESSAGEBUFFER);
	if (maxMessageBuffer != 0 && MessageBufferCount <= maxMessageBuffer + 1) {
		if (++MessageBufferCount > maxMessageBuffer) {
			uint32_t muteCount = 1;
			auto it = muteCountMap.find(guid);
			if (it != muteCountMap.end()) {
				muteCount = it->second;
			}

			uint32_t muteTime = 5 * muteCount * muteCount;
			muteCountMap[guid] = muteCount + 1;
			Condition* condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_MUTED, muteTime * 1000, 0);
			addCondition(condition);

			sendTextMessage(MESSAGE_STATUS_SMALL, fmt::format("You are muted for {:d} seconds.", muteTime));
		}
	}
}

void Player::drainHealth(Creature* attacker, int32_t damage)
{
	Creature::drainHealth(attacker, damage);
	sendStats();
}

void Player::drainMana(Creature* attacker, int32_t manaLoss)
{
	onAttacked();
	changeMana(-manaLoss);

	if (attacker) {
		addDamagePoints(attacker, manaLoss);
	}

	sendStats();
}

void Player::addManaSpent(uint64_t amount)
{
	if (hasFlag(PlayerFlag_NotGainMana)) {
		return;
	}

	uint64_t currReqMana = vocation->getReqMana(magLevel);
	uint64_t nextReqMana = vocation->getReqMana(magLevel + 1);
	if (currReqMana >= nextReqMana) {
		//player has reached max magic level
		return;
	}

	g_events->eventPlayerOnGainSkillTries(this, SKILL_MAGLEVEL, amount);
	if (amount == 0) {
		return;
	}

	bool sendUpdateStats = false;
	while ((manaSpent + amount) >= nextReqMana) {
		amount -= nextReqMana - manaSpent;

		magLevel++;
		manaSpent = 0;

		sendTextMessage(MESSAGE_EVENT_ADVANCE, fmt::format("You advanced to magic level {:d}.", magLevel));

		g_creatureEvents->playerAdvance(this, SKILL_MAGLEVEL, magLevel - 1, magLevel);

		sendUpdateStats = true;
		currReqMana = nextReqMana;
		nextReqMana = vocation->getReqMana(magLevel + 1);
		if (currReqMana >= nextReqMana) {
			return;
		}
	}

	manaSpent += amount;

	uint8_t oldPercent = magLevelPercent;
	if (nextReqMana > currReqMana) {
		magLevelPercent = Player::getPercentLevel(manaSpent, nextReqMana);
	} else {
		magLevelPercent = 0;
	}

	if (oldPercent != magLevelPercent) {
		sendUpdateStats = true;
	}

	if (sendUpdateStats) {
		sendStats();
	}
}

void Player::removeManaSpent(uint64_t amount, bool notify/* = false*/)
{
	if (amount == 0) {
		return;
	}

	uint32_t oldLevel = magLevel;
	uint8_t oldPercent = magLevelPercent;

	while (amount > manaSpent && magLevel > 0) {
		amount -= manaSpent;
		manaSpent = vocation->getReqMana(magLevel);
		magLevel--;
	}

	manaSpent -= amount;

	uint64_t nextReqMana = vocation->getReqMana(magLevel + 1);
	if (nextReqMana > vocation->getReqMana(magLevel)) {
		magLevelPercent = Player::getPercentLevel(manaSpent, nextReqMana);
	} else {
		magLevelPercent = 0;
	}

	if (notify) {
		bool sendUpdateStats = false;
		if (oldLevel != magLevel) {
			sendTextMessage(MESSAGE_EVENT_ADVANCE, fmt::format("You were downgraded to magic level {:d}.", magLevel));
			sendUpdateStats = true;
		}

		if (sendUpdateStats || oldPercent != magLevelPercent) {
			sendStats();
		}

		updateRegeneration();
	}
}

void Player::addExperience(Creature* source, uint64_t exp)
{
	uint64_t currLevelExp = Player::getExpForLevel(level);
	uint64_t nextLevelExp = Player::getExpForLevel(level + 1);
	uint64_t rawExp = exp;
	if (currLevelExp >= nextLevelExp) {
		//player has reached max level
		levelPercent = 0;
		sendStats();
		return;
	}

	if (exp == 0) {
		return;
	}

	experience += exp;

	uint32_t prevLevel = level;
	while (experience >= nextLevelExp) {
		updateBattlepass(BATTLEPASS_QUEST_GET_LEVEL);

		healthMax += vocation->getHPGain();
		//health += vocation->getHPGain();
    	if (vocation->getManaGain() > 15 && level == 6) {
			//mana += 5;
			manaMax += 5;
		}
		else {
			manaMax += vocation->getManaGain();
			//mana += vocation->getManaGain();
		}
		capacity += vocation->getCapGain();

    	++level;
		currLevelExp = nextLevelExp;
		nextLevelExp = Player::getExpForLevel(level + 1);
		if (currLevelExp >= nextLevelExp) {
			//player has reached max level
			break;
		}
	}

	if (prevLevel != level) {
		updateBaseSpeed();
		setBaseSpeed(getBaseSpeed());

		g_game.changeSpeed(this, 0);
		g_game.addCreatureHealth(this);
		updateRegeneration();

		if (party) {
			party->updateSharedExperience();
		}

		g_creatureEvents->playerAdvance(this, SKILL_LEVEL, prevLevel, level);

		sendTextMessage(MESSAGE_EVENT_ADVANCE, fmt::format("You advanced from Level {:d} to Level {:d}.", prevLevel, level));

		std::string logText = fmt::format("{} advanced from level {:d} to level {:d}!", getName(), prevLevel, level);
		std::string escapedLog = Database::getInstance().escapeString(logText);
		std::string escapedName = Database::getInstance().escapeString(getName());

		std::string query = "INSERT INTO log_register (log_text, param1, param2) VALUES (" + escapedLog + ", 0, " + escapedName + ")";

		Database::getInstance().executeQuery(query);
	}

	if (nextLevelExp > currLevelExp) {
		levelPercent = Player::getPercentLevel(experience - currLevelExp, nextLevelExp - currLevelExp);
	} else {
		levelPercent = 0;
	}
	sendStats();
}

void Player::removeExperience(uint64_t exp, bool sendText/* = false*/)
{
	if (experience == 0 || exp == 0) {
		return;
	}

	g_events->eventPlayerOnLoseExperience(this, exp);
	if (exp == 0) {
		return;
	}

	if (sendText) {
		g_game.addAnimatedText(getPosition(), TEXTCOLOR_WHITE_EXP, std::to_string(-static_cast<int32_t>(exp)));
	}

	experience = std::max<int64_t>(0, experience - exp);

	uint32_t oldLevel = level;
	uint64_t currLevelExp = Player::getExpForLevel(level);

	while (level > 1 && experience < currLevelExp) {
		--level;
		healthMax = std::max<int32_t>(0, healthMax - vocation->getHPGain());
		manaMax = std::max<int32_t>(0, manaMax - vocation->getManaGain());
		capacity = std::max<int32_t>(0, capacity - vocation->getCapGain());
		currLevelExp = Player::getExpForLevel(level);
	}

	if (oldLevel != level) {
		health = getMaxHealth();
		mana = getMaxMana();

		updateBaseSpeed();
		setBaseSpeed(getBaseSpeed());

		g_game.changeSpeed(this, 0);
		g_game.addCreatureHealth(this);

		if (party) {
			party->updateSharedExperience();
		}

		sendTextMessage(MESSAGE_EVENT_ADVANCE, fmt::format("You were downgraded from Level {:d} to Level {:d}.", oldLevel, level));
	}

	uint64_t nextLevelExp = Player::getExpForLevel(level + 1);
	if (nextLevelExp > currLevelExp) {
		levelPercent = Player::getPercentLevel(experience - currLevelExp, nextLevelExp - currLevelExp);
	} else {
		levelPercent = 0;
	}

	sendStats();
}

uint8_t Player::getPercentLevel(uint64_t count, uint64_t nextLevelCount)
{
	if (nextLevelCount == 0) {
		return 0;
	}

	uint8_t result = (count * 100) / nextLevelCount;
	if (result > 100) {
		return 0;
	}
	return result;
}

void Player::onBlockHit()
{
	if (hasShield() && bloodHitCount > 0) {
		--bloodHitCount;
			addSkillAdvance(SKILL_SHIELD, 1);
	}
}

void Player::onAttackedCreatureBlockHit(BlockType_t blockType, bool meleeHit /*= false */)
{
	lastAttackBlockType = blockType;

	switch (blockType) {
		case BLOCK_NONE: {
		if (meleeHit) {
			if (bloodHitCount > 0) {
				addAttackSkillPoint = true;
			} else {
				addAttackSkillPoint = false;
			}
			bloodHitCount = 30;
		}
		break;
	}

	case BLOCK_IMMUNITY:
	case BLOCK_DEFENSE:
	case BLOCK_ARMOR: {
		//need to draw blood every 30 hits
		if (bloodHitCount > 0) {
			addAttackSkillPoint = true;
		} else {
			addAttackSkillPoint = false;
		}
		break;
	}

	default: {
		addAttackSkillPoint = false;
		break;
	}
	}
}

bool Player::hasShield() const
{
	Item* item = inventory[CONST_SLOT_LEFT];
	if (item && item->getWeaponType() == WEAPON_SHIELD) {
		return true;
	}

	item = inventory[CONST_SLOT_RIGHT];
	if (item && item->getWeaponType() == WEAPON_SHIELD) {
		return true;
	}
	return false;
}

BlockType_t Player::blockHit(Creature* attacker, CombatType_t combatType, int32_t& damage,
                              bool checkDefense /* = false*/, bool checkArmor /* = false*/, bool field /* = false*/, bool ignoreResistances /* = false*/, bool meleeHit /*= false*/)
{
	BlockType_t blockType = Creature::blockHit(attacker, combatType, damage, checkDefense, checkArmor, field, ignoreResistances, meleeHit);

	if (attacker) {
		sendCreatureSquare(attacker, SQ_COLOR_BLACK);
	}

	if (blockType != BLOCK_NONE) {
		return blockType;
	}

	if (damage <= 0) {
		damage = 0;
		return BLOCK_ARMOR;
	}

	if (!field && meleeHit && combatType == COMBAT_PHYSICALDAMAGE && uniform_random(1, 100) <= (size_t)getParry(attacker ? attacker->getName() : "")) {
		sendTextMessage(MESSAGE_EVENT_DEFAULT, "Parry has been enabled.");

		if (attacker)
		{
			CombatDamage cDamage;
			cDamage.origin = ORIGIN_NONE;
			cDamage.type = combatType;
			cDamage.value = -(damage * (0.25f * (size_t)getParry(attacker ? attacker->getName() : "")) + 0.5f);
			if (attacker->getPlayer())
				cDamage.value /= 2;
			g_game.combatChangeHealth(getCreature(), attacker->getCreature(), cDamage);
			return BLOCK_ARMOR;
		}
		
		damage = 0;
		return BLOCK_ARMOR;
	}

	if (!ignoreResistances) {
		for (int32_t slot = CONST_SLOT_FIRST; slot <= CONST_SLOT_AMMO; ++slot) {
			if (!isItemAbilityEnabled(static_cast<slots_t>(slot))) {
				continue;
			}

			Item* item = inventory[slot];
			if (!item) {
				continue;
			}

			const ItemType& it = Item::items[item->getID()];
			if (!it.abilities) {
				if (damage <= 0) {
					damage = 0;
					return BLOCK_ARMOR;
				}

				continue;
			}

			int16_t absorbPercent = it.abilities->absorbPercent[combatTypeToIndex(combatType)];

			if (combatType == COMBAT_PHYSICALDAMAGE) {
				absorbPercent += item->getIntCustomAttribute(this, std::to_string(ITEM_RND_RESIST_PHYSICAL));
			}
			else if (combatType == COMBAT_ENERGYDAMAGE) {
				absorbPercent += item->getIntCustomAttribute(this, std::to_string(ITEM_RND_RESIST_ENERGY));
			}
			else if (combatType == COMBAT_FIREDAMAGE) {
				absorbPercent += item->getIntCustomAttribute(this, std::to_string(ITEM_RND_RESIST_FIRE));
			}
			else if (combatType == COMBAT_EARTHDAMAGE) {
				absorbPercent += item->getIntCustomAttribute(this, std::to_string(ITEM_RND_RESIST_POISON));
			}
			else if (combatType == COMBAT_ICEDAMAGE) {
				absorbPercent += item->getIntCustomAttribute(this, std::to_string(ITEM_RND_RESIST_ICE));
			}
			if (absorbPercent != 0) {
				damage = (100 - absorbPercent) * damage / 100;

				uint16_t charges = item->getCharges();
				if (charges != 0) {
					g_game.transformItem(item, item->getID(), charges - 1);
				}

				if (damage <= 0) {
					damage = 0;
					return BLOCK_IMMUNITY;
				}
			}

			if (field) {
				const int16_t fieldAbsorbPercent = it.abilities->fieldAbsorbPercent[combatTypeToIndex(combatType)];
				if (fieldAbsorbPercent != 0) {
					damage = (100 - absorbPercent) * damage / 100;

					uint16_t charges = item->getCharges();
					if (charges != 0) {
						g_game.transformItem(item, item->getID(), charges - 1);
					}
				}
			}
		}
	}

	if (damage <= 0) {
		damage = 0;
		blockType = BLOCK_ARMOR;
	}
	return blockType;
}

uint32_t Player::getIP() const
{
	if (client) {
		return client->getIP();
	}

	return 0;
}

void Player::death(Creature* lastHitCreature)
{
	std::ostringstream ss;
	ss << getName() << " died in " << getPosition();
	g_logger.gameLog(spdlog::level::info, ss.str());

	loginPosition = town->getTemplePosition();

	if (skillLoss) {
		uint8_t unfairFightReduction = 100;
		bool lastHitPlayer = Player::lastHitIsPlayer(lastHitCreature);

		if (lastHitPlayer && g_config.getBoolean(ConfigManager::ALLOW_UNFAIRFIGHT_DEATH_REDUCTION)) {
			uint32_t sumLevels = 0;
			uint32_t inFightTicks = g_config.getNumber(ConfigManager::PZ_LOCKED);
			for (const auto& it : damageMap) {
				CountBlock_t cb = it;
				if ((OTSYS_TIME() - cb.ticks) <= inFightTicks) {
					Player* damageDealer = g_game.getPlayerByID(it.CreatureID);
					if (damageDealer) {
						sumLevels += damageDealer->getLevel();
					}
				}
			}

			if (sumLevels > level) {
				double reduce = level / static_cast<double>(sumLevels);
				unfairFightReduction = std::max<uint8_t>(20, std::floor((reduce * 100) + 0.5));
			}
		}

		//Magic level loss
		uint64_t sumMana = 0;
		for (uint32_t i = 1; i <= magLevel; ++i) {
			sumMana += vocation->getReqMana(i);
		}

		double deathLossPercent = getLostPercent() * (unfairFightReduction / 100.);
		removeManaSpent(static_cast<uint64_t>((sumMana + manaSpent) * deathLossPercent), false);

		//Skill loss
		for (uint8_t i = SKILL_FIRST; i <= SKILL_LAST; ++i) { //for each skill
			uint64_t sumSkillTries = 0;
			for (uint16_t c = 10; c <= skills[i].level; ++c) { //sum up all required tries for all skill levels
				sumSkillTries += vocation->getReqSkillTries(i, c);
			}

			sumSkillTries += skills[i].tries;

			removeSkillTries(static_cast<skills_t>(i), sumSkillTries * deathLossPercent, false);
		}

		//Level loss
		uint64_t expLoss = static_cast<uint64_t>(experience * deathLossPercent);
		g_events->eventPlayerOnLoseExperience(this, expLoss);

		if (expLoss != 0) {
			uint32_t oldLevel = level;

			experience -= expLoss;

			while (level > 1 && experience < Player::getExpForLevel(level)) {
				--level;
				healthMax = std::max<int32_t>(0, healthMax - vocation->getHPGain());
				manaMax = std::max<int32_t>(0, manaMax - vocation->getManaGain());
				capacity = std::max<int32_t>(0, capacity - vocation->getCapGain());
			}

			if (oldLevel != level) {
				sendTextMessage(MESSAGE_EVENT_ADVANCE, fmt::format("You were downgraded from Level {:d} to Level {:d}.", oldLevel, level));
			}

			uint64_t currLevelExp = Player::getExpForLevel(level);
			uint64_t nextLevelExp = Player::getExpForLevel(level + 1);
			if (nextLevelExp > currLevelExp) {
				levelPercent = Player::getPercentLevel(experience - currLevelExp, nextLevelExp - currLevelExp);
			} else {
				levelPercent = 0;
			}
		}

		// player rooking system
		if (g_config.getBoolean(ConfigManager::ALLOW_PLAYER_ROOKING)) {
			if (static_cast<int32_t>(level) <= g_config.getNumber(ConfigManager::ROOKING_LEVEL) && getVocationId() != 0 || healthMax <= 0) {
				// reset stats
				level = 1;
				experience = 0;
				healthMax = 150;
				manaMax = 0;
				capacity = 40000;
				manaSpent = 0;
				magLevel = 0;
				soul = 100;
				setVocation(0); 
				setTown(g_game.map.towns.getTown(g_config.getString(ConfigManager::ROOK_TOWN_NAME)));
				loginPosition = town->getTemplePosition();

				// reset skills
				for (uint8_t i = SKILL_FIRST; i <= SKILL_LAST; ++i) { //for each skill
					skills[i].level = 10;
					skills[i].percent = 0;
					skills[i].tries = 0;
				}

				// unlearn all spells
				learnedInstantSpellList.clear();

				// reset inventory
				for (int32_t slot = getFirstIndex(); slot < getLastIndex(); slot++) {
					if (inventory[slot]) {
						g_game.internalRemoveItem(inventory[slot]);
					}
				}

				g_events->eventPlayerOnRookedEvent(this);
			}
		}

		// Always lose all blessings
		blessings.reset();

		sendStats();
		sendSkills();

		health = healthMax;
		mana = manaMax;

		auto it = conditions.begin(), end = conditions.end();
		while (it != end) {
			Condition* condition = *it;
			if (condition->isPersistent()) {
				it = conditions.erase(it);

				condition->endCondition(this);
				onEndCondition(condition->getType());
				delete condition;
			} else {
				++it;
			}
		}
	} else {
		setSkillLoss(true);

		auto it = conditions.begin(), end = conditions.end();
		while (it != end) {
			Condition* condition = *it;
			if (condition->isPersistent()) {
				it = conditions.erase(it);

				condition->endCondition(this);
				onEndCondition(condition->getType());
				delete condition;
			} else {
				++it;
			}
		}

		health = healthMax;
		g_game.internalTeleport(this, getTemplePosition(), true);
		g_game.addCreatureHealth(this);
		onThink(EVENT_CREATURE_THINK_INTERVAL);
		onIdleStatus();
		sendStats();
	}
}

bool Player::dropCorpse(Creature* lastHitCreature, Creature* mostDamageCreature, bool lastHitUnjustified, bool mostDamageUnjustified)
{
	if (getZone() != ZONE_PVP || !Player::lastHitIsPlayer(lastHitCreature)) {
		return Creature::dropCorpse(lastHitCreature, mostDamageCreature, lastHitUnjustified, mostDamageUnjustified);
	}

	setDropLoot(true);
	return false;
}

Item* Player::getCorpse(Creature* lastHitCreature, Creature* mostDamageCreature)
{
	Item* corpse = Creature::getCorpse(lastHitCreature, mostDamageCreature);
	if (corpse && corpse->getContainer()) {
		std::unordered_map<std::string, uint16_t> names;
		for (const auto& killer : getKillers()) {
			++names[killer->getName()];
		}

		if (lastHitCreature) {
			corpse->setSpecialDescription(fmt::format("You recognize {:s}. {:s} was killed by {:s}.", getNameDescription(), getSex() == PLAYERSEX_FEMALE ? "She" : "He", lastHitCreature->getNameDescription()));
		} else if (mostDamageCreature) {
			corpse->setSpecialDescription(fmt::format("You recognize {:s}. {:s} was killed by {:s}.", getNameDescription(), getSex() == PLAYERSEX_FEMALE ? "She" : "He", mostDamageCreature->getNameDescription()));
		} else {
			corpse->setSpecialDescription(fmt::format("You recognize {:s}.", getNameDescription()));
		}
	}
	return corpse;
}

void Player::addInFightTicks(bool pzlock /*= false*/)
{
	if (isAccessPlayer()) {
		return;
	}

	if (pzlock) {
		pzLocked = true;
	}

	Condition* condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_INFIGHT, g_config.getNumber(ConfigManager::PZ_LOCKED), 0);
	addCondition(condition);
}

void Player::removeList()
{
	g_game.removePlayer(this);

	for (const auto& it : g_game.getPlayers()) {
		it.second->notifyStatusChange(this, VIPSTATUS_OFFLINE);
	}
}

void Player::addList()
{
	for (const auto& it : g_game.getPlayers()) {
		it.second->notifyStatusChange(this, VIPSTATUS_ONLINE);
	}

	g_game.addPlayer(this);
}

void Player::kickPlayer(bool displayEffect, bool force)
{
	if (force || !isPzLocked()) {
		if (displayEffect) {
			g_game.addMagicEffect(getPosition(), CONST_ME_POFF);
		}

		g_game.removeCreature(this);
	}

	if (client) {
		client->disconnect();
	}
}

void Player::notifyStatusChange(Player* loginPlayer, VipStatus_t status)
{
	if (!client) {
		return;
	}

	auto it = VIPList.find(loginPlayer->guid);
	if (it == VIPList.end()) {
		return;
	}

	client->sendUpdatedVIPStatus(loginPlayer->guid, status);

}

bool Player::removeVIP(uint32_t vipGuid)
{
	if (VIPList.erase(vipGuid) == 0) {
		return false;
	}

	IOLoginData::removeVIPEntry(accountNumber, vipGuid);
	return true;
}

bool Player::addVIP(uint32_t vipGuid, const std::string& vipName, VipStatus_t status)
{
	if (VIPList.size() >= getMaxVIPEntries()) {
		sendTextMessage(MESSAGE_STATUS_SMALL, "You cannot add more buddies.");
		return false;
	}

	auto result = VIPList.insert(vipGuid);
	if (!result.second) {
		sendTextMessage(MESSAGE_STATUS_SMALL, "This player is already in your list.");
		return false;
	}

	IOLoginData::addVIPEntry(accountNumber, vipGuid);
	if (client) {
		client->sendVIP(vipGuid, vipName, status);
	}
	return true;
}

bool Player::addVIPInternal(uint32_t vipGuid)
{
	if (VIPList.size() >= getMaxVIPEntries()) {
		return false;
	}

	return VIPList.insert(vipGuid).second;
}

//close container and its child containers
void Player::autoCloseContainers(const Container* container)
{
	std::vector<uint32_t> closeList;
	for (const auto& it : openContainers) {
		Container* tmpContainer = it.second.container;
		while (tmpContainer) {
			if (tmpContainer->isRemoved() || tmpContainer == container) {
				closeList.push_back(it.first);
				break;
			}

			tmpContainer = dynamic_cast<Container*>(tmpContainer->getParent());
		}
	}

	for (uint32_t containerId : closeList) {
		closeContainer(containerId);
		if (client) {
			client->sendCloseContainer(containerId);
		}
	}
}

bool Player::hasCapacity(const Item* item, uint32_t count) const
{
	if (hasFlag(PlayerFlag_CannotPickupItem)) {
		return false;
	}

	if (hasFlag(PlayerFlag_HasInfiniteCapacity) || item->getTopParent() == this) {
		return true;
	}

	uint32_t itemWeight = item->getContainer() != nullptr ? item->getWeight() : item->getBaseWeight();
	if (item->isStackable()) {
		itemWeight *= count;
	}
	return itemWeight <= getFreeCapacity();
}

ReturnValue Player::queryAdd(int32_t index, const Thing& thing, uint32_t count, uint32_t flags, Creature*) const
{
	const Item* item = thing.getItem();
	if (item == nullptr) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	bool childIsOwner = hasBitSet(FLAG_CHILDISOWNER, flags);
	if (childIsOwner) {
		//a child container is querying the player, just check if enough capacity
		bool skipLimit = hasBitSet(FLAG_NOLIMIT, flags);
		if (skipLimit || hasCapacity(item, count)) {
			return RETURNVALUE_NOERROR;
		}
		return RETURNVALUE_NOTENOUGHCAPACITY;
	}

	if (!item->isPickupable()) {
		return RETURNVALUE_CANNOTPICKUP;
	}

	if (item->isStoreItem()) {
		return RETURNVALUE_ITEMCANNOTBEMOVEDTHERE;
	}

	ReturnValue ret = RETURNVALUE_NOTPOSSIBLE;

	const int32_t& slotPosition = item->getSlotPosition();
	if ((slotPosition & SLOTP_HEAD) || (slotPosition & SLOTP_NECKLACE) ||
	        (slotPosition & SLOTP_BACKPACK) || (slotPosition & SLOTP_ARMOR) ||
	        (slotPosition & SLOTP_LEGS) || (slotPosition & SLOTP_FEET) ||
	        (slotPosition & SLOTP_RING)) {
		ret = RETURNVALUE_CANNOTBEDRESSED;
	} else if (slotPosition & SLOTP_TWO_HAND) {
		ret = RETURNVALUE_PUTTHISOBJECTINBOTHHANDS;
	} else if ((slotPosition & SLOTP_RIGHT) || (slotPosition & SLOTP_LEFT)) {
		ret = RETURNVALUE_PUTTHISOBJECTINYOURHAND;
	}

	switch (index) {
		case CONST_SLOT_RIGHT:
		case CONST_SLOT_LEFT:
		{
			if (index == CONST_SLOT_LEFT)
			{
				const Item* left = thing.getItem();
				const Item* right = inventory[CONST_SLOT_RIGHT];

				if (!left || !right)
					break;

				if (left->getWeaponType() == WEAPON_SHIELD && right->getWeaponType() == WEAPON_SHIELD) {
					const ItemAttributes::CustomAttribute* attrPrev = const_cast<Item*>(right)->getCustomAttribute("ry");
					const ItemAttributes::CustomAttribute* attrNext = const_cast<Item*>(left)->getCustomAttribute("ry");

					if (attrPrev)
					{
						if (!attrNext)
							return RETURNVALUE_CANONLYUSEONESHIELDWITHATTRIBUTE1;
						else if (attrNext)
							return RETURNVALUE_CANONLYUSEONESHIELDWITHATTRIBUTE3;
					}
					else if (attrNext) {
						return RETURNVALUE_CANONLYUSEONESHIELDWITHATTRIBUTE2;
					}
				}
			} else if (index == CONST_SLOT_RIGHT)
			{
				const Item* right = thing.getItem();
				const Item* left = inventory[CONST_SLOT_LEFT];

				if (!left || !right)
					break;

				if (left->getWeaponType() == WEAPON_SHIELD && right->getWeaponType() == WEAPON_SHIELD) {

					const ItemAttributes::CustomAttribute* attrPrev = const_cast<Item*>(left)->getCustomAttribute("ry");
					const ItemAttributes::CustomAttribute* attrNext = const_cast<Item*>(right)->getCustomAttribute("ry");

					if (attrPrev)
					{
						if (!attrNext)
							return RETURNVALUE_CANONLYUSEONESHIELDWITHATTRIBUTE1;
						else
							return RETURNVALUE_CANONLYUSEONESHIELDWITHATTRIBUTE3;
					}
					else if (attrNext) {
						return RETURNVALUE_CANONLYUSEONESHIELDWITHATTRIBUTE2;
					}
					// ret = RETURNVALUE_CANONLYUSEONESHIELD;
				}
			}

			break;
		}
		default:
			break;
	}

	switch (index) {
		case CONST_SLOT_HEAD: {
			if (slotPosition & SLOTP_HEAD) {
				ret = RETURNVALUE_NOERROR;
			}
			break;
		}

		case CONST_SLOT_NECKLACE: {
			if (slotPosition & SLOTP_NECKLACE) {
				ret = RETURNVALUE_NOERROR;
			}
			break;
		}

		case CONST_SLOT_BACKPACK: {
			if (slotPosition & SLOTP_BACKPACK) {
				ret = RETURNVALUE_NOERROR;
			}
			break;
		}

		case CONST_SLOT_ARMOR: {
			if (slotPosition & SLOTP_ARMOR) {
				ret = RETURNVALUE_NOERROR;
			}
			break;
		}

		case CONST_SLOT_RIGHT: {
			if (slotPosition & SLOTP_RIGHT) {
				
				WeaponType_t type = item->getWeaponType();

				// Validação de vocação mesmo com mão oposta vazia
				if (type == WEAPON_QUIVER && !(getVocationId() == 3 || getVocationId() == 7)) {
					ret = RETURNVALUE_YOURVOCATIONCANNOTUSETHISITEM;
					break;
				}
				if (type == WEAPON_WAND && !(getVocationId() == 1 || getVocationId() == 2 || getVocationId() == 5 || getVocationId() == 6)) {
					ret = RETURNVALUE_YOURVOCATIONCANNOTUSETHISITEM;
					break;
				}				
				if (slotPosition & SLOTP_TWO_HAND) {
					if (inventory[CONST_SLOT_LEFT] && inventory[CONST_SLOT_LEFT] != item) {
						const Item* leftItem = inventory[CONST_SLOT_LEFT];
						WeaponType_t type = item->getWeaponType();
						WeaponType_t leftType = leftItem->getWeaponType();

						// Validar vocação para quiver
						if (type == WEAPON_QUIVER && !(getVocationId() == 3 || getVocationId() == 7)) {
							ret = RETURNVALUE_YOURVOCATIONCANNOTUSETHISITEM;
							break;
						}
						if (leftType == WEAPON_QUIVER && !(getVocationId() == 3 || getVocationId() == 7)) {
							ret = RETURNVALUE_YOURVOCATIONCANNOTUSETHISITEM;
							break;
						}

						// Validar vocação para wand
						if (type == WEAPON_WAND && !(getVocationId() == 1 || getVocationId() == 2 || getVocationId() == 5 || getVocationId() == 6)) {
							ret = RETURNVALUE_YOURVOCATIONCANNOTUSETHISITEM;
							break;
						}

						// Permitir bow/crossbow com quiver
						if (type == WEAPON_QUIVER && (leftItem->isBow() || leftItem->isCrossbow())) {
							ret = RETURNVALUE_NOERROR;
						}
						else if (leftType == WEAPON_QUIVER && (item->isBow() || item->isCrossbow())) {
							ret = RETURNVALUE_NOERROR;
						}
						else {
							ret = RETURNVALUE_BOTHHANDSNEEDTOBEFREE;
						}
					} else {
						ret = RETURNVALUE_NOERROR;
					}
				}
				else if (inventory[CONST_SLOT_LEFT]) {
					const Item* leftItem = inventory[CONST_SLOT_LEFT];
					WeaponType_t type = item->getWeaponType();
					WeaponType_t leftType = leftItem->getWeaponType();

					// Validar vocações
					if (item->getWeaponType() == WEAPON_QUIVER && !(getVocationId() == 3 || getVocationId() == 7)) {
						ret = RETURNVALUE_YOURVOCATIONCANNOTUSETHISITEM;
						break;
					}

					if (type == WEAPON_WAND && !(getVocationId() == 1 || getVocationId() == 2 || getVocationId() == 5 || getVocationId() == 6)) {
						ret = RETURNVALUE_YOURVOCATIONCANNOTUSETHISITEM;
						break;
					}
					if (leftType == WEAPON_QUIVER && !(getVocationId() == 3 || getVocationId() == 7)) {
						ret = RETURNVALUE_YOURVOCATIONCANNOTUSETHISITEM;
						break;
					}

					if (leftItem->getSlotPosition() & SLOTP_TWO_HAND) {
						if (type == WEAPON_QUIVER && (leftItem->isBow() || leftItem->isCrossbow())) {
							ret = RETURNVALUE_NOERROR;
						}
						else if (leftType == WEAPON_QUIVER && (item->isBow() || item->isCrossbow())) {
							ret = RETURNVALUE_NOERROR;
						}
						else {
							ret = RETURNVALUE_DROPTWOHANDEDITEM;
						}
					}
					else if (item == leftItem && count == item->getItemCount()) {
						ret = RETURNVALUE_NOERROR;
					}
					else if (leftType == WEAPON_WAND && type == WEAPON_WAND) {
						ret = RETURNVALUE_CANONLYUSEONEWEAPON;
					}
					else if (leftType == WEAPON_NONE || type == WEAPON_NONE ||
							 leftType == WEAPON_SHIELD || leftType == WEAPON_AMMO || leftType == WEAPON_QUIVER ||
							 type == WEAPON_SHIELD || type == WEAPON_AMMO || type == WEAPON_QUIVER) {
						ret = RETURNVALUE_NOERROR;
					}
					else {
						ret = RETURNVALUE_CANONLYUSEONEWEAPON;
					}
				}
				else {
					ret = RETURNVALUE_NOERROR;
				}
			}
			break;
		}

		case CONST_SLOT_LEFT: {
			if (slotPosition & SLOTP_LEFT) {

				WeaponType_t type = item->getWeaponType();

				// Validação de vocação mesmo com mão oposta vazia
				if (type == WEAPON_QUIVER && !(getVocationId() == 3 || getVocationId() == 7)) {
					ret = RETURNVALUE_YOURVOCATIONCANNOTUSETHISITEM;
					break;
				}
				if (type == WEAPON_WAND && !(getVocationId() == 1 || getVocationId() == 2 || getVocationId() == 5 || getVocationId() == 6)) {
					ret = RETURNVALUE_YOURVOCATIONCANNOTUSETHISITEM;
					break;
				}

				if (slotPosition & SLOTP_TWO_HAND) {
					if (inventory[CONST_SLOT_RIGHT] && inventory[CONST_SLOT_RIGHT] != item) {
						const Item* rightItem = inventory[CONST_SLOT_RIGHT];
						WeaponType_t rightType = rightItem->getWeaponType();

						// Validar vocações
						if (type == WEAPON_QUIVER && !(getVocationId() == 3 || getVocationId() == 7)) {
							ret = RETURNVALUE_YOURVOCATIONCANNOTUSETHISITEM;
							break;
						}
						if (rightType == WEAPON_QUIVER && !(getVocationId() == 3 || getVocationId() == 7)) {
							ret = RETURNVALUE_YOURVOCATIONCANNOTUSETHISITEM;
							break;
						}
						if (type == WEAPON_WAND && !(getVocationId() == 1 || getVocationId() == 2 || getVocationId() == 5 || getVocationId() == 6)) {
							ret = RETURNVALUE_YOURVOCATIONCANNOTUSETHISITEM;
							break;
						}

						// Permitir bow/crossbow com quiver
						if (type == WEAPON_QUIVER && (rightItem->isBow() || rightItem->isCrossbow())) {
							ret = RETURNVALUE_NOERROR;
						}
						else if (rightType == WEAPON_QUIVER && (item->isBow() || item->isCrossbow())) {
							ret = RETURNVALUE_NOERROR;
						}
						else {
							ret = RETURNVALUE_BOTHHANDSNEEDTOBEFREE;
						}
					} else {
						ret = RETURNVALUE_NOERROR;
					}
				}
				else if (inventory[CONST_SLOT_RIGHT]) {
					const Item* rightItem = inventory[CONST_SLOT_RIGHT];
					WeaponType_t rightType = rightItem->getWeaponType();

					// Validar vocações
					if (type == WEAPON_QUIVER && !(getVocationId() == 3 || getVocationId() == 7)) {
						ret = RETURNVALUE_YOURVOCATIONCANNOTUSETHISITEM;
						break;
					}
					if (type == WEAPON_WAND && !(getVocationId() == 1 || getVocationId() == 2 || getVocationId() == 5 || getVocationId() == 6)) {
						ret = RETURNVALUE_YOURVOCATIONCANNOTUSETHISITEM;
						break;
					}
					if (rightType == WEAPON_QUIVER && !(getVocationId() == 3 || getVocationId() == 7)) {
						ret = RETURNVALUE_YOURVOCATIONCANNOTUSETHISITEM;
						break;
					}

					if (rightItem->getSlotPosition() & SLOTP_TWO_HAND) {
						if (type == WEAPON_QUIVER && (rightItem->isBow() || rightItem->isCrossbow())) {
							ret = RETURNVALUE_NOERROR;
						}
						else if (rightType == WEAPON_QUIVER && (item->isBow() || item->isCrossbow())) {
							ret = RETURNVALUE_NOERROR;
						}
						else {
							ret = RETURNVALUE_DROPTWOHANDEDITEM;
						}
					}
					else if (item == rightItem && count == item->getItemCount()) {
						ret = RETURNVALUE_NOERROR;
					}
					else if (rightType == WEAPON_WAND && type == WEAPON_WAND) {
						ret = RETURNVALUE_CANONLYUSEONEWEAPON;
					}
					else if (rightType == WEAPON_NONE || type == WEAPON_NONE ||
							 rightType == WEAPON_SHIELD || rightType == WEAPON_AMMO || rightType == WEAPON_QUIVER ||
							 type == WEAPON_SHIELD || type == WEAPON_AMMO || type == WEAPON_QUIVER) {
						ret = RETURNVALUE_NOERROR;
					}
					else {
						ret = RETURNVALUE_CANONLYUSEONEWEAPON;
					}
				}
				else {
					ret = RETURNVALUE_NOERROR;
				}
			}
			break;
		}

		case CONST_SLOT_LEGS: {
			if (slotPosition & SLOTP_LEGS) {
				ret = RETURNVALUE_NOERROR;
			}
			break;
		}

		case CONST_SLOT_FEET: {
			if (slotPosition & SLOTP_FEET) {
				ret = RETURNVALUE_NOERROR;
			}
			break;
		}

		case CONST_SLOT_RING: {
			if (slotPosition & SLOTP_RING) {
				ret = RETURNVALUE_NOERROR;
			}
			break;
		}

		case CONST_SLOT_AMMO: {
			ret = RETURNVALUE_NOERROR;
			break;
		}

		case CONST_SLOT_WHEREEVER:
		case -1:
			ret = RETURNVALUE_NOTENOUGHROOM;
			break;

		default:
			ret = RETURNVALUE_NOTPOSSIBLE;
			break;
	}

	if (ret != RETURNVALUE_NOERROR && ret != RETURNVALUE_NOTENOUGHROOM) {
		return ret;
	}

	//check if enough capacity
	if (!hasCapacity(item, count)) {
		return RETURNVALUE_NOTENOUGHCAPACITY;
	}

	if (index != CONST_SLOT_WHEREEVER && index != -1) { // we don't try to equip whereever call
		ret = g_moveEvents->onPlayerEquip(const_cast<Player*>(this), const_cast<Item*>(item), static_cast<slots_t>(index), true);
		if (ret != RETURNVALUE_NOERROR) {
			return ret;
		}
	}

	//need an exchange with source? (destination item is swapped with currently moved item)
	const Item* inventoryItem = getInventoryItem(static_cast<slots_t>(index));
	if (inventoryItem && (!inventoryItem->isStackable() || inventoryItem->getID() != item->getID())) {
		return RETURNVALUE_NEEDEXCHANGE;
	}

	return ret;
}

ReturnValue Player::queryMaxCount(int32_t index, const Thing& thing, uint32_t count, uint32_t& maxQueryCount,
		uint32_t flags) const
{
	const Item* item = thing.getItem();
	if (item == nullptr) {
		maxQueryCount = 0;
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (index == INDEX_WHEREEVER) {
		uint32_t n = 0;
		for (int32_t slotIndex = CONST_SLOT_FIRST; slotIndex <= CONST_SLOT_LAST; ++slotIndex) {
			Item* inventoryItem = inventory[slotIndex];
			if (inventoryItem) {
				if (Container* subContainer = inventoryItem->getContainer()) {
					uint32_t queryCount = 0;
					subContainer->queryMaxCount(INDEX_WHEREEVER, *item, item->getItemCount(), queryCount, flags);
					n += queryCount;

					//iterate through all items, including sub-containers (deep search)
					for (ContainerIterator it = subContainer->iterator(); it.hasNext(); it.advance()) {
						if (Container* tmpContainer = (*it)->getContainer()) {
							queryCount = 0;
							tmpContainer->queryMaxCount(INDEX_WHEREEVER, *item, item->getItemCount(), queryCount, flags);
							n += queryCount;
						}
					}
				} else if (inventoryItem->isStackable() && item->equals(inventoryItem) && inventoryItem->getItemCount() < 100) {
					uint32_t remainder = (100 - inventoryItem->getItemCount());

					if (queryAdd(slotIndex, *item, remainder, flags) == RETURNVALUE_NOERROR) {
						n += remainder;
					}
				}
			} else if (queryAdd(slotIndex, *item, item->getItemCount(), flags) == RETURNVALUE_NOERROR) { //empty slot
				if (item->isStackable()) {
					n += 100;
				} else {
					++n;
				}
			}
		}

		maxQueryCount = n;
	} else {
		const Item* destItem = nullptr;

		const Thing* destThing = getThing(index);
		if (destThing) {
			destItem = destThing->getItem();
		}

		if (destItem) {
			if (destItem->isStackable() && item->equals(destItem) && destItem->getItemCount() < 100) {
				maxQueryCount = 100 - destItem->getItemCount();
			} else {
				maxQueryCount = 0;
			}
		} else if (queryAdd(index, *item, count, flags) == RETURNVALUE_NOERROR) { //empty slot
			if (item->isStackable()) {
				maxQueryCount = 100;
			} else {
				maxQueryCount = 1;
			}

			return RETURNVALUE_NOERROR;
		}
	}

	if (maxQueryCount < count) {
		return RETURNVALUE_NOTENOUGHROOM;
	} else {
		return RETURNVALUE_NOERROR;
	}
}

ReturnValue Player::queryRemove(const Thing& thing, uint32_t count, uint32_t flags, Creature* /*= nullptr*/) const
{
	int32_t index = getThingIndex(&thing);
	if (index == -1) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	const Item* item = thing.getItem();
	if (item == nullptr) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (count == 0 || (item->isStackable() && count > item->getItemCount())) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (!item->isMoveable() && !hasBitSet(FLAG_IGNORENOTMOVEABLE, flags)) {
		return RETURNVALUE_NOTMOVEABLE;
	}

	return RETURNVALUE_NOERROR;
}

Cylinder* Player::queryDestination(int32_t& index, const Thing& thing, Item** destItem,
		uint32_t& flags)
{
	if (index == 0 /*drop to capacity window*/ || index == INDEX_WHEREEVER) {
		*destItem = nullptr;

		const Item* item = thing.getItem();
		if (item == nullptr) {
			return this;
		}

		bool autoStack = g_config.getBoolean(ConfigManager::PLAYER_INVENTORY_AUTOSTACK);
		bool isStackable = item->isStackable();

		if (hasBitSet(FLAG_IGNOREAUTOSTACK, flags)) {
			autoStack = false;
		}

		std::vector<Container*> containers;

		for (uint32_t slotIndex = CONST_SLOT_FIRST; slotIndex <= CONST_SLOT_LAST; ++slotIndex) {
			Item* inventoryItem = inventory[slotIndex];
			if (inventoryItem) {
				if (inventoryItem == tradeItem) {
					continue;
				}

				if (inventoryItem == item) {
					continue;
				}

				if (autoStack && isStackable) {
					//try find an already existing item to stack with
					if (queryAdd(slotIndex, *item, item->getItemCount(), 0) == RETURNVALUE_NOERROR) {
						if (inventoryItem->equals(item) && inventoryItem->getItemCount() < 100) {
							index = slotIndex;
							*destItem = inventoryItem;
							return this;
						}
					}

					if (Container* subContainer = inventoryItem->getContainer()) {
						containers.push_back(subContainer);
					}
				} else if (Container* subContainer = inventoryItem->getContainer()) {
					containers.push_back(subContainer);
				}
			} else if (queryAdd(slotIndex, *item, item->getItemCount(), flags) == RETURNVALUE_NOERROR) { //empty slot
				index = slotIndex;
				*destItem = nullptr;
				return this;
			}
		}

		size_t i = 0;
		while (i < containers.size()) {
			Container* tmpContainer = containers[i++];
			if (!autoStack || !isStackable) {
				//we need to find first empty container as fast as we can for non-stackable items
				uint32_t n = tmpContainer->capacity() - std::min(tmpContainer->capacity(), static_cast<uint32_t>(tmpContainer->size()));
				while (n) {
					if (tmpContainer->queryAdd(tmpContainer->capacity() - n, *item, item->getItemCount(), flags) == RETURNVALUE_NOERROR) {
						index = tmpContainer->capacity() - n;
						*destItem = nullptr;
						return tmpContainer;
					}

					--n;
				}

				if (g_config.getBoolean(ConfigManager::DEEP_PLAYER_CONTAINER_SEARCH)) {
					for (Item* tmpContainerItem : tmpContainer->getItemList()) {
						if (Container* subContainer = tmpContainerItem->getContainer()) {
							containers.push_back(subContainer);
						}
					}
				}

				continue;
			}

			uint32_t n = 0;

			for (Item* tmpItem : tmpContainer->getItemList()) {
				if (tmpItem == tradeItem) {
					continue;
				}

				if (tmpItem == item) {
					continue;
				}

				//try find an already existing item to stack with
				if (tmpItem->equals(item) && tmpItem->getItemCount() < 100) {
					index = n;
					*destItem = tmpItem;
					return tmpContainer;
				}

				if (g_config.getBoolean(ConfigManager::DEEP_PLAYER_CONTAINER_SEARCH)) {
					if (Container* subContainer = tmpItem->getContainer()) {
						containers.push_back(subContainer);
					}
				}

				n++;
			}

			if (n < tmpContainer->capacity() && tmpContainer->queryAdd(n, *item, item->getItemCount(), flags) == RETURNVALUE_NOERROR) {
				index = n;
				*destItem = nullptr;
				return tmpContainer;
			}
		}

		return this;
	}

	Thing* destThing = getThing(index);
	if (destThing) {
		*destItem = destThing->getItem();
	}

	Cylinder* subCylinder = dynamic_cast<Cylinder*>(destThing);
	if (subCylinder) {
		index = INDEX_WHEREEVER;
		*destItem = nullptr;
		return subCylinder;
	} else {
		return this;
	}
}

void Player::addThing(int32_t index, Thing* thing)
{
	if (index < CONST_SLOT_FIRST || index > CONST_SLOT_LAST) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	Item* item = thing->getItem();
	if (!item) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	item->setParent(this);
	inventory[index] = item;

	//send to client
	sendInventoryItem(static_cast<slots_t>(index), item);
}

void Player::updateThing(Thing* thing, uint16_t itemId, uint32_t count)
{
	int32_t index = getThingIndex(thing);
	if (index == -1) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	Item* item = thing->getItem();
	if (!item) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	item->setID(itemId);
	item->setSubType(count);

	//send to client
	sendInventoryItem(static_cast<slots_t>(index), item);

	//event methods
	onUpdateInventoryItem(item, item);
}

void Player::replaceThing(uint32_t index, Thing* thing)
{
	if (index > CONST_SLOT_LAST) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	Item* oldItem = getInventoryItem(static_cast<slots_t>(index));
	if (!oldItem) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	Item* item = thing->getItem();
	if (!item) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	//send to client
	sendInventoryItem(static_cast<slots_t>(index), item);

	//event methods
	onUpdateInventoryItem(oldItem, item);

	item->setParent(this);

	inventory[index] = item;
}

void Player::removeThing(Thing* thing, uint32_t count)
{
	Item* item = thing->getItem();
	if (!item) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	int32_t index = getThingIndex(thing);
	if (index == -1) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	if (item->isStackable()) {
		if (count == item->getItemCount()) {
			//send change to client
			sendInventoryItem(static_cast<slots_t>(index), nullptr);

			//event methods
			onRemoveInventoryItem(item);

			item->setParent(nullptr);
			inventory[index] = nullptr;
		} else {
			uint8_t newCount = static_cast<uint8_t>(std::max<int32_t>(0, item->getItemCount() - count));
			item->setItemCount(newCount);

			//send change to client
			sendInventoryItem(static_cast<slots_t>(index), item);

			//event methods
			onUpdateInventoryItem(item, item);
		}
	} else {
		//send change to client
		sendInventoryItem(static_cast<slots_t>(index), nullptr);

		//event methods
		onRemoveInventoryItem(item);

		item->setParent(nullptr);
		inventory[index] = nullptr;
	}
}

int32_t Player::getThingIndex(const Thing* thing) const
{
	for (int i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; ++i) {
		if (inventory[i] == thing) {
			return i;
		}
	}
	return -1;
}

size_t Player::getFirstIndex() const
{
	return CONST_SLOT_FIRST;
}

size_t Player::getLastIndex() const
{
	return CONST_SLOT_LAST + 1;
}

uint32_t Player::getItemTypeCount(uint16_t itemId, int32_t subType /*= -1*/) const
{
	uint32_t count = 0;
	for (int32_t i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; i++) {
		Item* item = inventory[i];
		if (!item) {
			continue;
		}

		if (item->getID() == itemId) {
				count += Item::countByType(item, subType);
		}

		if (Container* container = item->getContainer()) {
			for (ContainerIterator it = container->iterator(); it.hasNext(); it.advance()) {
				if ((*it)->getID() == itemId) {
					count += Item::countByType(*it, subType);
				}
			}
		}
	}
	return count;
}

uint32_t Player::removeItemOfType(uint16_t itemId, uint32_t amount, int32_t subType, bool ignoreEquipped/* = false*/) const
{
	if (amount == 0) {
		return 0;
	}

	std::list<Item*> itemList;

	for (int32_t i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; i++) {
		Item* item = inventory[i];
		if (!item) {
			continue;
		}

		if (!ignoreEquipped && item->getID() == itemId) {
			uint32_t itemCount = Item::countByType(item, subType);
			if (itemCount == 0) {
				continue;
			}

			itemList.push_front(item);
		} else if (Container* container = item->getContainer()) {
			if (container->getID() == itemId) {
				uint32_t itemCount = Item::countByType(item, subType);
				if (itemCount == 0) {
					continue;
				}

				itemList.push_front(item);
			}

			for (ContainerIterator it = container->iterator(); it.hasNext(); it.advance()) {
				Item* containerItem = *it;
				if (containerItem->getID() == itemId) {
					uint32_t itemCount = Item::countByType(containerItem, subType);
					if (itemCount == 0) {
						continue;
					}

					itemList.push_front(containerItem);
				}
			}
		}
	}

	if (itemList.empty()) {
		return 0;
	}

	uint32_t totalRemoved = 0;
	if (Item::items[itemId].stackable) {
		for (Item* item : itemList) {
			if (item->getItemCount() > amount) {
				g_game.internalRemoveItem(item, amount, false, 0, true);
				totalRemoved += amount;
				break;
			} else {
				amount -= item->getItemCount();
				g_game.internalRemoveItem(item, -1, false, 0, true);
				totalRemoved += item->getItemCount();
			}
		}
	} else {
		for (Item* item : itemList) {
			if (totalRemoved >= amount) {
				break;
			}

			g_game.internalRemoveItem(item, -1, false, 0, true);
			totalRemoved++;
		}
	}
	return totalRemoved;
}

std::map<uint32_t, uint32_t>& Player::getAllItemTypeCount(std::map<uint32_t, uint32_t>& countMap) const
{
	for (int32_t i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; i++) {
		Item* item = inventory[i];
		if (!item) {
			continue;
		}

		countMap[item->getID()] += Item::countByType(item, -1);

		if (Container* container = item->getContainer()) {
			for (ContainerIterator it = container->iterator(); it.hasNext(); it.advance()) {
				countMap[(*it)->getID()] += Item::countByType(*it, -1);
			}
		}
	}
	return countMap;
}

Thing* Player::getThing(size_t index) const
{
	if (index >= CONST_SLOT_FIRST && index <= CONST_SLOT_LAST) {
		return inventory[index];
	}
	return nullptr;
}

void Player::postAddNotification(Thing* thing, const Cylinder* oldParent, int32_t index, cylinderlink_t link /*= LINK_OWNER*/)
{
	if (link == LINK_OWNER) {
		//calling movement scripts
		g_moveEvents->onPlayerEquip(this, thing->getItem(), static_cast<slots_t>(index), false);
	}

	bool requireListUpdate = false;

	if (link == LINK_OWNER || link == LINK_TOPPARENT) {
		const Item* i = (oldParent ? oldParent->getItem() : nullptr);

		// Check if we owned the old container too, so we don't need to do anything,
		// as the list was updated in postRemoveNotification
		assert(i ? i->getContainer() != nullptr : true);

		if (i) {
			requireListUpdate = i->getContainer()->getHoldingPlayer() != this;
		} else {
			requireListUpdate = oldParent != this;
		}

		updateInventoryWeight();
		updateItemsLight();
		sendStats();
	}

	if (const Item* item = thing->getItem()) {
		if (const Container* container = item->getContainer()) {
			onSendContainer(container);
		}

		if (shopOwner && requireListUpdate) {
			updateSaleShopList(item);
		}
	} else if (const Creature* creature = thing->getCreature()) {
		if (creature == this) {
			//check containers
			std::vector<Container*> containers;

			for (const auto& it : openContainers) {
				Container* container = it.second.container;
				if (!Position::areInRange<1, 1, 0>(container->getPosition(), getPosition())) {
					containers.push_back(container);
				}
			}

			for (const Container* container : containers) {
				autoCloseContainers(container);
			}
		}
	}
}

void Player::postRemoveNotification(Thing* thing, const Cylinder* newParent, int32_t index, cylinderlink_t link /*= LINK_OWNER*/)
{
	if (link == LINK_OWNER) {
		//calling movement scripts
		g_moveEvents->onPlayerDeEquip(this, thing->getItem(), static_cast<slots_t>(index));
	}

	bool requireListUpdate = false;

	if (link == LINK_OWNER || link == LINK_TOPPARENT) {
		const Item* i = (newParent ? newParent->getItem() : nullptr);

		// Check if we owned the old container too, so we don't need to do anything,
		// as the list was updated in postRemoveNotification
		assert(i ? i->getContainer() != nullptr : true);

		if (i) {
			requireListUpdate = i->getContainer()->getHoldingPlayer() != this;
		} else {
			requireListUpdate = newParent != this;
		}

		updateInventoryWeight();
		updateItemsLight();
		sendStats();
	}

	if (const Item* item = thing->getItem()) {
		if (const Container* container = item->getContainer()) {
			if (container->isRemoved() || !Position::areInRange<1, 1, 0>(getPosition(), container->getPosition())) {
				autoCloseContainers(container);
			} else if (container->getTopParent() == this) {
				onSendContainer(container);
			} else if (const Container* topContainer = dynamic_cast<const Container*>(container->getTopParent())) {
				if (const DepotLocker* depotLocker = dynamic_cast<const DepotLocker*>(topContainer)) {
					bool isOwner = false;

					for (const auto& it : depotLockerMap) {
						if (it.second.get() == depotLocker) {
							isOwner = true;
							onSendContainer(container);
						}
					}

					if (!isOwner) {
						autoCloseContainers(container);
					}
				} else {
					onSendContainer(container);
				}
			} else {
				autoCloseContainers(container);
			}
		}

		if (shopOwner && requireListUpdate) {
			updateSaleShopList(item);
		}
	}
}

bool Player::updateSaleShopList(const Item* item)
{
	uint16_t itemId = item->getID();
	if (itemId != ITEM_GOLD_COIN && itemId != ITEM_PLATINUM_COIN && itemId != ITEM_CRYSTAL_COIN) {
		auto it = std::find_if(shopItemList.begin(), shopItemList.end(), [itemId](const ShopInfo& shopInfo) { return shopInfo.itemId == itemId && shopInfo.sellPrice != 0; });
		if (it == shopItemList.end()) {
			const Container* container = item->getContainer();
			if (!container) {
				return false;
			}

			const auto& items = container->getItemList();
			return std::any_of(items.begin(), items.end(), [this](const Item* containerItem) {
				return updateSaleShopList(containerItem);
			});
		}
	}

	if (client) {
		client->sendSaleItemList(shopItemList);
	}
	return true;
}

bool Player::hasShopItemForSale(uint32_t itemId, uint8_t subType) const
{
	const ItemType& itemType = Item::items[itemId];
	return std::any_of(shopItemList.begin(), shopItemList.end(), [&](const ShopInfo& shopInfo) {
		uint8_t serverType = getLiquidColor(shopInfo.subType);
		return shopInfo.itemId == itemId && shopInfo.buyPrice != 0 && (!itemType.isFluidContainer() || shopInfo.subType == subType || serverType == subType);
	});
}

void Player::internalAddThing(Thing* thing)
{
	internalAddThing(0, thing);
}

void Player::internalAddThing(uint32_t index, Thing* thing)
{
	Item* item = thing->getItem();
	if (!item) {
		return;
	}

	//index == 0 means we should equip this item at the most appropriate slot (no action required here)
	if (index > CONST_SLOT_WHEREEVER && index <= CONST_SLOT_LAST) {
		if (inventory[index]) {
			return;
		}

		inventory[index] = item;
		item->setParent(this);
	}
}

bool Player::setFollowCreature(Creature* creature)
{
	if (!Creature::setFollowCreature(creature)) {
		setFollowCreature(nullptr);
		setAttackedCreature(nullptr);

		sendCancelMessage(RETURNVALUE_THEREISNOWAY);
		sendCancelTarget();
		return false;
	}

	addWaitToDo(100);
	startToDo();
	return true;
}

bool Player::setAttackedCreature(Creature* creature)
{
	if (!Creature::setAttackedCreature(creature)) {
		sendCancelTarget();
		return false;
	}

	return true;
}

void doSecondAttack(uint32_t id)
{
	Player* player = g_game.getPlayerByID(id);
	if (player) {
		player->doAttacking(true);
	}
}

void Player::doAttacking(bool skipDelay)
{
	// If no valid creature, skip
	if (!attackedCreature || attackedCreature->isRemoved() || attackedCreature->getHealth() <= 0) {
		return;
	}

	if (hasCondition(CONDITION_PACIFIED)) {
		return;
	}

	if (earliestAttackTime == 0) {
		earliestAttackTime = OTSYS_TIME();
	}
	
	if (Player* targetPlayer = attackedCreature->getPlayer()) {
		if (secureMode && getSkullClient(targetPlayer) == SKULL_NONE && targetPlayer->formerLogoutTime < OTSYS_TIME() && targetPlayer->formerPartyTime < OTSYS_TIME()) {
			setAttackedCreature(nullptr);
			sendCancelTarget();
			sendCancelMessage(RETURNVALUE_TURNSECUREMODETOATTACKUNMARKEDPLAYERS);
			return;
		}
	}

	if (!skipDelay && OTSYS_TIME() < earliestAttackTime) {
		return;
	}

	const Position& targetPos = attackedCreature->getPosition();
	if (!g_game.canThrowObjectTo(getPosition(), targetPos, false)) {
		return;
	}

	bool result = false;

	Item* tool = getWeapon();
	const Weapon* weapon = g_weapons->getWeapon(tool);

	if (weapon) {
		result = weapon->useWeapon(this, tool, attackedCreature);
	} else {
		result = Weapon::useFist(this, attackedCreature);
	}

	if (result) {
		
/*		uint64_t currentTime = OTSYS_TIME();
		
		if (canActivateSpecialEffect(currentTime)) {
			bool effectActivated = false;
			if (uniform_random(1, 100) <= getCustomItemMaximumAttrStat(ITEM_RND_BLEEDING)) {
				ConditionDamage* damage = new ConditionDamage(CONDITIONID_DEFAULT, CONDITION_BLEEDING);
				damage->addDamage(10, 2000, -5 * getCustomItemMaximumAttrStat(ITEM_RND_BLEEDING));
				attackedCreature->addCondition(damage);

				sendTextMessage(MESSAGE_EVENT_DEFAULT, "Bleeding has been enabled.");
				g_game.addMagicEffect(getPosition(), CONST_ME_MAGIC_RED);
				effectActivated = true;
			}
			
			if (uniform_random(1, 100) <= getCustomItemMaximumAttrStat(ITEM_RND_ELETRICFYING)) {
				ConditionDamage* damage = new ConditionDamage(CONDITIONID_DEFAULT, CONDITION_ENERGY);
				damage->addDamage(10, 2000, -5 * getCustomItemMaximumAttrStat(ITEM_RND_ELETRICFYING));
				attackedCreature->addCondition(damage);

				sendTextMessage(MESSAGE_EVENT_DEFAULT, "Electrifying has been enabled.");
				g_game.addMagicEffect(getPosition(), CONST_ME_MAGIC_RED);
				effectActivated = true;
			}
			
			if (uniform_random(1, 100) <= getCustomItemMaximumAttrStat(ITEM_RND_BURNING)) {
				ConditionDamage* damage = new ConditionDamage(CONDITIONID_DEFAULT, CONDITION_FIRE);
				damage->addDamage(10, 2000, -5 * getCustomItemMaximumAttrStat(ITEM_RND_BURNING));
				attackedCreature->addCondition(damage);

				sendTextMessage(MESSAGE_EVENT_DEFAULT, "Burning has been enabled.");
				g_game.addMagicEffect(getPosition(), CONST_ME_MAGIC_RED);
				effectActivated = true;
			}
			
			if (uniform_random(1, 100) <= getCustomItemMaximumAttrStat(ITEM_RND_POISONING)) {
				ConditionDamage* damage = new ConditionDamage(CONDITIONID_DEFAULT, CONDITION_POISON);
				damage->addDamage(10, 2000, -5 * getCustomItemMaximumAttrStat(ITEM_RND_POISONING));
				attackedCreature->addCondition(damage);

				sendTextMessage(MESSAGE_EVENT_DEFAULT, "Poisoning has been enabled.");
				g_game.addMagicEffect(getPosition(), CONST_ME_MAGIC_RED);
				effectActivated = true;
			}
			
			if (effectActivated) {
				updateSpecialEffectTime(currentTime);
			}
		} */
		
		if ((getVocation()->getId() == 4 || getVocation()->getId() == 8) && uniform_random(1, 100) <= getCustomItemAttrStat(ITEM_RND_PERSEVERANCE) && OTSYS_TIME() + 1000 >= perseveranceTicks) {
			perseveranceTicks = OTSYS_TIME() + (2 * 60 * 1000);
			
			double perseverancePercent = (getCustomItemAttrStat(ITEM_RND_PERSEVERANCE) * 0.1);

			ConditionAttributes* attr = static_cast<ConditionAttributes*>(Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_ATTRIBUTES, 20 * 1000, 0, true, 0xFFFF));
			attr->setParam(CONDITION_PARAM_STAT_MAXHITPOINTS, healthMax * perseverancePercent);
			attr->setParam(CONDITION_PARAM_SKILL_SHIELD, getSkillLevel(SKILL_SHIELD) * perseverancePercent);
			addCondition(attr);

			changeHealth(healthMax * perseverancePercent);
			g_game.addCreatureHealth(this);

			sendStats();

			sendTextMessage(MESSAGE_EVENT_DEFAULT, "Perseverance has been enabled.");
			g_game.addMagicEffect(getPosition(), CONST_ME_MAGIC_RED);
		}

		if ((getVocation()->getId() == 4 || getVocation()->getId() == 8) && uniform_random(1, 100) <= getCustomItemAttrStat(ITEM_RND_BERSERK) && OTSYS_TIME() + 1000 > berserkTicks) {

			double berserkPercent = (getCustomItemAttrStat(ITEM_RND_BERSERK) * 0.1);

			ConditionSpeed* speed = static_cast<ConditionSpeed*>(Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_HASTE, 20 * 1000, 0, true, 0xFFFF));
			speed->setParam(CONDITION_PARAM_SPEED, 60);
			addCondition(speed);

			ConditionAttributes* attr = static_cast<ConditionAttributes*>(Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_ATTRIBUTES, 20 * 1000, 0, true, 0xFFFF));
			attr->setParam(CONDITION_PARAM_SKILL_AXE, getSkillLevel(SKILL_AXE) * berserkPercent);
			attr->setParam(CONDITION_PARAM_SKILL_CLUB, getSkillLevel(SKILL_CLUB) * berserkPercent);
			attr->setParam(CONDITION_PARAM_SKILL_SWORD, getSkillLevel(SKILL_SWORD) * berserkPercent);
			addCondition(attr);

			sendTextMessage(MESSAGE_EVENT_DEFAULT, "Berserk has been enabled.");
			g_game.addMagicEffect(getPosition(), CONST_ME_MAGIC_RED);

			berserkTicks = OTSYS_TIME() + (2 * 60 * 1000);
		}

		if ((getVocation()->getId() == 3 || getVocation()->getId() == 7) && uniform_random(1, 100) <= getCustomItemMaximumAttrStat(ITEM_RND_SHARPSHOOTER) && OTSYS_TIME() + 1000 > sharpshooterTicks) {

			float sharpshooterPercent = getCustomItemMaximumAttrStat(ITEM_RND_SHARPSHOOTER) * 0.1;

			ConditionSpeed* speed = static_cast<ConditionSpeed*>(Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_HASTE, 20 * 1000, 0, true, 0xFFFF));
			speed->setParam(CONDITION_PARAM_SPEED, 60);
			addCondition(speed);

			ConditionAttributes* attr = static_cast<ConditionAttributes*>(Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_ATTRIBUTES, 10 * 1000, 0, true, 0xFFFF));
			attr->setParam(CONDITION_PARAM_SKILL_DISTANCE, getSkillLevel(SKILL_DISTANCE) * sharpshooterPercent);
			addCondition(attr);

			sendTextMessage(MESSAGE_EVENT_DEFAULT, "Sharpshooter has been enabled.");
			g_game.addMagicEffect(getPosition(), CONST_ME_MAGIC_RED);

			sharpshooterTicks = OTSYS_TIME() + (2 * 60 * 1000);
		}

		if (uniform_random(1, 100) <= getCustomItemMaximumAttrStat(ITEM_RND_FAST_HAND) && (getVocation()->getId() == 3 || getVocation()->getId() == 7) ) {
			g_scheduler.addEvent(createSchedulerTask(500, std::bind(doSecondAttack, getID())));

			sendTextMessage(MESSAGE_EVENT_DEFAULT, "Fast Hand has been enabled.");
			g_game.addMagicEffect(getPosition(), CONST_ME_MAGIC_RED);
		}

		earliestAttackTime = OTSYS_TIME() + getAttackSpeed();
	}
}

void Player::setChaseMode(bool mode)
{
	chaseMode = mode;
}

void Player::onWalkAborted()
{
	sendCancelWalk();
	OTCWalkList.clear();
}

bool Player::hasLightScroll() const
{
	//Light scroll:
	Condition* condition = getCondition(CONDITION_FULLLIGHT, CONDITIONID_DEFAULT);
	if (condition && condition->getTicks() >= 0) {
		return true;
	}
	return false;
}

LightInfo Player::getCreatureLight() const
{
    LightInfo lightInfo = Creature::getCreatureLight();

    if (!isLightExchangeable()) {
		return lightInfo;
    }

	for (int32_t i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; ++i)
	{
		Item *item = inventory[i];
		if (item && Item::items.getItemType(item->getID()).lightLevel != 0)
		{
			LightInfo curLight = item->getLightInfo();

			int32_t brightness = curLight.level;
			int32_t red = brightness * (curLight.color / 36);
			int32_t green = brightness * (curLight.color / 6 - 6 * (curLight.color / 36));
			int32_t blue = brightness * (curLight.color % 6);

			if (brightness >= lightInfo.level)
			{
				lightInfo.level = brightness;
			}

			if (red >= lightInfo.red)
			{
				lightInfo.red = red;
			}

			if (blue >= lightInfo.blue)
			{
				lightInfo.blue = blue;
			}

			if (green >= lightInfo.green)
			{
				lightInfo.green = green;
			}
		}
	}

	if (lightInfo.level) {
		lightInfo.color = lightInfo.blue / lightInfo.level + 6 * (lightInfo.green / lightInfo.level) + 36 * (lightInfo.red / lightInfo.level);
	} else {
		lightInfo.color = 0;
	}

	return lightInfo;
}

void Player::updateItemsLight()
{
	g_game.changeLight(this);
}

void Player::onAddCondition(ConditionType_t type)
{
	Creature::onAddCondition(type);

	if (type == CONDITION_POISON || type == CONDITION_FIRE || type == CONDITION_ENERGY) {
		addInFightTicks();
	}

	sendIcons();
}

void Player::onAddCombatCondition(ConditionType_t type)
{
	switch (type) {
		case CONDITION_POISON:
			sendTextMessage(MESSAGE_STATUS_DEFAULT, "You are poisoned.");
			break;

		case CONDITION_PARALYZE:
			sendTextMessage(MESSAGE_STATUS_DEFAULT, "You are paralyzed.");
			break;

		case CONDITION_DRUNK:
			sendTextMessage(MESSAGE_STATUS_DEFAULT, "You are drunk.");
			break;

		case CONDITION_BLEEDING:
			sendTextMessage(MESSAGE_STATUS_DEFAULT, "You are bleeding.");
			break;

		default:
			break;
	}
}

void Player::onEndCondition(ConditionType_t type)
{
	Creature::onEndCondition(type);

	if (type == CONDITION_INFIGHT) {
		onIdleStatus();
		pzLocked = false;
		clearAttacked();

		if (getSkull() != SKULL_RED) {
			setSkull(SKULL_NONE);
		}

		// player has lost aggressor status
		for (auto& it : g_game.getPlayers()) {
			Player* player = it.second;
			player->removeAttacked(this);

			if (canSee(player->getPosition())) {
				sendCreatureSkull(player);
			}
		}
	}

	sendIcons();
}

void Player::onAttackedCreature(Creature* target, bool addFightTicks /* = true */)
{
	Creature::onAttackedCreature(target);

	if (target->getZone() == ZONE_PVP) {
		return;
	}

	if (target == this) {
		if (addFightTicks) {
			addInFightTicks();
		}
		return;
	}

	Player* targetPlayer = target->getPlayer();
	if (targetPlayer) {
		targetPlayer->addInFightTicks();
		addInFightTicks(true);

		if (getSkull() == SKULL_NONE && getSkullClient(targetPlayer) == SKULL_YELLOW) {
			addAttacked(targetPlayer);
			targetPlayer->sendCreatureSkull(this);
		} else if (!targetPlayer->hasAttacked(this)) {
			if (!Combat::isInPvpZone(this, targetPlayer) && !isInWar(targetPlayer) && !isPartner(targetPlayer)) {
				addAttacked(targetPlayer);

				if (targetPlayer->getSkull() == SKULL_NONE && getSkull() == SKULL_NONE && (OTSYS_TIME() >= targetPlayer->formerLogoutTime && OTSYS_TIME() >= targetPlayer->formerPartyTime)) {
					setSkull(SKULL_WHITE);
				}

				if (getSkull() == SKULL_NONE) {
					targetPlayer->sendCreatureSkull(this);
				}
			}
		}
	}

	if (addFightTicks) {
		addInFightTicks();
	}
}

void Player::onAttacked()
{
	Creature::onAttacked();

	addInFightTicks();
}

void Player::onIdleStatus()
{
	Creature::onIdleStatus();

	if (party) {
		party->clearPlayerPoints(this);
	}
}

void Player::onPlacedCreature()
{
	//scripting event - onLogin
	if (!g_creatureEvents->playerLogin(this)) {
		kickPlayer(true, true);
	}
}

void Player::onAttackedCreatureDrainHealth(Creature* target, int32_t points)
{
	Creature::onAttackedCreatureDrainHealth(target, points);

	if (target) {
		if (party && !Combat::isPlayerCombat(target)) {
			Monster* tmpMonster = target->getMonster();
			if (tmpMonster && tmpMonster->isHostile()) {
				//We have fulfilled a requirement for shared experience
				party->updatePlayerTicks(this, points);
			}
		}
	}
}

void Player::onTargetCreatureGainHealth(Creature* target, int32_t points)
{
	if (target && party) {
		Player* tmpPlayer = nullptr;

		if (target->getPlayer()) {
			tmpPlayer = target->getPlayer();
		} else if (Creature* targetMaster = target->getMaster()) {
			if (Player* targetMasterPlayer = targetMaster->getPlayer()) {
				tmpPlayer = targetMasterPlayer;
			}
		}

		if (isPartner(tmpPlayer)) {
			party->updatePlayerTicks(this, points);
		}
	}
}

bool Player::onKilledCreature(Creature* target, bool lastHit/* = true*/)
{
	bool unjustified = false;

	if (hasFlag(PlayerFlag_NotGenerateLoot)) {
		target->setDropLoot(false);
	}

	Creature::onKilledCreature(target, lastHit);

	Player* targetPlayer = target->getPlayer();
	if (!targetPlayer) {
		MonsterClass_t monsterClass = target->getMonsterClass();
		if (monsterClass != MONSTER_NONE) {
			updateBattlepass(BATTLEPASS_QUEST_KILL_MONSTERS, monsterClass);
		}
		
		if (target->isBoss()) {
			updateBattlepass(BATTLEPASS_QUEST_KILL_BOSS, target->getName());
		}
		else {
			updateBattlepass(BATTLEPASS_QUEST_KILL_MONSTER, target->getName());
		}

		return false;
	}

	if (targetPlayer->getZone() == ZONE_PVP) {
		targetPlayer->setDropLoot(false);
		targetPlayer->setSkillLoss(false);
	} else if (!isPartner(targetPlayer)) {
		if (!Combat::isInPvpZone(this, targetPlayer) && hasAttacked(targetPlayer) && !targetPlayer->hasAttacked(this) && targetPlayer != this) {
			if (targetPlayer->getSkull() == SKULL_NONE && !isInWar(targetPlayer) && targetPlayer->formerLogoutTime < OTSYS_TIME() && targetPlayer->formerPartyTime < OTSYS_TIME()) {
				unjustified = true;

				if (lastHit) {
					addUnjustifiedDead(targetPlayer);
				}
			}
		}
	}

	// only last hit gets pz-block for white skull duration
	if (lastHit && hasCondition(CONDITION_INFIGHT) && !hasFlag(PlayerFlag_IgnoreProtectionZone)) {
		pzLocked = true;
		Condition* condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_INFIGHT, g_config.getNumber(ConfigManager::WHITE_SKULL_TIME) * 1000, 0);
		addCondition(condition);
	}

	return unjustified;
}

void Player::gainExperience(uint64_t gainExp, Creature* source)
{
	if (hasFlag(PlayerFlag_NotGainExperience) || gainExp == 0 || staminaMinutes == 0) {
		return;
	}

	addExperience(source, gainExp);
}

void Player::onGainExperience(uint64_t gainExp, Creature* target)
{
	if (hasFlag(PlayerFlag_NotGainExperience)) {
		return;
	}

	if (target && !target->getPlayer() && party && party->isSharedExperienceActive() && party->isSharedExperienceEnabled()) {
		party->shareExperience(gainExp, target);
		//We will get a share of the experience through the sharing mechanism
		return;
	}

	g_events->eventPlayerOnGainExperience(this, target, gainExp, gainExp);

	Creature::onGainExperience(gainExp, target);
	gainExperience(gainExp, target);
}

void Player::onGainSharedExperience(uint64_t gainExp, Creature* source)
{
  g_events->eventPlayerOnGainExperience(this, source, gainExp, gainExp);

	Creature::onGainExperience(gainExp, source);
	gainExperience(gainExp, source);
}

bool Player::isImmune(CombatType_t type) const
{
	if (hasFlag(PlayerFlag_CannotBeAttacked)) {
		return true;
	}
	return Creature::isImmune(type);
}

bool Player::isImmune(ConditionType_t type) const
{
	if (hasFlag(PlayerFlag_CannotBeAttacked)) {
		return true;
	}
	return Creature::isImmune(type);
}

bool Player::isAttackable() const
{
	return !hasFlag(PlayerFlag_CannotBeAttacked);
}

bool Player::lastHitIsPlayer(Creature* lastHitCreature)
{
	if (!lastHitCreature) {
		return false;
	}

	if (lastHitCreature->getPlayer()) {
		return true;
	}

	Creature* lastHitMaster = lastHitCreature->getMaster();
	return lastHitMaster && lastHitMaster->getPlayer();
}

void Player::changeHealth(int32_t healthChange, bool sendHealthChange/* = true*/)
{
	Creature::changeHealth(healthChange, sendHealthChange);
	sendStats();
}

void Player::changeMana(int32_t manaChange)
{
	if (!hasFlag(PlayerFlag_HasInfiniteMana)) {
		if (manaChange > 0) {
			mana += std::min<int32_t>(manaChange, getMaxMana() - mana);
		} else {
			mana = std::max<int32_t>(0, mana + manaChange);
		}
	}

	sendStats();
}

void Player::changeSoul(int32_t soulChange)
{
	if (soulChange > 0) {
		soul += std::min<int32_t>(soulChange, vocation->getSoulMax() - soul);
	} else {
		soul = std::max<int32_t>(0, soul + soulChange);
	}

	sendStats();
}

bool Player::canWear(uint32_t lookType, uint8_t addons) const
{
	if (group->access) {
		return true;
	}

	const Outfit* outfit = Outfits::getInstance().getOutfitByLookType(sex, lookType);
	if (!outfit) {
		return false;
	}

	if (outfit->premium && !isPremium()) {
		return false;
	}

	if (outfit->unlocked && addons == 0) {
		return true;
	}

	for (const OutfitEntry& outfitEntry : outfits) {
		if (outfitEntry.lookType == lookType) {
			if (outfitEntry.addons == addons || outfitEntry.addons == 3 || addons == 0) {
				return true;
			}
			return false; //have lookType on list and addons don't match
		}
	}
	return false;
}

bool Player::hasOutfit(uint32_t lookType, uint8_t addons)
{
	const Outfit* outfit = Outfits::getInstance().getOutfitByLookType(sex, lookType);
	if (!outfit) {
		return false;
	}

	if (outfit->unlocked && addons == 0) {
		return true;
	}

	for (const OutfitEntry& outfitEntry : outfits) {
		if (outfitEntry.lookType == lookType) {
			if (outfitEntry.addons == addons || outfitEntry.addons == 3 || addons == 0){
				return true;
			}
			return false; //have lookType on list and addons don't match
		}
	}
	return false;
}

void Player::genReservedStorageRange()
{
	//generate outfits range
	uint32_t base_key = PSTRG_OUTFITS_RANGE_START;
	for (const OutfitEntry& entry : outfits) {
		storageMap[++base_key] = (entry.lookType << 16) | entry.addons;
	}
}

void Player::addOutfit(uint16_t lookType, uint8_t addons)
{
	for (OutfitEntry& outfitEntry : outfits) {
		if (outfitEntry.lookType == lookType) {
			outfitEntry.addons |= addons;
			return;
		}
	}
	outfits.emplace_back(lookType, addons);
}

bool Player::removeOutfit(uint16_t lookType)
{
	for (auto it = outfits.begin(), end = outfits.end(); it != end; ++it) {
		OutfitEntry& entry = *it;
		if (entry.lookType == lookType) {
			outfits.erase(it);
			return true;
		}
	}
	return false;
}

bool Player::removeOutfitAddon(uint16_t lookType, uint8_t addons)
{
	for (OutfitEntry& outfitEntry : outfits) {
		if (outfitEntry.lookType == lookType) {
			outfitEntry.addons &= ~addons;
			return true;
		}
	}
	return false;
}

bool Player::getOutfitAddons(const Outfit& outfit, uint8_t& addons) const
{
	if (group->access) {
		addons = 3;
		return true;
	}

	if (outfit.premium && !isPremium()) {
		return false;
	}

	for (const OutfitEntry& outfitEntry : outfits) {
		if (outfitEntry.lookType != outfit.lookType) {
			continue;
		}

		addons = outfitEntry.addons;
		return true;
	}

	if (!outfit.unlocked) {
		return false;
	}

	addons = 0;
	return true;
}

bool Player::getRandomStep(Direction& direction) const
{
	static std::vector<Direction> dirList{
			DIRECTION_NORTH,
		DIRECTION_WEST, DIRECTION_EAST,
			DIRECTION_SOUTH
	};
	std::shuffle(dirList.begin(), dirList.end(), getRandomGenerator());

	for (Direction dir : dirList) {
		Tile* tile = g_game.map.getTile(getNextPosition(dir, getPosition()));
		if (tile && tile->queryAdd(INDEX_WHEREEVER, *this, 1, FLAG_IGNOREFIELDDAMAGE) == RETURNVALUE_NOERROR) {
			direction = dir;
			return true;
		}
	}
	return false;
}

void Player::setSex(PlayerSex_t newSex)
{
	sex = newSex;
}

Skulls_t Player::getSkull() const
{
	return skull;
}

Skulls_t Player::getSkullClient(const Creature* creature) const
{
	if (!creature || g_game.getWorldType() != WORLD_TYPE_PVP) {
		return SKULL_NONE;
	}

	const Player* player = creature->getPlayer();
	if (!player || player->getSkull() != SKULL_NONE) {
		return Creature::getSkullClient(creature);
	}

	if (player->hasAttacked(this) && getSkull() != SKULL_NONE) {
		return SKULL_YELLOW;
	}

	if (isPartner(player)) {
		return SKULL_GREEN;
	}

	return Creature::getSkullClient(creature);
}

bool Player::hasAttacked(const Player* attacked) const
{
	if (!attacked) {
		return false;
	}

	return attackedSet.find(attacked->id) != attackedSet.end();
}

void Player::addAttacked(const Player* attacked)
{
	if (!attacked || attacked == this) {
		return;
	}

	attackedSet.insert(attacked->id);
}

void Player::removeAttacked(const Player* attacked)
{
	if (!attacked || attacked == this) {
		return;
	}

	auto it = attackedSet.find(attacked->id);
	if (it != attackedSet.end()) {
		attackedSet.erase(it);
	}
}

void Player::clearAttacked()
{
	attackedSet.clear();
}

void Player::addUnjustifiedDead(const Player* attacked)
{
	if (attacked == this || g_game.getWorldType() == WORLD_TYPE_PVP_ENFORCED) {
		return;
	}

	if (lastUnjustCreatureId == attacked->getID()) {
		return;
	}

	lastUnjustCreatureId = attacked->getID();

	murderTimeStamps.push_back(std::time(nullptr));

	sendTextMessage(MESSAGE_STATUS_WARNING, "Warning! The murder of " + attacked->getName() + " was not justified.");

	if (playerKillerEnd == 0) {
		// white skull time, it only sets on first kill!
		playerKillerEnd = std::time(nullptr) + g_config.getNumber(ConfigManager::WHITE_SKULL_TIME);
	}

	PlayerKillingResult_t murderResult = checkPlayerKilling();
	if (murderResult >= PLAYER_KILLING_RED) {
		// red skull player
		playerKillerEnd = std::time(nullptr) + g_config.getNumber(ConfigManager::RED_SKULL_DURATION);
		setSkull(SKULL_RED);

		if (murderResult == PLAYER_KILLING_BANISHMENT) {
			g_game.addMagicEffect(getPosition(), CONST_ME_MAGIC_RED);
			g_databaseTasks.addTask(fmt::format("INSERT INTO `account_bans` (`account_id`, `reason`, `banned_at`, `expires_at`, `banned_by`) VALUES ({:d}, {:s}, {:d}, {:d}, {:d})",
				getAccount(),
				Database::getInstance().escapeString("Too many unjustified kills"),
				time(nullptr),
				time(nullptr) + (g_config.getNumber(ConfigManager::BAN_DAYS_LENGTH) * 86400),
				0));
			g_scheduler.addEvent(createSchedulerTask(1000, std::bind(&Game::kickPlayer, &g_game, getID(), false)));
		}
	}
}

PlayerKillingResult_t Player::checkPlayerKilling()
{
	time_t today = std::time(nullptr);
	int32_t lastDay = 0;
	int32_t lastWeek = 0;
	int32_t lastMonth = 0;
	uint64_t egibleMurders = 0;

	time_t dayTimestamp = today - (24 * 60 * 60);
	time_t weekTimestamp = today - (7 * 24 * 60 * 60);
	time_t monthTimestamp = today - (30 * 24 * 60 * 60);

	for (time_t currentMurderTimestamp : murderTimeStamps) {
		if (currentMurderTimestamp > dayTimestamp) {
			lastDay++;
		}

		if (currentMurderTimestamp > weekTimestamp) {
			lastWeek++;
		}

		egibleMurders = lastMonth + 1;

		if (currentMurderTimestamp <= monthTimestamp) {
			egibleMurders = lastMonth;
		}

		lastMonth = egibleMurders;
	}

	if (lastDay >= g_config.getNumber(ConfigManager::KILLS_DAY_BANISHMENT) ||
		lastWeek >= g_config.getNumber(ConfigManager::KILLS_WEEK_BANISHMENT) ||
		lastMonth >= g_config.getNumber(ConfigManager::KILLS_MONTH_BANISHMENT)) {
		return PLAYER_KILLING_BANISHMENT;
	}

	if (lastDay >= g_config.getNumber(ConfigManager::KILLS_DAY_RED_SKULL) ||
		lastWeek >= g_config.getNumber(ConfigManager::KILLS_WEEK_RED_SKULL) ||
		lastMonth >= g_config.getNumber(ConfigManager::KILLS_MONTH_RED_SKULL)) {
		return PLAYER_KILLING_RED; 
	}

	return PLAYER_KILLING_FRAG;
}

void Player::checkSkullTicks()
{
	time_t today = std::time(nullptr);

	if (!hasCondition(CONDITION_INFIGHT) && ((skull == SKULL_RED && today >= playerKillerEnd) || (skull == SKULL_WHITE))) {
		setSkull(SKULL_NONE);
		formerLogoutTime = OTSYS_TIME() + 5000;
	}
}

bool Player::isPromoted() const
{
	uint16_t promotedVocation = g_vocations.getPromotedVocation(vocation->getId());
	return promotedVocation == VOCATION_NONE && vocation->getId() != promotedVocation;
}

double Player::getLostPercent() const
{
	int32_t deathLosePercent = g_config.getNumber(ConfigManager::DEATH_LOSE_PERCENT);
	if (deathLosePercent != -1) {
		if (isPromoted()) {
			deathLosePercent -= 3;
		}

		deathLosePercent -= blessings.count();
		return std::max<int32_t>(0, deathLosePercent) / 100.;
	}

	double lossPercent;
	if (level >= 25) {
		double tmpLevel = level + (levelPercent / 100.);
		lossPercent = static_cast<double>((tmpLevel + 50) * 50 * ((tmpLevel * tmpLevel) - (5 * tmpLevel) + 8)) / experience;
	} else {
		lossPercent = 10;
	}

	double percentReduction = 0;
	if (isPromoted()) {
		percentReduction += 30;
	}
	percentReduction += blessings.count() * 8;
	return lossPercent * (1 - (percentReduction / 100.)) / 100.;
}

void Player::learnInstantSpell(const std::string& spellName)
{
	if (!hasLearnedInstantSpell(spellName)) {
		learnedInstantSpellList.push_front(spellName);
	}
}

void Player::forgetInstantSpell(const std::string& spellName)
{
	learnedInstantSpellList.remove(spellName);
}

bool Player::hasLearnedInstantSpell(const std::string& spellName) const
{
	if (hasFlag(PlayerFlag_CannotUseSpells)) {
		return false;
	}

 if (hasFlag(PlayerFlag_IgnoreSpellCheck) || g_config.getBoolean(ConfigManager::NO_SPELL_REQUIREMENTS)) {
		return true;
	}

	for (const auto& learnedSpellName : learnedInstantSpellList) {
		if (strcasecmp(learnedSpellName.c_str(), spellName.c_str()) == 0) {
			return true;
		}
	}
	return false;
}

bool Player::isInWar(const Player* player) const
{
	if (!player || !guild) {
		return false;
	}

	const Guild* playerGuild = player->getGuild();
	if (!playerGuild) {
		return false;
	}

	return isInWarList(playerGuild->getId()) && player->isInWarList(guild->getId());
}

bool Player::isInWarList(uint32_t guildId) const
{
	return std::find(guildWarVector.begin(), guildWarVector.end(), guildId) != guildWarVector.end();
}

bool Player::isPremium() const
{
	if (g_config.getBoolean(ConfigManager::FREE_PREMIUM) || hasFlag(PlayerFlag_IsAlwaysPremium)) {
		return true;
	}

	return premiumEndsAt > time(nullptr);
}

void Player::setPremiumTime(time_t premiumEndsAt)
{
	this->premiumEndsAt = premiumEndsAt;
}

PartyShields_t Player::getPartyShield(const Player* player) const
{
	if (!player) {
		return SHIELD_NONE;
	}

	if (party) {
		if (party->getLeader() == player) {
			if (party->isSharedExperienceActive())
			{
				if (party->isSharedExperienceEnabled())
					return ShieldYellowSharedExp;
				else
					return ShieldYellowNoSharedExpBlink;
			}
			//ShieldYellowSharedExp, // 6 // party leader sexp on
			//ShieldYellowNoSharedExpBlink, // 8 // party leader sexp inactive guilty
			//ShieldYellowNoSharedExp, // 10 party leader sexp inactive innocent
			return SHIELD_YELLOW;
		}

		if (player->party == party) {
			if (party->isSharedExperienceActive())
			{
				if (party->isSharedExperienceEnabled())
					return ShieldBlueSharedExp;
				else
					return ShieldBlueNoSharedExpBlink;
			}
			//ShieldBlueSharedExp, // 5 party member sexp on
			//ShieldBlueNoSharedExpBlink, // 7 party member sexp inactive guilty
			//ShieldBlueNoSharedExp, // 9 party member sexp inactive innocent
			return SHIELD_BLUE;
		}

		if (isInviting(player)) {
			return SHIELD_WHITEBLUE;
		}
	}

	if (player->isInviting(this)) {
		return SHIELD_WHITEYELLOW;
	}

	return SHIELD_NONE;
}

bool Player::isInviting(const Player* player) const
{
	if (!player || !party || party->getLeader() != this) {
		return false;
	}
	return party->isPlayerInvited(player);
}

bool Player::isPartner(const Player* player) const
{
	if (!player || !party) {
		return false;
	}
	return party == player->party;
}

bool Player::isGuildMate(const Player* player) const
{
	if (!player || !guild) {
		return false;
	}
	return guild == player->guild;
}

void Player::sendPlayerPartyIcons(Player* player)
{
	sendCreatureShield(player);
	sendCreatureSkull(player);
}

bool Player::addPartyInvitation(Party* party)
{
	auto it = std::find(invitePartyList.begin(), invitePartyList.end(), party);
	if (it != invitePartyList.end()) {
		return false;
	}

	invitePartyList.push_front(party);
	return true;
}

void Player::removePartyInvitation(Party* party)
{
	invitePartyList.remove(party);
}

void Player::clearPartyInvitations()
{
	for (Party* invitingParty : invitePartyList) {
		invitingParty->removeInvite(*this, false);
	}
	invitePartyList.clear();
}

void Player::sendClosePrivate(uint16_t channelId)
{
	if (channelId == CHANNEL_GUILD || channelId == CHANNEL_PARTY) {
		g_chat->removeUserFromChannel(*this, channelId);
	}

	if (client) {
		client->sendClosePrivate(channelId);
	}
}

bool Player::addWing(uint8_t wingId)
{
    if (!g_game.wings.getWingByID(wingId)) {
        return false;
    }

    const uint8_t tmpWingId = wingId - 1;
    const uint32_t key = PSTRG_WINGS_RANGE_START + (tmpWingId / 31);

    int32_t value;
    if (getStorageValue(key, value)) {
        value |= (1 << (tmpWingId % 31));
    }
    else {
        value = (1 << (tmpWingId % 31));
    }

    addStorageValue(key, value);
    return true;
}

bool Player::hasWing(const Wing* wing) const
{
	if (isAccessPlayer()) {
		return true;
	}

	if (wing->premium && !isPremium()) {
		return false;
	}

	const uint8_t tmpWingId = wing->id - 1;

	int32_t value;
	if (!getStorageValue(PSTRG_WINGS_RANGE_START + (tmpWingId / 31), value)) {
		return false;
	}

	return ((1 << (tmpWingId % 31)) & value) != 0;
}

uint8_t Player::getCurrentWing() const
{
	int32_t value;
	if (getStorageValue(PSTRG_WINGS_CURRENTWINGS, value)) {
		return value;
	}
	return 0;
}

void Player::setCurrentWing(uint8_t wingId)
{
	addStorageValue(PSTRG_WINGS_CURRENTWINGS, wingId);
}

bool Player::addAura(uint8_t auraId)
{
    if (!g_game.auras.getAuraByID(auraId)) {
        return false;
    }

    const uint8_t tmpAuraId = auraId - 1;
    const uint32_t key = PSTRG_AURAS_RANGE_START + (tmpAuraId / 31);

    int32_t value;
    if (getStorageValue(key, value)) {
        value |= (1 << (tmpAuraId % 31));
    }
    else {
        value = (1 << (tmpAuraId % 31));
    }

    addStorageValue(key, value);
    return true;
}

bool Player::hasAura(const Aura* aura) const
{
	if (isAccessPlayer()) {
		return true;
	}

	if (aura->premium && !isPremium()) {
		return false;
	}

	const uint8_t tmpAuraId = aura->id - 1;

	int32_t value;
	if (!getStorageValue(PSTRG_AURAS_RANGE_START + (tmpAuraId / 31), value)) {
		return false;
	}

	return ((1 << (tmpAuraId % 31)) & value) != 0;
}

uint8_t Player::getCurrentAura() const
{
	int32_t value;
	if (getStorageValue(PSTRG_AURAS_CURRENTAURA, value)) {
		return value;
	}
	return 0;
}

void Player::setCurrentAura(uint8_t auraId)
{
	addStorageValue(PSTRG_AURAS_CURRENTAURA, auraId);
}

bool Player::addShader(uint8_t shaderId)
{
    if (!g_game.shaders.getShaderByID(shaderId)) {
        return false;
    }

    const uint8_t tmpShaderId = shaderId - 1;
    const uint32_t key = PSTRG_SHADERS_RANGE_START + (tmpShaderId / 31);

    int32_t value;
    if (getStorageValue(key, value)) {
        value |= (1 << (tmpShaderId % 31));
    }
    else {
        value = (1 << (tmpShaderId % 31));
    }

    addStorageValue(key, value);
    return true;
}

bool Player::hasShader(const Shader* shader) const
{
    if (!shader) {
        return false;
    }

    // Admin libera tudo
    if (isAccessPlayer()) {
        return true;
    }

    // Se shader é premium e player não é premium, bloqueia
    if (shader->premium && !isPremium()) {
        return false;
    }

    // Storage que controla liberação do shader específico
    constexpr int32_t STORAGE_BASE_SHADER = 60000;
    int32_t storageKey = STORAGE_BASE_SHADER + shader->id;

    int32_t value;
    if (!getStorageValue(storageKey, value)) {
        return false; // storage não existe = shader bloqueado
    }

    return value >= 1; // liberado se storage >= 1
}

uint64_t Player::getMoney() const
{
	std::vector<const Container*> containers;
	uint64_t moneyCount = 0;

	for (int32_t i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; ++i) {
		Item* item = inventory[i];
		if (!item) {
			continue;
		}

		const Container* container = item->getContainer();
		if (container) {
			containers.push_back(container);
		} else {
			moneyCount += item->getWorth();
		}
	}

	size_t i = 0;
	while (i < containers.size()) {
		const Container* container = containers[i++];
		for (const Item* item : container->getItemList()) {
			const Container* tmpContainer = item->getContainer();
			if (tmpContainer) {
				containers.push_back(tmpContainer);
			} else {
				moneyCount += item->getWorth();
			}
		}
	}
	return moneyCount;
}

size_t Player::getMaxVIPEntries() const
{
	if (group->maxVipEntries != 0) {
		return group->maxVipEntries;
	}

	return g_config.getNumber(isPremium() ? ConfigManager::VIP_PREMIUM_LIMIT : ConfigManager::VIP_FREE_LIMIT);
}

size_t Player::getMaxDepotItems() const
{
	if (group->maxDepotItems != 0) {
		return group->maxDepotItems;
	}

	return g_config.getNumber(isPremium() ? ConfigManager::DEPOT_PREMIUM_LIMIT : ConfigManager::DEPOT_FREE_LIMIT);
}

std::forward_list<Condition*> Player::getMuteConditions() const
{
	std::forward_list<Condition*> muteConditions;
	for (Condition* condition : conditions) {
		if (condition->getTicks() <= 0) {
			continue;
		}

		ConditionType_t type = condition->getType();
		if (type != CONDITION_MUTED && type != CONDITION_CHANNELMUTEDTICKS && type != CONDITION_YELLTICKS) {
			continue;
		}

		muteConditions.push_front(condition);
	}
	return muteConditions;
}

void Player::setGuild(Guild* guild)
{
	if (guild == this->guild) {
		return;
	}

	Guild* oldGuild = this->guild;

	this->guildNick.clear();
	this->guild = nullptr;
	this->guildRank = nullptr;

	if (guild) {
		GuildRank_ptr rank = guild->getRankByLevel(1);
		if (!rank) {
			return;
		}

		this->guild = guild;
		this->guildRank = rank;
		guild->addMember(this);
	}

	if (oldGuild) {
		oldGuild->removeMember(this);
	}
}

void Player::updateRegeneration()
{
	if (!vocation) {
		return;
	}

	uint32_t health_amount = vocation->getHealthGainAmount();
	uint32_t health_ticks = vocation->getHealthGainTicks() * 1000;
	uint32_t mana_amount = vocation->getManaGainAmount();
	uint32_t mana_ticks = vocation->getManaGainTicks() * 1000;
	
	Condition* condition = getCondition(CONDITION_REGENERATION, CONDITIONID_DEFAULT);
	if (condition) {
		//INFO: A lógica implica em +20% da *velocidade* de regeneração para cada tier (somados).
		condition->setParam(CONDITION_PARAM_HEALTHGAIN, health_amount);
		condition->setParam(CONDITION_PARAM_HEALTHTICKS, health_ticks);

		condition->setParam(CONDITION_PARAM_MANAGAIN, mana_amount);
		condition->setParam(CONDITION_PARAM_MANATICKS, mana_ticks);

	}

	Condition* conditionAttr = getCondition(CONDITION_REGENERATION, CONDITIONID_ITEM_ATTR);

	if (conditionAttr)
	{
		uint32_t healthRegen = getCustomItemAttrStat(ITEM_RND_HEALTH_REGEN);
		uint32_t manaRegen = getCustomItemAttrStat(ITEM_RND_MANA_REGEN);

		if (healthRegen > 0) {
			conditionAttr->setParam(CONDITION_PARAM_HEALTHGAIN, health_amount);
			conditionAttr->setParam(CONDITION_PARAM_HEALTHTICKS, 5 * health_ticks / healthRegen);
		}

		if (manaRegen > 0) {
			conditionAttr->setParam(CONDITION_PARAM_MANAGAIN, mana_amount);
			conditionAttr->setParam(CONDITION_PARAM_MANATICKS, 5 * mana_ticks / manaRegen);
		}
	}
}

void Player::updateBattlepass(BattlePassQuests_t id, const std::string& value)
{
	updateBattlepassCounter(id, value);
}

void Player::updateBattlepass(BattlePassQuests_t id, uint16_t value)
{
	updateBattlepassCounter(id, value);
}

void Player::updateBattlepass(BattlePassQuests_t id)
{
	updateBattlepassCounter(id, {});
}

void Player::updateBattlepassCounter(BattlePassQuests_t id, std::any value)
{
	if (!g_game.isBattlepassActive()) {
		return;
	}

	BattlePassQuestsVector quests;
	if (battlepass->update(id, value, &quests)) {
		// Send to client
		if (openBattlepass && client) {
			// Send the battlepass quest progress to the game client
			// client->sendUpdateBattlepassQuest(quests);
		}
	}
}

void Player::sendBattlepassQuests(bool sendLevels)
{
	if (!client) {
		return;
	}

	openBattlepass = true;
	battlepass->checkQuestsCooldown(level);
	client->sendBattlepassQuests(battlepass->getExperience(), battlepass->getLevel(), battlepass->getBattlepass(), BattlePasses::getInstance()->getRewards(), battlepass->hasPremium(), sendLevels);
}

void Player::addBattlepassQuest(BattlePassType_t type, BattlePassQuests_t id, uint32_t amount, time_t cooldown, const std::any& value, bool shuffled)
{
	battlepass->addQuest(type, id, amount, cooldown, value, shuffled, nullptr);
}

void Player::resetBattlepass()
{
	// Reset the battlepass data
	battlepass->reset(level);
}

void Player::buyPremiumBattlepass()
{
    if (!openBattlepass || !client) {
        return;
    }

    if (battlepass->hasPremium()) {
        // Já possui premium battlepass
        client->sendBattlepassPremium(0, true);
        return;
    }

    uint32_t accountId = getAccount();
    Database& db = Database::getInstance();

    int32_t shopCoins = -1;
    {
        std::ostringstream query;
        query << "SELECT shop_coins FROM web_accounts WHERE account_id = " << accountId;
        DBResult_ptr result = db.storeQuery(query.str());
        if (result) {
            shopCoins = result->getNumber<int32_t>("shop_coins");
        }
    }

    if (shopCoins < BATTLEPASS_PREMIUM_COST) {
        client->sendBattlepassPremium(1, false);
        return;
    }

    std::ostringstream updateQuery;
    updateQuery << "UPDATE web_accounts SET shop_coins = shop_coins - " << BATTLEPASS_PREMIUM_COST
                << " WHERE account_id = " << accountId << " AND shop_coins >= " << BATTLEPASS_PREMIUM_COST;
				
    if (!db.executeQuery(updateQuery.str())) {
        client->sendBattlepassPremium(1, false);
        return;
    }

    std::ostringstream insertQuery;
    insertQuery << "INSERT INTO store_history (account_id, mode, description, coin_amount, time) VALUES ("
                << accountId << ", "
                << "1" << ", "
                << "'Purchase Premium Battlepass', "
                << "-" << BATTLEPASS_PREMIUM_COST << ", "
                << std::time(nullptr) << ")";

    db.executeQuery(insertQuery.str());

    battlepass->setPremium(true);
    client->sendBattlepassPremium(2, true);
}


void Player::completeBattlepassQuest(uint8_t id, uint16_t questId)
{
	switch (id) {
	case 2: {
		// "Complete" button pressed
		time_t cooldown;
		if (battlepass->completeQuest(getPlayer(), questId, &cooldown)) {
			// Quest completed successfully, send the info back to game client
			if (client && openBattlepass) {
				client->sendUpdateBattlepassQuest(questId, battlepass->getExperience(), battlepass->getLevel(), cooldown);
			}
		}
		break;
	}
	case 3:
	{
		uint32_t accountId = getAccount();
		Database& db = Database::getInstance();

		int32_t shopCoins = -1;
		{
			std::ostringstream query;
			query << "SELECT shop_coins FROM web_accounts WHERE account_id = " << accountId;

			DBResult_ptr result = db.storeQuery(query.str());
			if (result) {
				shopCoins = result->getNumber<int32_t>("shop_coins");
			}
		}

		if (shopCoins < BATTLEPASS_SHUFFLE_COST) {
			client->sendBattlepassPremium(4, battlepass->hasPremium());
			break;  
		}

		BattlePassPlayerData data;
		if (battlepass->shuffleQuest(level, questId, data)) {

			std::ostringstream updateQuery;
			updateQuery << "UPDATE web_accounts SET shop_coins = shop_coins - " << BATTLEPASS_SHUFFLE_COST
						<< " WHERE account_id = " << accountId << " AND shop_coins >= " << BATTLEPASS_SHUFFLE_COST;

			db.executeQuery(updateQuery.str());

			// Inserir no store_history
			std::ostringstream insertQuery;
			insertQuery << "INSERT INTO store_history (account_id, mode, description, coin_amount, time) VALUES ("
						<< accountId << ", "
						<< "1" << ", "
						<< "'Battlepass Shuffle Quest', "
						<< "-" << BATTLEPASS_SHUFFLE_COST << ", "
						<< std::time(nullptr) << ")";

			db.executeQuery(insertQuery.str());

			if (client && openBattlepass) {
				client->sendBattlepassQuest(questId, data);
				client->sendBattlepassPremium(3, battlepass->hasPremium());
			}
		}

		break;
	}
	default:
		break;
	}
}

Item* Player::getItemByUID(uint32_t uid) const {
	if (uid == 0) {
		return nullptr;
	}

	std::vector<Item*> itemList;

	for (int32_t i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; i++) {
		Item* item = inventory[i];
		if (!item) {
			continue;
		}

		if (item->getRealUID() == uid) {
			return item;
		}
		else if (Container* container = item->getContainer()) {
			for (ContainerIterator it = container->iterator(); it.hasNext(); it.advance()) {
				Item* containerItem = *it;
				if (containerItem->getRealUID() == uid) {
					return containerItem;
				}
			}
		}
	}
	return nullptr;
}

bool Player::isLightExchangeable() const {

	return true;
}

bool Player::addBestiaryKill(uint16_t id)
{
	const uint32_t key = 9015500 + id;
	int32_t value = getBestiaryKill(id);

	value += g_config.getNumber(ConfigManager::BESTIARY_KILL_COUNT);
	addStorageValue(key, value);
	return true;
}

int32_t Player::getBestiaryKill(uint16_t id)
{
	const uint32_t key = 9015500 + id;
	int32_t value;

	if (getStorageValue(key, value)) {
		return value;
	}

	return 0;
}

BestiaryStage_t Player::getBestiaryStage(MonsterType* mType)
{
	uint16_t id = mType->info.bestiary.id;

	if (id != 0) {
		int32_t kills = getBestiaryKill(id);

		uint16_t prowess = mType->info.bestiary.prowess;
		uint16_t expertise = mType->info.bestiary.expertise;
		uint16_t mastery = mType->info.bestiary.mastery;

		if (kills >= prowess && kills < expertise) {
			return BESTIARY_STAGE_PROWESS;
		}
		else if (kills >= expertise && kills < mastery) {
			return BESTIARY_STAGE_EXPERTISE;
		}
		else if (kills >= mastery) {
			return BESTIARY_STAGE_MASTERY;
		}
	}

	return BESTIARY_STAGE_NONE;
}

bool Player::canTradeItem(uint32_t storageCooldown) {
    int32_t lastTradeTime = -1;
    getStorageValue(storageCooldown, lastTradeTime);

    uint64_t currentTime = OTSYS_TIME();

    if (lastTradeTime == -1) {
        return true; // Nunca trocou antes, pode trocar
    }

    uint64_t cooldownTime = 20 * 60 * 60 * 1000;
    return (currentTime >= static_cast<uint64_t>(lastTradeTime) + cooldownTime);
}

// Atualiza o tempo da última troca no storage
void Player::updateTradeCooldown(uint32_t storageCooldown) {
    addStorageValue(storageCooldown, static_cast<int32_t>(OTSYS_TIME()));
}