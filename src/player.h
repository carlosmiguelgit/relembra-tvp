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

#ifndef FS_PLAYER_H_4083D3D3A05B4EDE891B31BB720CD06F
#define FS_PLAYER_H_4083D3D3A05B4EDE891B31BB720CD06F

#include "creature.h"
#include "container.h"
#include "cylinder.h"
#include "outfit.h"
#include "enums.h"
#include "vocation.h"
#include "protocolgame.h"
#include "ioguild.h"
#include "party.h"
#include "depotlocker.h"
#include "guild.h"
#include "groups.h"
#include "town.h"
#include "auras.h"
#include "wings.h"
#include "shaders.h"
#include "storeinbox.h"
#include "battlepass.h"

#include <bitset>

class House;
class NetworkMessage;
class Weapon;
class ProtocolGame;
class ProtocolStatus;
class Npc;
class Party;
class SchedulerTask;
class Bed;
class Guild;
class MoveEvent;

enum skillsid_t {
	SKILLVALUE_LEVEL = 0,
	SKILLVALUE_TRIES = 1,
	SKILLVALUE_PERCENT = 2,
};

enum fightMode_t : uint8_t {
	FIGHTMODE_ATTACK = 1,
	FIGHTMODE_BALANCED = 2,
	FIGHTMODE_DEFENSE = 3,
};

enum tradestate_t : uint8_t {
	TRADE_NONE,
	TRADE_INITIATED,
	TRADE_ACCEPT,
	TRADE_ACKNOWLEDGE,
	TRADE_TRANSFER,
};

struct VIPEntry {
	VIPEntry(uint32_t guid, std::string name) :
		guid(guid), name(std::move(name)) {}

	uint32_t guid;
	std::string name;
};

struct OpenContainer {
	Container* container;
	uint16_t index;
};

struct OutfitEntry {
	constexpr OutfitEntry(uint16_t lookType, uint8_t addons) : lookType(lookType), addons(addons) {}

	uint16_t lookType;
	uint8_t addons;
};

struct Skill {
	uint64_t tries = 0;
	uint16_t level = 10;
	uint8_t percent = 0;
};

enum PlayerKillingResult_t {
	PLAYER_KILLING_FRAG,
	PLAYER_KILLING_RED,
	PLAYER_KILLING_BANISHMENT,
};

using MuteCountMap = std::map<uint32_t, uint32_t>;

static constexpr int32_t PLAYER_MAX_SPEED = 1500;
static constexpr int32_t PLAYER_MIN_SPEED = 80;

class Player final : public Creature, public Cylinder
{
	public:
		explicit Player(ProtocolGame_ptr p);
		~Player();

		// non-copyable
		Player(const Player&) = delete;
		Player& operator=(const Player&) = delete;

		Player* getPlayer() override {
			return this;
		}
		const Player* getPlayer() const override {
			return this;
		}

		void setID() override {
			if (id == 0) {
				id = playerAutoID++;
			}
		}

		static MuteCountMap muteCountMap;

		const std::string& getName() const override {
			return name;
		}
		void setName(const std::string& name) {
			this->name = name;
		}
		const std::string& getNameDescription() const override {
			return name;
		}
		std::string getDescription(int32_t lookDistance) const override;

		const std::string& getMarketDescription() const override {
			return name;
		}

		CreatureType_t getType() const override {
			return CREATURETYPE_PLAYER;
		}
		
	    bool hasAura() const
		{
			return defaultOutfit.lookAura != 0;
		}
		bool hasWings() const
		{
			return defaultOutfit.lookWings != 0;
		}
		bool hasShader() const
		{
			return defaultOutfit.lookShader != 0;
		}

		bool hasWing(const Wing* wing) const;
		bool addWing(uint8_t wingId);
		uint8_t getCurrentAura() const;
		void setCurrentAura(uint8_t auraId);
		bool hasAura(const Aura* aura) const;
		bool addAura(uint8_t auraId);
		uint8_t getCurrentWing() const;
		void setCurrentWing(uint8_t wingId);
		bool hasShader(const Shader* shader) const;
		
		bool addShader(uint8_t shaderId);
		void sendFYIBox(const std::string& message) {
			if (client) {
				client->sendFYIBox(message);
			}
		}

		void setGUID(uint32_t guid) {
			this->guid = guid;
		}
		uint32_t getGUID() const {
			return guid;
		}
		bool canSeeInvisibility() const override {
			return hasFlag(PlayerFlag_CanSenseInvisibility) || group->access;
		}

		void removeList() override;
		void addList() override;
		void kickPlayer(bool displayEffect, bool force = false);

		static uint64_t getExpForLevel(const uint64_t lv) {
			return (((lv - 6ULL) * lv + 17ULL) * lv - 12ULL) / 6ULL * 100ULL;
		}

		uint16_t getStaminaMinutes() const {
			return staminaMinutes;
		}

		uint64_t getBankBalance() const {
			return bankBalance;
		}
		void setBankBalance(uint64_t balance) {
			bankBalance = balance;
		}

		Guild* getGuild() const {
			return guild;
		}
		void setGuild(Guild* guild);

		GuildRank_ptr getGuildRank() const {
			return guildRank;
		}
		void setGuildRank(GuildRank_ptr newGuildRank) {
			guildRank = newGuildRank;
		}

		bool isGuildMate(const Player* player) const;
		bool canWalkthrough(const Creature* creature) const;

		const std::string& getGuildNick() const {
			return guildNick;
		}
		void setGuildNick(std::string nick) {
			guildNick = nick;
		}

		bool isInWar(const Player* player) const;
		bool isInWarList(uint32_t guildId) const;

		uint16_t getClientIcons() const;

		const GuildWarVector& getGuildWarVector() const {
			return guildWarVector;
		}

