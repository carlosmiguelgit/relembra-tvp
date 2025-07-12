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

#ifndef FS_MONSTER_H_9F5EEFE64314418CA7DA41D1B9409DD0
#define FS_MONSTER_H_9F5EEFE64314418CA7DA41D1B9409DD0

#include "tile.h"
#include "monsters.h"

class Creature;
class Game;
class BaseSpawn;

using CreatureHashSet = std::unordered_set<Creature*>;
using CreatureList = std::list<Creature*>;

enum TargetSearchType_t {
	TARGETSEARCH_NONE, 
	TARGETSEARCH_RANDOM,
	TARGETSEARCH_NEAREST,
	TARGETSEARCH_WEAKEST,
	TARGETSEARCH_MOSTDAMAGE,
};

class Monster final : public Creature
{
	public:
		static Monster* createMonster(const std::string& name, const std::vector<LootBlock>* extraLoot = nullptr, bool isSpawn = false, const Position& pos = {});

		explicit Monster(MonsterType* mType, const std::vector<LootBlock>* extraLoot = nullptr, bool isSpawn = false, const Position& pos = {});
		~Monster();

		// non-copyable
		Monster(const Monster&) = delete;
		Monster& operator=(const Monster&) = delete;

		Monster* getMonster() override {
			return this;
		}
		const Monster* getMonster() const override {
			return this;
		}

		void setID() override {
			if (id == 0) {
				id = monsterAutoID++;
			}
		}

		void addList() override;
		void removeList() override;

		const std::string& getName() const override;
		void setName(const std::string& newName);

		const std::string& getNameDescription() const override;
		void setNameDescription(const std::string& newNameDescription) {
			nameDescription = newNameDescription;
		}

		const std::string& getMarketDescription() const override {return marketDescription;}
		void setMarketDescription(const std::string& description);

		std::string getDescription(int32_t) const override {
			return nameDescription + '.';
		}

		CreatureType_t getType() const override {
			return CREATURETYPE_MONSTER;
		}

		const Position& getMasterPos() const {
			return masterPos;
		}
		void setMasterPos(Position pos) {
			masterPos = pos;
		}

		void setSpawnInterval(uint32_t interval) {
			spawnInterval = interval;
		}

		void setLifeTimeExpiration(uint64_t lifetime) { 
			lifeTimeExpiration = lifetime;
		}

		RaceType_t getRace() const override {
			return mType->info.race;
		}
		int32_t getArmor() const override;
		int32_t getDefense() const override;
		bool isPushable() const override {
			return mType->info.pushable && baseSpeed != 0 && !isElite();
		}

		bool isAttackable() const override {
			return mType->info.isAttackable;
		}

		bool isBoss() const override {
			return mType->info.isBoss;
		}
		MonsterClass_t getMonsterClass() const override {
			return mType->info.monsterClass;
		}

		bool hasBestiary() const {
			return mType->info.bestiary.id != 0;
		}

		bool canPushItems() const;
		bool canPushCreatures() const {
			return mType->info.canPushCreatures && !isSummon() || isElite();
		}
		bool isHostile() const {
			return mType->info.isHostile;
		}
		bool canSee(const Position& pos) const override;
		bool canSeeInvisibility() const override {
			return isImmune(CONDITION_INVISIBLE);
		}
		uint32_t getManaCost() const {
			return mType->info.manaCost;
		}
		void setSpawn(BaseSpawn* newSpawn) {
			spawn = newSpawn;
		}
		bool canWalkOnFieldType(CombatType_t combatType) const;

		void onCreatureAppear(Creature* creature, bool isLogin) override;
		void onRemoveCreature(Creature* creature, bool isLogout) override;
		void onCreatureMove(Creature* creature, const Tile* newTile, const Position& newPos, const Tile* oldTile, const Position& oldPos, bool teleport) override;
		void onCreatureSay(Creature* creature, SpeakClasses type, const std::string& text) override;

		void drainHealth(Creature* attacker, int32_t damage) override;
		void changeHealth(int32_t healthChange, bool sendHealthChange = true) override;

		LightInfo getCreatureLight() const override;

		void onIdleStimulus() override;
		void onThink(uint32_t interval) override;

		bool challengeCreature(Creature* creature, bool force = false) override;

		bool getCombatValues(int32_t& min, int32_t& max) override;

		void doAttackSpells();
		void doDefensiveSpells();

		void doAttacking(bool skipDelay = false) override;

		bool selectTarget(Creature* creature);

		const CreatureList& getTargetList() const {
			return targetList;
		}

		bool isElite() const {
			return getStar() > STAR_NONE;
		}

		bool isTarget(const Creature* creature) const;
		bool isFleeing() const {
			return !isElite() && !isSummon() && getHealth() <= mType->info.runAwayHealth;
		}

