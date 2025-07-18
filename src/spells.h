/**
 * The Violet Project - a free and open-source MMORPG server emulator
 * Copyright (C) 2021 - Ezzz <alejandromujica.rsm@gmail.com>
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

#ifndef FS_SPELLS_H_D78A7CCB7080406E8CAA6B1D31D3DA71
#define FS_SPELLS_H_D78A7CCB7080406E8CAA6B1D31D3DA71

#include "luascript.h"
#include "player.h"
#include "actions.h"
#include "talkaction.h"
#include "baseevents.h"

class InstantSpell;
class RuneSpell;
class Spell;

using VocSpellMap = std::map<uint16_t, bool>;
using InstantSpell_ptr = std::unique_ptr<InstantSpell>;
using RuneSpell_ptr = std::unique_ptr<RuneSpell>;

class Spells final : public BaseEvents
{
	public:
		Spells();
		~Spells();

		// non-copyable
		Spells(const Spells&) = delete;
		Spells& operator=(const Spells&) = delete;

		Spell* getSpellByName(const std::string& name);
		RuneSpell* getRuneSpell(uint32_t id);
		RuneSpell* getRuneSpellByName(const std::string& name);

		InstantSpell* getInstantSpell(const std::string& words);
		InstantSpell* getInstantSpellByName(const std::string& name);

		TalkActionResult_t playerSaySpell(Player* player, std::string& words);

		static Position getCasterPosition(Creature* creature, Direction dir);
		std::string getScriptBaseName() const override;

		const std::map<std::string, InstantSpell>& getInstantSpells() const {
			return instants;
		};

		void clearMaps(bool fromLua);
		void clear(bool fromLua) override final;
		bool registerInstantLuaEvent(InstantSpell* event);
		bool registerRuneLuaEvent(RuneSpell* event);

	private:
		LuaScriptInterface& getScriptInterface() override;
		Event_ptr getEvent(const std::string& nodeName) override;
		bool registerEvent(Event_ptr event, const pugi::xml_node& node) override;

		std::map<uint16_t, RuneSpell> runes;
		std::map<std::string, InstantSpell> instants;

		friend class CombatSpell;
		LuaScriptInterface scriptInterface { "Spell Interface" };
};

class BaseSpell
{
	public:
		constexpr BaseSpell() = default;
		virtual ~BaseSpell() = default;

		virtual bool castSpell(Creature* creature) = 0;
		virtual bool castSpell(Creature* creature, Creature* target) = 0;
};

class CombatSpell final : public Event, public BaseSpell
{
	public:
		CombatSpell(Combat_ptr combat, bool needTarget, bool needDirection);

		// non-copyable
		CombatSpell(const CombatSpell&) = delete;
		CombatSpell& operator=(const CombatSpell&) = delete;

		bool castSpell(Creature* creature) override;
		bool castSpell(Creature* creature, Creature* target) override;
		bool configureEvent(const pugi::xml_node&) override {
			return true;
		}

		//scripting
		bool executeCastSpell(Creature* creature, const LuaVariant& var);

		bool loadScriptCombat();
		Combat_ptr getCombat() {
			return combat;
		}

	private:
		std::string getScriptEventName() const override {
			return "onCastSpell";
		}

		Combat_ptr combat;

		bool needDirection;
		bool needTarget;
};

class Spell : public BaseSpell
{
	public:
		Spell() = default;

		bool configureSpell(const pugi::xml_node& node);
		const std::string& getName() const {
			return name;
		}
		void setName(std::string n) {
			name = n;
		}
		uint8_t getId() const {
			return spellId;
		}
		void setId(uint8_t id) {
			spellId = id;
		}

		void postCastSpell(Player* player, bool finishedCast = true, bool payCost = true) const;
		static void postCastSpell(Player* player, uint32_t manaCost, uint32_t soulCost);

		uint32_t getManaCost(const Player* player) const;
		uint32_t getSoulCost() const {
			return soul;
		}
		void setSoulCost(uint32_t s) {
			soul = s;
		}
		uint32_t getLevel() const {
			return level;
		}
		void setLevel(uint32_t lvl) {
			level = lvl;
		}
		uint32_t getMagicLevel() const {
			return magLevel;
		}
		void setMagicLevel(uint32_t lvl) {
			magLevel = lvl;
		}
		uint32_t getMana() const {
			return mana;
		}
		void setMana(uint32_t m) {
			mana = m;
		}
		uint32_t getManaPercent() const {
			return manaPercent;
		}
		void setManaPercent(uint32_t m) {
			manaPercent = m;
		}
		bool isPremium() const {
			return premium;
		}
		void setPremium(bool p) {
			premium = p;
		}
		bool isEnabled() const {
			return enabled;
		}
		void setEnabled(bool e) {
			enabled = e;
		}

		virtual bool isInstant() const = 0;
		bool isLearnable() const {
			return learnable;
		}
		void setLearnable(bool l) {
			learnable = l;
		}

		const VocSpellMap& getVocMap() const {
			return vocSpellMap;
		}
		void addVocMap(uint16_t n, bool b) {
			vocSpellMap[n] = b;
		}

		int32_t getRange() const {
			return range;
		}
		void setRange(int32_t r) {
			range = r;
		}

		uint32_t getCooldown() const {
			return cooldown;
		}
		void setCooldown(uint32_t a) {
			cooldown = a;
		}

		bool getNeedTarget() const {
			return needTarget;
		}
		void setNeedTarget(bool n) {
			needTarget = n;
		}
		bool getNeedWeapon() const {
			return needWeapon;
		}
		void setNeedWeapon(bool n) {
			needWeapon = n;
		}
		bool getNeedLearn() const {
			return learnable;
		}
		void setNeedLearn(bool n) {
			learnable = n;
		}
		bool getSelfTarget() const {
			return selfTarget;
		}
		void setSelfTarget(bool s) {
			selfTarget = s;
		}
		bool getBlockingSolid() const {
			return blockingSolid;
		}
		void setBlockingSolid(bool b) {
			blockingSolid = b;
		}
		bool getBlockingCreature() const {
			return blockingCreature;
		}
		void setBlockingCreature(bool b) {
			blockingCreature = b;
		}
		bool getAggressive() const {
			return aggressive;
		}
		void setAggressive(bool a) {
			aggressive = a;
		}
		bool getPzLock() const {
			return pzLock;
		}
		void setPzLock(bool pzLock) {
			this->pzLock = pzLock;
		}
		bool getCooldownSpellTime() const {
			return cooldownSpellTime;
		}
		void setCooldownSpellTime(bool a) {
			this->cooldownSpellTime = a;
		}

		const SpellCategory_t& getCategory() const {
			return spellCategory;
		}
		void setCategory(SpellCategory_t c) {
			spellCategory = c;
		}

		SpellType_t spellType = SPELL_UNDEFINED;
		SpellCategory_t spellCategory = SPELL_CATEGORY_UNDEFINED;

	protected:
		bool playerSpellCheck(Player* player) const;
		bool playerInstantSpellCheck(Player* player, const Position& toPos);
		bool playerRuneSpellCheck(Player* player, const Position& toPos);

		VocSpellMap vocSpellMap;

		uint32_t cooldown = 2000;
		uint32_t level = 0;
		uint32_t magLevel = 0;
		int32_t range = -1;

		uint8_t spellId = 0;

		bool selfTarget = false;
		bool needTarget = false;

	private:
		uint32_t mana = 0;
		uint32_t manaPercent = 0;
		uint32_t soul = 0;

		bool needWeapon = false;
		bool blockingSolid = false;
		bool blockingCreature = false;
		bool aggressive = true;
		bool pzLock = false;
		bool learnable = true;
		bool enabled = true;
		bool premium = false;
		bool cooldownSpellTime = true;

		std::string name;
};

class InstantSpell final : public TalkAction, public Spell
{
	public:
		explicit InstantSpell(LuaScriptInterface* interface) : TalkAction(interface) {}

		bool configureEvent(const pugi::xml_node& node) override;

		virtual bool playerCastInstant(Player* player, std::string& param);

		bool castSpell(Creature* creature) override;
		bool castSpell(Creature* creature, Creature* target) override;

		//scripting
		bool executeCastSpell(Creature* creature, const LuaVariant& var);

		bool isInstant() const override {
			return true;
		}
		bool getHasParam() const {
			return hasParam;
		}
		void setHasParam(bool p) {
			hasParam = p;
		}
		bool getHasPlayerNameParam() const {
			return hasPlayerNameParam;
		}
		void setHasPlayerNameParam(bool p) {
			hasPlayerNameParam = p;
		}
		bool getNeedDirection() const {
			return needDirection;
		}
		void setNeedDirection(bool n) {
			needDirection = n;
		}
		bool getNeedCasterTargetOrDirection() const {
			return casterTargetOrDirection;
		}
		void setNeedCasterTargetOrDirection(bool d) {
			casterTargetOrDirection = d;
		}
		bool getBlockWalls() const {
			return checkLineOfSight;
		}
		void setBlockWalls(bool w) {
			checkLineOfSight = w;
		}
		bool canCast(const Player* player) const;
		bool canThrowSpell(const Creature* creature, const Creature* target) const;

	private:
		std::string getScriptEventName() const override;

		bool internalCastSpell(Creature* creature, const LuaVariant& var);

		bool needDirection = false;
		bool hasParam = false;
		bool hasPlayerNameParam = false;
		bool checkLineOfSight = true;
		bool casterTargetOrDirection = false;
};

class RuneSpell final : public Action, public Spell
{
	public:
		explicit RuneSpell(LuaScriptInterface* interface) : Action(interface) {}

		bool configureEvent(const pugi::xml_node& node) override;

		ReturnValue canExecuteAction(const Player* player, const Position& toPos) override;
		bool hasOwnErrorHandler() override {
			return true;
		}
		Thing* getTarget(Player*, Creature* targetCreature, const Position&, uint8_t, uint16_t) const override {
			return targetCreature;
		}

		bool executeUse(Player* player, Item* item, const Position& fromPosition, Thing* target, const Position& toPosition) override;

		bool castSpell(Creature* creature) override;
		bool castSpell(Creature* creature, Creature* target) override;

		//scripting
		bool executeCastSpell(Creature* creature, const LuaVariant& var);

		bool isInstant() const override {
			return false;
		}
		uint16_t getRuneItemId() const {
			return runeId;
		}
		void setRuneItemId(uint16_t i) {
			runeId = i;
		}
		uint32_t getCharges() const {
			return charges;
		}
		void setCharges(uint32_t c) {
			if (c > 0) {
				hasCharges = true;
			}
			charges = c;
		}

	private:
		std::string getScriptEventName() const override;

		bool internalCastSpell(Creature* creature, const LuaVariant& var);

		uint16_t runeId = 0;
		uint32_t charges = 0;
		bool hasCharges = false;
};

#endif