		Vocation* getVocation() const {
			return vocation;
		}

		StoreInbox* getStoreInbox() const {
			return storeInbox;
		}

		OperatingSystem_t getOperatingSystem() const {
			return operatingSystem;
		}
		void setOperatingSystem(OperatingSystem_t clientos) {
			operatingSystem = clientos;
		}

		uint16_t getProtocolVersion() const {
			if (!client) {
				return 0;
			}

			return client->getVersion();
		}

		bool hasSecureMode() const {
			return secureMode;
		}

		void setParty(Party* party) {
			this->party = party;
		}
		Party* getParty() const {
			return party;
		}
		PartyShields_t getPartyShield(const Player* player) const;
		bool isInviting(const Player* player) const;
		bool isPartner(const Player* player) const;
		void sendPlayerPartyIcons(Player* player);
		bool addPartyInvitation(Party* party);
		void removePartyInvitation(Party* party);
		void clearPartyInvitations();

		uint64_t getSpentMana() const {
			return manaSpent;
		}

		bool hasFlag(PlayerFlags value) const {
			return (group->flags & value) != 0;
		}

		void addBlessing(uint8_t blessing) {
			blessings.set(blessing);
		}
		void removeBlessing(uint8_t blessing) {
			blessings.reset(blessing);
		}
		bool hasBlessing(uint8_t blessing) const {
			return blessings.test(blessing);
		}

		bool isOffline() const {
			return (getID() == 0);
		}
		void disconnect() {
			if (client) {
				client->disconnect();
			}
		}
		uint32_t getIP() const;

		void addContainer(uint8_t cid, Container* container);
		void closeContainer(uint8_t cid);
		void setContainerIndex(uint8_t cid, uint16_t index);

		Container* getContainerByID(uint8_t cid);
		int8_t getContainerID(const Container* container) const;
		uint16_t getContainerIndex(uint8_t cid) const;

		bool canOpenCorpse(uint32_t ownerId) const;

		void addStorageValue(const uint32_t key, const int32_t value, const bool isLogin = false);
		bool getStorageValue(const uint32_t key, int32_t& value) const;
		void genReservedStorageRange();

		void setGroup(Group* newGroup) {
			group = newGroup;
		}
		Group* getGroup() const {
			return group;
		}

		void setLastDepotId(int16_t newId) {
			lastDepotId = newId;
		}
		int16_t getLastDepotId() const {
			return lastDepotId;
		}

		void resetIdleTime() {
			idleTime = 0;
		}

		bool isInGhostMode() const override {
			return ghostMode;
		}
		bool canSeeGhostMode(const Creature* creature) const override;
		void switchGhostMode() {
			ghostMode = !ghostMode;
		}
		
		bool canTradeItem(uint32_t storageCooldown);
		void updateTradeCooldown(uint32_t storageCooldown);
		
		Player() : lastSpecialEffectTime(0) { }
		bool canActivateSpecialEffect(uint64_t currentTime, uint64_t cooldown = 12000) {
			return (currentTime - lastSpecialEffectTime) >= cooldown;
		}
		void updateSpecialEffectTime(uint64_t currentTime) {
			lastSpecialEffectTime = currentTime;
		}

		uint32_t getAccount() const {
			return accountNumber;
		}
		AccountType_t getAccountType() const {
			return accountType;
		}
		uint32_t getLevel() const {
			return level;
		}
		uint8_t getLevelPercent() const {
			return levelPercent;
		}
		uint32_t getMagicLevel() const {
			return std::max<int32_t>(0, magLevel + varStats[STAT_MAGICPOINTS]);
		}
		uint32_t getBaseMagicLevel() const {
			return magLevel;
		}
		uint8_t getMagicLevelPercent() const {
			return magLevelPercent;
		}
		uint8_t getSoul() const {
			return soul;
		}
		bool isAccessPlayer() const {
			return group->access;
		}
		bool isPremium() const;
		void setPremiumTime(time_t premiumEndsAt);

		bool setVocation(uint16_t vocId);
		uint16_t getVocationId() const {
			return vocation->getId();
		}

		PlayerSex_t getSex() const {
			return sex;
		}
		void setSex(PlayerSex_t);
		uint64_t getExperience() const {
			return experience;
		}

		time_t getLastLoginSaved() const {
			return lastLoginSaved;
		}

		time_t getLastLogout() const {
			return lastLogout;
		}

		const Position& getLoginPosition() const {
			return loginPosition;
		}
		const Position& getTemplePosition() const {
			return town->getTemplePosition();
		}
		Town* getTown() const {
			return town;
		}
		void setTown(Town* town) {
			this->town = town;
		}

		bool isPushable() const override;
		uint32_t isMuted() const;
		void addMessageBuffer();
		void removeMessageBuffer();

		uint32_t removeItemOfType(uint16_t itemId, uint32_t amount, int32_t subType, bool ignoreEquipped = false) const;

		uint32_t getCapacity() const {
			if (hasFlag(PlayerFlag_CannotPickupItem)) {
				return 0;
			} else if (hasFlag(PlayerFlag_HasInfiniteCapacity)) {
				return std::numeric_limits<uint32_t>::max();
			}
			return capacity;
		}

		uint32_t getFreeCapacity() const {
			if (hasFlag(PlayerFlag_CannotPickupItem)) {
				return 0;
			} else if (hasFlag(PlayerFlag_HasInfiniteCapacity)) {
				return std::numeric_limits<uint32_t>::max();
			} else {
				return std::max<int32_t>(0, capacity - inventoryWeight);
			}
		}

		int32_t getMaxHealth() const override {
			return std::max<int32_t>(1, healthMax + varStats[STAT_MAXHITPOINTS]);
		}
		uint32_t getMana() const {
			return mana;
		}
		uint32_t getMaxMana() const {
			return std::max<int32_t>(0, manaMax + varStats[STAT_MAXMANAPOINTS]);
		}