		bool getRandomStep(const Position& creaturePos, Direction& resultDir) const;
		bool getFlightStep(const Position& targetPos, Direction& resultDir) const;
		bool isIgnoringFieldDamage() const {
			return isAttackPanicking;
		}

		bool isPathBlockingChecking() const {
			return pathBlockCheck;
		}

		bool isOpponent(const Creature* creature) const;
		bool isCreatureAvoidable(const Creature* creature) const;

		BlockType_t blockHit(Creature* attacker, CombatType_t combatType, int32_t& damage,
		                      bool checkDefense = false, bool checkArmor = false, bool field = false, bool ignoreResistances = false, bool meleeHit = false) override;

		bool isInSpawnRange(const Position& pos) const;

		static bool pushItem(const Position& fromPos, Item* item);
		static void pushItems(const Position& fromPos, Tile* fromTile);
		static bool pushCreature(const Position& fromPos, Creature* creature);
		static void pushCreatures(const Position& fromPos, Tile* fromTile);

		Star_t getStar() const override {
			return star;
		}
		
		Star_t getStarAppearance() const override {

			if (mType->info.isBoss || isSummon())
			{
				if (star == STAR_ONE)
					return STAR_ONE_BOSS;
				
				if (star == STAR_TWO)
					return STAR_TWO_BOSS;

				if (star == STAR_THREE)
					return STAR_THREE_BOSS;
			}

			return star;
		}

		void setStar(const Star_t value) {
			star = value;

			if (star > STAR_NONE)
			{
				// Build monster stars immunities
				// Elite monsters are imune to invisible and elements
				elite_conditionImmunities |= CONDITION_INVISIBLE;
			}
		}

		static uint32_t monsterAutoID;
	private:
		CreatureList targetList;

		std::string name;
		std::string nameDescription;

		std::string marketDescription;

		MonsterType* mType;
		BaseSpawn* spawn = nullptr;
		BaseSpawn* originalSpawn = nullptr;
		Star_t star = STAR_NONE;

		uint64_t lifeTimeExpiration = 0;
		int64_t earliestMeleeAttack = 0;
		int32_t minCombatValue = 0;
		int32_t maxCombatValue = 0;
		uint32_t spawnInterval = 0;
		uint32_t currentSkill = 0;
		uint32_t skillCurrentExp = 0;
		uint32_t skillFactorPercent = 1000;
		uint32_t skillNextLevel = 0;
		uint32_t skillLearningPoints = 30;
		uint8_t panicToggleCount = 0;
		uint32_t accountBound = 0;
		LightInfo internalLight{};

		uint32_t elite_damageImmunities = 0;
		uint32_t elite_conditionImmunities = 0;

		Position masterPos;

		bool isIdle = true;
		bool isEscaping = false;
		bool isAttackPanicking = false;
		bool isReachingTarget = false;
		bool pathBlockCheck = false;

		std::array<Item*, CONST_SLOT_LAST + 1> inventory{};

		bool isAccountBound() const {
			return accountBound > 0;
		}

		uint32_t getAccountBound() const {
			return accountBound;
		}

		void setAccountBound(uint32_t aid)
		{
			accountBound = aid;
		}

		void addMonsterItemInventory(Container* bagItem, Item* item);

		void onCreatureEnter(Creature* creature);
		void onCreatureLeave(Creature* creature);
		void onCreatureFound(Creature* creature, bool pushFront = false);

		void updateLookDirection();

		void addTarget(Creature* creature, bool pushFront = false);
		void removeTarget(Creature* creature);

		void updateTargetList();
		void clearTargetList();

		void addSkillPoint();

		void death(Creature* lastHitCreature) override;
		Item* getCorpse(Creature* lastHitCreature, Creature* mostDamageCreature) override;

		void setIdle(bool idle);
		void updateIdleStatus();
		bool getIdleStatus() const {
			return isIdle;
		}

		void onAddCondition(ConditionType_t type) override;
		void onEndCondition(ConditionType_t type) override;

        void onAttackedCreature(Creature* creature, bool addInFightTicks = true) override;
		void onAttackedCreatureBlockHit(BlockType_t blockType, bool meleeHit = false) override;
		
		bool canWalkTo(Position pos, Direction dir) const;

		uint64_t getLostExperience() const override {
			return skillLoss ? mType->info.experience : 0;
		}
		uint16_t getLookCorpse() const override {
			return mType->info.lookcorpse;
		}
		void dropLoot(Container* corpse, Creature* lastHitCreature) override;
		uint32_t getDamageImmunities() const override {
			return mType->info.damageImmunities | elite_damageImmunities;
		}
		uint32_t getConditionImmunities() const override {
			return mType->info.conditionImmunities | elite_conditionImmunities;
		}

		friend class LuaScriptInterface;
		friend class Creature;
		friend class Game;
		friend class Tile;
};

#endif