		Item* getInventoryItem(slots_t slot) const;

		bool isItemAbilityEnabled(slots_t slot) const {
			return inventoryAbilities[slot];
		}
		void setItemAbility(slots_t slot, bool enabled) {
			inventoryAbilities[slot] = enabled;
		}

		void setVarSkill(skills_t skill, int32_t modifier) {
			varSkills[skill] += modifier;
		}

		void setVarSpecialSkill(SpecialSkills_t skill, int32_t modifier) {
			varSpecialSkills[skill] += modifier;
		}

		void setVarStats(stats_t stat, int32_t modifier);
		int32_t getDefaultStats(stats_t stat) const;

		void addConditionSuppressions(uint32_t conditions);
		void removeConditionSuppressions(uint32_t conditions);

		DepotLocker* getDepotLocker(uint32_t depotId, bool autoCreate);
		void onReceiveMail() const;
		bool isNearDepotBox(int32_t depotId = -1) const;

		bool canSeeCreature(const Creature* creature) const override;

		RaceType_t getRace() const override {
			return RACE_BLOOD;
		}

		uint64_t getMoney() const;

		uint64_t getExaltedCore() const {
			return getItemTypeCount(42813);
		}
		
		Item * getItemByUID(uint32_t uid) const;

		//safe-trade functions
		void setTradeState(tradestate_t state) {
			tradeState = state;
		}
		tradestate_t getTradeState() const {
			return tradeState;
		}
		Item* getTradeItem() {
			return tradeItem;
		}

		//shop functions
		void setShopOwner(Npc* owner, int32_t onBuy, int32_t onSell) {
			shopOwner = owner;
			purchaseCallback = onBuy;
			saleCallback = onSell;
		}

		Npc* getShopOwner(int32_t& onBuy, int32_t& onSell) {
			onBuy = purchaseCallback;
			onSell = saleCallback;
			return shopOwner;
		}

		const Npc* getShopOwner(int32_t& onBuy, int32_t& onSell) const {
			onBuy = purchaseCallback;
			onSell = saleCallback;
			return shopOwner;
		}

		//V.I.P. functions
		void notifyStatusChange(Player* loginPlayer, VipStatus_t status);
		bool removeVIP(uint32_t vipGuid);
		bool addVIP(uint32_t vipGuid, const std::string& vipName, VipStatus_t status);
		bool addVIPInternal(uint32_t vipGuid);

		//follow functions
		bool setFollowCreature(Creature* creature) override;

		//walk events
		void onWalkAborted() override;
		
		void openShopWindow(Npc* npc, const std::list<ShopInfo>& shop);
		bool closeShopWindow(bool sendCloseShopWindow = true);
		bool updateSaleShopList(const Item* item);
		bool hasShopItemForSale(uint32_t itemId, uint8_t subType) const;

		void setChaseMode(bool mode);
		void setFightMode(fightMode_t mode) {
			fightMode = mode;
		}
		void setSecureMode(bool mode) {
			secureMode = mode;
		}

		//combat functions
		bool setAttackedCreature(Creature* creature) override;
		bool isImmune(CombatType_t type) const override;
		bool isImmune(ConditionType_t type) const override;
		bool hasShield() const;
		bool isAttackable() const override;
		static bool lastHitIsPlayer(Creature* lastHitCreature);

		void changeHealth(int32_t healthChange, bool sendHealthChange = true) override;
		void changeMana(int32_t manaChange);
		void changeSoul(int32_t soulChange);

		bool isPzLocked() const {
			return pzLocked;
		}
		BlockType_t blockHit(Creature* attacker, CombatType_t combatType, int32_t& damage,
		                             bool checkDefense = false, bool checkArmor = false, bool field = false, bool ignoreResistances = false, bool meleeHit = false) override;
		void doAttacking(bool skipDelay = false) override;
		
		uint16_t getSpecialSkill(uint8_t skill) const {
			return std::max<int32_t>(0, varSpecialSkills[skill]);
		}
		uint16_t getSkillLevel(uint8_t skill) const {
			return std::max<int32_t>(0, skills[skill].level + varSkills[skill]);
		}
		uint16_t getBaseSkill(uint8_t skill) const {
			return skills[skill].level;
		}
		uint8_t getSkillPercent(uint8_t skill) const {
			return skills[skill].percent;
		}

		bool getAddAttackSkill() const {
			return addAttackSkillPoint;
		}
		BlockType_t getLastAttackBlockType() const {
			return lastAttackBlockType;
		}

		int32_t getCustomItemAttrStat(int32_t stat) const;
		int32_t getCustomItemMaximumAttrStat(int32_t stat) const;

		Item* getWeapon(slots_t slot, bool ignoreAmmo) const;
		Item* getWeapon(bool ignoreAmmo = false) const;
		WeaponType_t getWeaponType() const;
		int32_t getWeaponSkill(const Item* item) const;
		void getShieldAndWeapon(const Item*& shield, const Item*& weapon) const;

		void drainHealth(Creature* attacker, int32_t damage) override;
		void drainMana(Creature* attacker, int32_t manaLoss);
		void addManaSpent(uint64_t amount);
		void removeManaSpent(uint64_t amount, bool notify = false);
		void addSkillAdvance(skills_t skill, uint64_t count);
		void removeSkillTries(skills_t skill, uint64_t count, bool notify = false);

		time_t getPremiumEndsAt() const { return premiumEndsAt; }

		int32_t getArmor() const override;
		int32_t getDefense() const override;
		float getAttackFactor() const override;
		float getDefenseFactor() const override;

		void addInFightTicks(bool pzlock = false);

		//combat event functions
		void onAddCondition(ConditionType_t type) override;
		void onAddCombatCondition(ConditionType_t type) override;
		void onEndCondition(ConditionType_t type) override;
		void onAttackedCreature(Creature* target, bool addFightTicks = true) override;
		void onAttacked() override;
		void onAttackedCreatureDrainHealth(Creature* target, int32_t points) override;
		void onTargetCreatureGainHealth(Creature* target, int32_t points) override;
		bool onKilledCreature(Creature* target, bool lastHit = true) override;
		void onGainExperience(uint64_t gainExp, Creature* target) override;
		void onGainSharedExperience(uint64_t gainExp, Creature* source);
		void onAttackedCreatureBlockHit(BlockType_t blockType, bool meleeHit = false) override;
		void onBlockHit() override;
		void onChangeZone(ZoneType_t zone) override;
		void onAttackedCreatureChangeZone(ZoneType_t zone) override;
		void onIdleStatus() override;
		void onPlacedCreature() override;

		void setLastAttackBlockType(BlockType_t blocktype) {
			lastAttackBlockType = blocktype;
		}

		void decrementBloodHitCount(int32_t amount) {
			if (bloodHitCount > 0) {
				addAttackSkillPoint = true;
				bloodHitCount = bloodHitCount - std::min<int32_t>(bloodHitCount, std::abs(amount));
			} else {
				addAttackSkillPoint = false;
			}
		}

		int32_t getBloodHitCount() const {
			return bloodHitCount;
		}

		bool hasLightScroll() const;
		LightInfo getCreatureLight() const override;

		Skulls_t getSkull() const override;
		Skulls_t getSkullClient(const Creature* creature) const override;

		bool hasAttacked(const Player* attacked) const;
		void addAttacked(const Player* attacked);
		void removeAttacked(const Player* attacked);
		void clearAttacked();
		void addUnjustifiedDead(const Player* attacked);
		void sendCreatureSkull(const Creature* creature) const {
			if (client) {
				client->sendCreatureSkull(creature);
			}
		}
		void checkSkullTicks();

		bool getRandomStep(Direction& direction) const;
		bool canWear(uint32_t lookType, uint8_t addons) const;
		bool hasOutfit(uint32_t lookType, uint8_t addons);
		void addOutfit(uint16_t lookType, uint8_t addons);
		bool removeOutfit(uint16_t lookType);
		bool removeOutfitAddon(uint16_t lookType, uint8_t addons);
		bool getOutfitAddons(const Outfit& outfit, uint8_t& addons) const;

		size_t getMaxVIPEntries() const;
		size_t getMaxDepotItems() const;

		//tile
		//send methods
		void sendAddTileItem(const Tile* tile, const Position& pos, const Item* item) {
			if (client) {
				int32_t stackpos = tile->getStackposOfItem(this, item);
				if (stackpos != -1) {
					client->sendAddTileItem(pos, stackpos, item);
				}
			}
		}
		void sendUpdateTileItem(const Tile* tile, const Position& pos, const Item* item) {
			if (client) {
				int32_t stackpos = tile->getStackposOfItem(this, item);
				if (stackpos != -1) {
					client->sendUpdateTileItem(pos, stackpos, item);
				}
			}
		}
		void sendRemoveTileThing(const Position& pos, int32_t stackpos) {
			if (stackpos != -1 && client) {
				client->sendRemoveTileThing(pos, stackpos);
			}
		}
		void sendUpdateTileCreature(const Creature* creature) {
			if (client) {
				client->sendUpdateTileCreature(creature->getPosition(), creature->getTile()->getClientIndexOfCreature(this, creature), creature);
			}
		}
		void sendRemoveTileCreature(const Creature* creature, const Position& pos, int32_t stackpos) {
			if (client) {
				client->sendRemoveTileCreature(creature, pos, stackpos);
			}
		}
		void sendUpdateTile(const Tile* tile, const Position& pos) {
			if (client) {
				client->sendUpdateTile(tile, pos);
			}
		}

		void sendChannelMessage(const std::string& author, const std::string& text, SpeakClasses type, uint16_t channel) {
			if (client) {
				client->sendChannelMessage(author, text, type, channel);
			}
		}
		void sendCreatureAppear(const Creature* creature, const Position& pos) {
			if (client) {
				client->sendAddCreature(creature, pos, creature->getTile()->getClientIndexOfCreature(this, creature));
			}
		}
		void sendMoveCreature(const Creature* creature, const Position& newPos, int32_t newStackPos, const Position& oldPos, int32_t oldStackPos, bool teleport) {
			if (client) {
				client->sendMoveCreature(creature, newPos, newStackPos, oldPos, oldStackPos, teleport);
			}
		}
		void sendCreatureTurn(const Creature* creature) {
			if (client && canSeeCreature(creature)) {
				int32_t stackpos = creature->getTile()->getClientIndexOfCreature(this, creature);
				if (stackpos != -1) {
					client->sendCreatureTurn(creature, stackpos);
				}
			}
		}
		void sendCreatureSay(const Creature* creature, SpeakClasses type, const std::string& text, const Position* pos = nullptr) {
			if (client) {
				client->sendCreatureSay(creature, type, text, pos);
			}
		}
		void sendPrivateMessage(const Player* speaker, SpeakClasses type, const std::string& text) {
			if (client) {
				client->sendPrivateMessage(speaker, type, text);
			}
		}
		void sendCreatureSquare(const Creature* creature, SquareColor_t color) {
			if (client) {
				client->sendCreatureSquare(creature, color);
			}
		}
		void sendCreatureChangeOutfit(const Creature* creature, const Outfit_t& outfit) {
			if (client) {
				client->sendCreatureOutfit(creature, outfit);
			}
		}
		void sendCreatureChangeVisible(const Creature* creature, bool visible) {
			if (!client) {
				return;
			}

			if (creature->getPlayer()) {
				if (visible) {
					client->sendCreatureOutfit(creature, creature->getCurrentOutfit());
				} else {
					static Outfit_t outfit;
					client->sendCreatureOutfit(creature, outfit);
				}
			} else if (canSeeInvisibility()) {
				client->sendCreatureOutfit(creature, creature->getCurrentOutfit());
			} else {
				int32_t stackpos = creature->getTile()->getClientIndexOfCreature(this, creature);
				if (stackpos == -1) {
					return;
				}

				if (visible) {
					client->sendAddCreature(creature, creature->getPosition(), stackpos);
				} else {
					client->sendRemoveTileCreature(creature, creature->getPosition(), stackpos);
				}
			}
		}
		void sendCreatureLight(const Creature* creature) {
			if (client) {
				client->sendCreatureLight(creature);
			}
		}
		void sendCreatureShield(const Creature* creature) {
			if (client) {
				client->sendCreatureShield(creature);
			}
		}

		void sendCreatureStar(const Creature* creature) {
			if (client) {
				client->sendCreatureStar(creature);
			}
		}

		//container
		void sendAddContainerItem(const Container* container, const Item* item);
		void sendUpdateContainerItem(const Container* container, uint16_t slot, const Item* newItem);
		void sendRemoveContainerItem(const Container* container, uint16_t slot);
		void sendContainer(uint8_t cid, const Container* container, bool hasParent) {
			if (client) {
				client->sendContainer(cid, container, hasParent);
			}
		}

		//inventory
		void sendInventoryItem(slots_t slot, const Item* item) {
			if (client) {
				client->sendInventoryItem(slot, item);
			}
		}

		void autoOpenContainers();

		//event methods
		void onUpdateTileItem(const Tile* tile, const Position& pos, const Item* oldItem,
		                              const ItemType& oldType, const Item* newItem, const ItemType& newType) override;
		void onRemoveTileItem(const Tile* tile, const Position& pos, const ItemType& iType,
		                              const Item* item) override;

		void onCreatureAppear(Creature* creature, bool isLogin) override;
		void onRemoveCreature(Creature* creature, bool isLogout) override;
		void onCreatureMove(Creature* creature, const Tile* newTile, const Position& newPos,
		                            const Tile* oldTile, const Position& oldPos, bool teleport) override;

		void onAttackedCreatureDisappear(bool isLogout) override;
		void onFollowCreatureDisappear(bool isLogout) override;

		//container
		void onAddContainerItem(const Item* item);
		void onUpdateContainerItem(const Container* container, const Item* oldItem, const Item* newItem);
		void onRemoveContainerItem(const Container* container, const Item* item);

		void onCloseContainer(const Container* container);
		void onSendContainer(const Container* container);
		void autoCloseContainers(const Container* container);

		//inventory
		void onUpdateInventoryItem(Item* oldItem, Item* newItem);
		void onRemoveInventoryItem(Item* item);

		void sendCancelMessage(const std::string& msg) const {
			if (client) {
				client->sendTextMessage(TextMessage(MESSAGE_STATUS_SMALL, msg));
			}
		}
		void sendCancelMessage(ReturnValue message) const;
		void sendCancelTarget() const {
			if (client) {
				client->sendCancelTarget();
			}
		}
		void sendCancelWalk() const {
			if (client) {
				client->sendCancelWalk();
			}
		}
#ifdef OTC_NEWWALKING
		void sendNewCancelWalk() const {
			if (client) {
				client->sendNewCancelWalk();
			}
		}
#endif
		void sendChangeSpeed(const Creature* creature, uint32_t newSpeed) const {
			if (client) {
				client->sendChangeSpeed(creature, newSpeed);
			}
		}
		void sendCreatureHealth(const Creature* creature) const {
			if (client) {
				client->sendCreatureHealth(creature);
			}
		}
		void sendDistanceShoot(const Position& from, const Position& to, unsigned char type) const {
			if (client) {
				client->sendDistanceShoot(from, to, type);
			}
		}
		void sendHouseWindow(House* house, uint32_t listId) const;
		void sendCreatePrivateChannel(uint16_t channelId, const std::string& channelName) {
			if (client) {
				client->sendCreatePrivateChannel(channelId, channelName);
			}
		}
		void sendClosePrivate(uint16_t channelId);
		void sendIcons() const {
			if (client) {
				client->sendIcons(getClientIcons());
			}
		}
		void sendMagicEffect(const Position& pos, uint8_t type) const {
			if (client) {
				client->sendMagicEffect(pos, type);
			}
		}
		void sendPing();
		void sendPingBack() const {
			if (client) {
				client->sendPingBack();
			}
		}
		void sendStats();
		void sendSkills() const {
			if (client) {
				client->sendSkills();
			}
		}
		void sendTextMessage(MessageClasses mclass, const std::string& message) const {
			if (client) {
				client->sendTextMessage(TextMessage(mclass, message));
			}
		}
		void sendTextMessage(const TextMessage& message) const {
			if (client) {
				client->sendTextMessage(message);
			}
		}
		void sendAdvancedAnimatedText(const Position& pos, uint8_t color, const std::string& text, const std::string& font) const {
			if (client) {
				client->sendAdvancedAnimatedText(pos, color, text, font);
			}
		}
		void sendAnimatedText(const Position& pos, uint8_t color, const std::string& text) const {
			if (client) {
				client->sendAnimatedText(pos, color, text);
			}
		}
		void sendTextWindow(Item* item, uint16_t maxlen, bool canWrite) const {
			if (client) {
				client->sendTextWindow(windowTextId, item, maxlen, canWrite);
			}
		}
		void sendTextWindow(uint32_t itemId, const std::string& text) const {
			if (client) {
				client->sendTextWindow(windowTextId, itemId, text);
			}
		}
		void sendToChannel(const Creature* creature, SpeakClasses type, const std::string& text, uint16_t channelId) const {
			if (client) {
				client->sendToChannel(creature, type, text, channelId);
			}
		}
		void sendShop(Npc* npc) const {
			if (client) {
				client->sendShop(npc, shopItemList);
			}
		}
		void sendSaleItemList(bool isHalloween = false) const {
			if (client) {
				client->sendSaleItemList(shopItemList, isHalloween);
			}
		}
		void sendCloseShop() const {
			if (client) {
				client->sendCloseShop();
			}
		}
		void sendTradeItemRequest(const std::string& traderName, const Item* item, bool ack) const {
			if (client) {
				client->sendTradeItemRequest(traderName, item, ack);
			}
		}
		void sendTradeClose() const {
			if (client) {
				client->sendCloseTrade();
			}
		}
		void sendWorldLight(LightInfo lightInfo) {
			if (client) {
				client->sendWorldLight(lightInfo);
			}
		}
		void sendChannelsDialog() {
			if (client) {
				client->sendChannelsDialog();
			}
		}
		void sendOpenPrivateChannel(const std::string& receiver) {
			if (client) {
				client->sendOpenPrivateChannel(receiver);
			}
		}
		void sendOutfitWindow() {
			if (client) {
				client->sendOutfitWindow();
			}
		}
		void sendCloseContainer(uint8_t cid) {
			if (client) {
				client->sendCloseContainer(cid);
			}
		}
		void sendRemoveRuleViolationReport(const std::string& name) const {
			if (client) {
				client->sendRemoveRuleViolationReport(name);
			}
		}
		void sendRuleViolationCancel(const std::string& name) const {
			if (client) {
				client->sendRuleViolationCancel(name);
			}
		}
		void sendLockRuleViolationReport() const {
			if (client) {
				client->sendLockRuleViolation();
			}
		}
		void sendRuleViolationsChannel(uint16_t channelId) const {
			if (client) {
				client->sendRuleViolationsChannel(channelId);
			}
		}

		void sendChannel(uint16_t channelId, const std::string& channelName) {
			if (client) {
				client->sendChannel(channelId, channelName);
			}
		}
		void sendFightModes() {
			if (client) {
				client->sendFightModes();
			}
		}
		void sendNetworkMessage(const NetworkMessage& message) {
			if (client) {
				client->writeToOutputBuffer(message);
			}
		}

		void updateBattlepass(BattlePassQuests_t id, const std::string& value);
		void updateBattlepass(BattlePassQuests_t id, uint16_t value);
		void updateBattlepass(BattlePassQuests_t id);
		void updateBattlepassCounter(BattlePassQuests_t id, std::any value);
		void addBattlepassQuest(BattlePassType_t type, BattlePassQuests_t id, uint32_t amount, time_t cooldown, const std::any& value, bool shuffled);
		void resetBattlepass();
		void sendBattlepassQuests(bool sendLevels);
		void completeBattlepassQuest(uint8_t id, uint16_t questId);
		void closeBattlepassWindow() { openBattlepass = false; }
		void buyPremiumBattlepass();
		bool hasPremiumBattlepass() const { return battlepass->hasPremium(); }

		void receivePing() {
			lastPong = OTSYS_TIME();
		}

		void onIdleStimulus() override;
		void onThink(uint32_t interval) override;

		void postAddNotification(Thing* thing, const Cylinder* oldParent, int32_t index, cylinderlink_t link = LINK_OWNER) override;
		void postRemoveNotification(Thing* thing, const Cylinder* newParent, int32_t index, cylinderlink_t link = LINK_OWNER) override;

		Item* getWriteItem(uint32_t& windowTextId, uint16_t& maxWriteLen);
		void setWriteItem(Item* item, uint16_t maxWriteLen = 0);

		House* getEditHouse(uint32_t& windowTextId, uint32_t& listId);
		void setEditHouse(House* house, uint32_t listId = 0);

		void learnInstantSpell(const std::string& spellName);
		void forgetInstantSpell(const std::string& spellName);
		bool hasLearnedInstantSpell(const std::string& spellName) const;

		const std::map<uint8_t, OpenContainer>& getOpenContainers() const
		{
			return openContainers;
		}

		void removeKillingEnd() {
			playerKillerEnd = 0;
		}

		void updateRegeneration();
		bool isLightExchangeable() const override;

		bool addBestiaryKill(uint16_t id);
		int32_t getBestiaryKill(uint16_t id);

		BestiaryStage_t getBestiaryStage(MonsterType* mType);

		bool getCharm(std::string charm) const {
			auto charmSearch = allCharms.find(charm);
			if (charmSearch != allCharms.end()) {
				return true;
			}

			return false;
		}
		std::string getCharmMonster(std::string charm) {
			auto charmSearch = allCharms.find(charm);
			if (charmSearch != allCharms.end()) {
				return charmSearch->second;
			}

			return "";
		}
		void setCharm(std::string charm, bool canCharm = false, std::string monster = "") {
			if (canCharm) {
				allCharms[charm] = monster;
			} else {
				auto charmSearch = allCharms.find(charm);
				if (charmSearch != allCharms.end()) {
					allCharms.erase(charmSearch);
				}
			}
		}

		uint32_t getWound(std::string monster = "") const {
			uint32_t physicalDamage = 0;

			if (monster != "") {
				auto charmSearch = allCharms.find("Wound");
				if (charmSearch != allCharms.end()) {
					if (monster == charmSearch->second) {
						physicalDamage += 4;
					}
				}
			}

			return physicalDamage;
		}

		uint32_t getEnflame(std::string monster = "") const {
			uint32_t enflame = 0;

			if (monster != "") {
				auto charmSearch = allCharms.find("Enflame");
				if (charmSearch != allCharms.end()) {
					if (monster == charmSearch->second) {
						enflame += 4;
					}
				}
			}

			return enflame;
		}

		uint32_t getPoison(std::string monster = "") const {
			uint32_t poison = 0;

			if (monster != "") {
				auto charmSearch = allCharms.find("Poison");
				if (charmSearch != allCharms.end()) {
					if (monster == charmSearch->second) {
						poison += 4;
					}
				}
			}

			return poison;
		}

		uint32_t getZap(std::string monster = "") const {
			uint32_t zap = 0;

			if (monster != "") {
				auto charmSearch = allCharms.find("Zap");
				if (charmSearch != allCharms.end()) {
					if (monster == charmSearch->second) {
						zap += 4;
					}
				}
			}

			return zap;
		}

		uint32_t getCriticalChance(std::string monster = "") const {
			uint32_t criticalChance = 0;

			static const slots_t slots[] = {CONST_SLOT_LEFT, CONST_SLOT_RIGHT};
			for (slots_t slot : slots) {
				Item* inventoryItem = inventory[slot];
				if (inventoryItem) {
					criticalChance += this->getCustomItemMaximumAttrStat(ITEM_RND_CRITICAL);
				}
			}

			if (monster != "") {
				auto charmSearch = allCharms.find("Low Blow");
				if (charmSearch != allCharms.end()) {
					if (monster == charmSearch->second) {
						criticalChance += 4;
					}
				}
			}

			return criticalChance;
		}

		uint32_t getDodge(std::string monster = "") const {
			uint32_t dodge = 0;

			if (monster != "") {
				auto charmSearch = allCharms.find("Dodge");
				if (charmSearch != allCharms.end()) {
					if (monster == charmSearch->second) {
						dodge += 4;
					}
				}
			}

			return dodge;
		}

		uint32_t getParry(std::string monster = "") const {
			uint32_t parry = 0;

			static const slots_t slots[] = {CONST_SLOT_LEFT, CONST_SLOT_RIGHT};
			for (slots_t slot : slots) {
				Item* inventoryItem = inventory[slot];
				if (inventoryItem) {
					parry += this->getCustomItemMaximumAttrStat(ITEM_RND_PARRY);
				}
			}

			if (monster != "") {
				auto charmSearch = allCharms.find("Parry");
				if (charmSearch != allCharms.end()) {
					if (monster == charmSearch->second) {
						parry += 4;
					}
				}
			}

			return parry;
		}

		uint32_t getNumb(std::string monster = "") const {
			uint32_t numb = 0;

			if (monster != "") {
				auto charmSearch = allCharms.find("Numb");
				if (charmSearch != allCharms.end()) {
					if (monster == charmSearch->second) {
						numb += 4;
					}
				}
			}

			return numb;
		}

		uint32_t getAdrenalineBurst(std::string monster = "") const {
			uint32_t adrenalineBurst = 0;

			if (monster != "") {
				auto charmSearch = allCharms.find("Adrenaline Burst");
				if (charmSearch != allCharms.end()) {
					if (monster == charmSearch->second) {
						adrenalineBurst += 4;
					}
				}
			}

			return adrenalineBurst;
		}

	private:
		std::forward_list<Condition*> getMuteConditions() const;

		void checkTradeState(const Item* item);
		bool hasCapacity(const Item* item, uint32_t count) const;

		void gainExperience(uint64_t gainExp, Creature* source);
		void addExperience(Creature* source, uint64_t exp);
		void removeExperience(uint64_t exp, bool sendText = false);

		void updateInventoryWeight();

		void death(Creature* lastHitCreature) override;
		bool dropCorpse(Creature* lastHitCreature, Creature* mostDamageCreature, bool lastHitUnjustified, bool mostDamageUnjustified) override;
		Item* getCorpse(Creature* lastHitCreature, Creature* mostDamageCreature) override;

		//cylinder implementations
		ReturnValue queryAdd(int32_t index, const Thing& thing, uint32_t count,
				uint32_t flags, Creature* actor = nullptr) const override;
		ReturnValue queryMaxCount(int32_t index, const Thing& thing, uint32_t count, uint32_t& maxQueryCount,
				uint32_t flags) const override;
		ReturnValue queryRemove(const Thing& thing, uint32_t count, uint32_t flags, Creature* actor = nullptr) const override;
		Cylinder* queryDestination(int32_t& index, const Thing& thing, Item** destItem,
				uint32_t& flags) override;

		void addThing(Thing*) override {}
		void addThing(int32_t index, Thing* thing) override;

		void updateThing(Thing* thing, uint16_t itemId, uint32_t count) override;
		void replaceThing(uint32_t index, Thing* thing) override;

		void removeThing(Thing* thing, uint32_t count) override;

		int32_t getThingIndex(const Thing* thing) const override;
		size_t getFirstIndex() const override;
		size_t getLastIndex() const override;
		uint32_t getItemTypeCount(uint16_t itemId, int32_t subType = -1) const override;
		std::map<uint32_t, uint32_t>& getAllItemTypeCount(std::map<uint32_t, uint32_t>& countMap) const override;
		Thing* getThing(size_t index) const override;

		void internalAddThing(Thing* thing) override;
		void internalAddThing(uint32_t index, Thing* thing) override;

		PlayerKillingResult_t checkPlayerKilling();

		std::unordered_set<uint32_t> attackedSet;
		std::unordered_set<uint32_t> VIPList;

		std::map<uint8_t, OpenContainer> openContainers;
		std::map<uint32_t, DepotLocker_ptr> depotLockerMap;
		std::map<uint32_t, int32_t> storageMap;

		std::vector<OutfitEntry> outfits;
		GuildWarVector guildWarVector;

		std::list<ShopInfo> shopItemList;

		std::forward_list<Party*> invitePartyList;
		std::forward_list<std::string> learnedInstantSpellList;
		std::forward_list<Condition*> storedConditionList; // TODO: This variable is only temporarily used when logging in, get rid of it somehow

		std::list<time_t> murderTimeStamps;

		std::string name;
		std::string guildNick;

		Skill skills[SKILL_LAST + 1];
		LightInfo itemsLight;
		Position loginPosition;

		time_t lastLoginSaved = 0;
		time_t lastLogout = 0;
		time_t premiumEndsAt = 0;
		time_t playerKillerEnd = 0;

		uint64_t experience = 0;
		uint64_t manaSpent = 0;
		uint64_t bankBalance = 0;
		int64_t earliestSpellTime = 0;
		int64_t earliestAttackTime = 0;
		int64_t earliestMultiUseTime = 0;
#ifdef OTC_NEWWALKING
		int64_t earliestAutoWalkTime = 0;
#endif
		int64_t formerLogoutTime = 0;
		int64_t formerPartyTime = 0;
		int64_t lastPing;
		int64_t lastPong;

		Guild* guild = nullptr;
		GuildRank_ptr guildRank = nullptr;
		Group* group = nullptr;
		Item* tradeItem = nullptr;
 		Item* inventory[CONST_SLOT_LAST + 1] = {};
		Item* writeItem = nullptr;
		House* editHouse = nullptr;
		Npc* shopOwner = nullptr;
		Party* party = nullptr;
		Player* tradePartner = nullptr;
		ProtocolGame_ptr client;
		Town* town = nullptr;
		Vocation* vocation = nullptr;
		StoreInbox* storeInbox = nullptr;

		uint32_t lastUnjustCreatureId = 0;
		uint32_t inventoryWeight = 0;
		uint32_t capacity = 40000;
		uint32_t damageImmunities = 0;
		uint32_t conditionImmunities = 0;
		uint32_t conditionSuppressions = 0;
		uint32_t level = 1;
		uint32_t magLevel = 0;
		uint32_t MessageBufferTicks = 0;
		uint32_t lastIP = 0;
		uint32_t accountNumber = 0;
		uint32_t guid = 0;
		uint32_t windowTextId = 0;
		uint32_t editListId = 0;
		uint32_t mana = 0;
		uint32_t manaMax = 0;
		int32_t varSkills[SKILL_LAST + 1] = {};
		int32_t varSpecialSkills[SPECIALSKILL_LAST + 1] = {};
		int32_t varStats[STAT_LAST + 1] = {};
		int32_t purchaseCallback = -1;
		int32_t saleCallback = -1;
		int32_t MessageBufferCount = 0;
		int32_t bloodHitCount = 0;
		int32_t idleTime = 0;

		// Special
		int32_t increasedMana = 0;
		int32_t increasedHP = 0;

		int32_t perseveranceHP = 0;
		int32_t perseveranceMP = 0;
		int64_t perseveranceTicks = 0;

		int64_t berserkTicks = 0;
		int64_t sharpshooterTicks = 0;

		uint16_t staminaMinutes = 2520;
		uint16_t maxWriteLen = 0;
		int16_t lastDepotId = -1;

		uint8_t soul = 0;
		std::bitset<6> blessings;
		uint8_t levelPercent = 0;
		uint8_t magLevelPercent = 0;

		std::unordered_map<std::string, std::string> allCharms;

		PlayerSex_t sex = PLAYERSEX_FEMALE;
		OperatingSystem_t operatingSystem = CLIENTOS_NONE;
		BlockType_t lastAttackBlockType = BLOCK_NONE;
		tradestate_t tradeState = TRADE_NONE;
		fightMode_t fightMode = FIGHTMODE_ATTACK;
		AccountType_t accountType = ACCOUNT_TYPE_NORMAL;

		std::unique_ptr<BattlePass> battlepass;

		bool isLoggingOut = false;
		bool chaseMode = false;
		bool secureMode = false;
		bool ghostMode = false;
		bool pzLocked = false;
		bool isConnecting = false;
		bool addAttackSkillPoint = false;
		bool openBattlepass = false;
		bool inventoryAbilities[CONST_SLOT_LAST + 1] = {};

		static uint32_t playerAutoID;
		
		uint64_t lastSpecialEffectTime; // Armazena o último tempo de ativação bleeding

    void updateItemsLight();
		int32_t getStepSpeed() const override {
			return getSpeed();
		}
		void updateBaseSpeed() {
			if (!hasFlag(PlayerFlag_SetMaxSpeed)) {
				baseSpeed = vocation->getBaseSpeed() + (level > 1 ? level - 1: 0);
			} else {
				baseSpeed = PLAYER_MAX_SPEED;
			}
		}

		bool isPromoted() const;

		uint32_t getAttackSpeed() const;

		static uint8_t getPercentLevel(uint64_t count, uint64_t nextLevelCount);
		double getLostPercent() const;
		uint64_t getLostExperience() const override {
			return skillLoss ? static_cast<uint64_t>(experience * getLostPercent()) : 0;
		}
		uint32_t getDamageImmunities() const override {
			return damageImmunities;
		}
		uint32_t getConditionImmunities() const override {
			return conditionImmunities;
		}
		uint32_t getConditionSuppressions() const override {
			return conditionSuppressions;
		}
		uint16_t getLookCorpse() const override;

		friend class Game;
		friend class Npc;
		friend class Creature;
		friend class LuaScriptInterface;
		friend class Map;
		friend class Actions;
		friend class IOLoginData;
		friend class ProtocolGame;
		friend class NpcBehavior;
		friend class Spell;
		friend class InstantSpell;
		friend class ProtocolStatus;
		friend class MoveEvent;
		friend class Party;
		friend class Combat;
};

#endif
