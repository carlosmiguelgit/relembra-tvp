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

#ifndef FS_PROTOCOLGAME_H_FACA2A2D1A9348B78E8FD7E8003EBB87
#define FS_PROTOCOLGAME_H_FACA2A2D1A9348B78E8FD7E8003EBB87

#include "protocol.h"
#include "chat.h"
#include "creature.h"
#include "tasks.h"
#include "battlepass.h"

#include "walkmatrix.h"

class NetworkMessage;
class Player;
class Game;
class House;
class Container;
class Tile;
class Connection;
class Quest;
class ProtocolGame;
using ProtocolGame_ptr = std::shared_ptr<ProtocolGame>;

extern Game g_game;

struct TextMessage
{
	MessageClasses type = MESSAGE_STATUS_DEFAULT;
	std::string text;
	
	TextMessage() = default;
	TextMessage(MessageClasses type, std::string text) : type(type), text(std::move(text)) {}
};

class ProtocolGame final : public Protocol
{
	public:
		// static protocol information
		enum {server_sends_first = false};
		enum {protocol_identifier = 0x0A}; // Not required as we send first

		static const char* protocol_name() {
			return "gameworld protocol";
		}

		explicit ProtocolGame(Connection_ptr connection) : Protocol(connection) {}

		void login(const std::string& name, uint32_t accountId, OperatingSystem_t operatingSystem);
		void logout(bool forced);

		uint16_t getVersion() const {
			return version;
		}

	private:
		ProtocolGame_ptr getThis() {
			return std::static_pointer_cast<ProtocolGame>(shared_from_this());
		}
		void connect(uint32_t playerId, OperatingSystem_t operatingSystem);
		void disconnectClient(const std::string& message) const;
		void writeToOutputBuffer(const NetworkMessage& msg);

		void release() override;

		void checkCreatureAsKnown(uint32_t id, bool& known, uint32_t& removedKnown);

		bool canSee(int32_t x, int32_t y, int32_t z) const;
		bool canSee(const Creature*) const;
		bool canSee(const Position& pos) const;

		// we have all the parse methods
		void parsePacket(NetworkMessage& msg) override;
		void parsePacketOnDispatcher(NetworkMessage msg) override;
		void onRecvFirstMessage(NetworkMessage& msg) override;

		//Parse methods
		void parseAutoWalk(NetworkMessage& msg);
		void parseSetOutfit(NetworkMessage& msg);
		void parseSay(NetworkMessage& msg);
		void parseLookAt(NetworkMessage& msg);
		void parseLookInBattleList(NetworkMessage& msg);
		void parseFightModes(NetworkMessage& msg);
		void parseAttack(NetworkMessage& msg);
		void parseFollow(NetworkMessage& msg);

		void parseProcessRuleViolationReport(NetworkMessage& msg);
		void parseCloseRuleViolationReport(NetworkMessage& msg);

		void parseBugReport(NetworkMessage& msg);
		void parseDebugAssert(NetworkMessage& msg);

		void parseThrow(NetworkMessage& msg);
		void parseUseItemEx(NetworkMessage& msg);
		void parseUseWithCreature(NetworkMessage& msg);
		void parseUseItem(NetworkMessage& msg);
		void parseCloseContainer(NetworkMessage& msg);
		void parseUpArrowContainer(NetworkMessage& msg);
		void parseUpdateContainer(NetworkMessage& msg);
		void parseTextWindow(NetworkMessage& msg);
		void parseHouseWindow(NetworkMessage& msg);

		void parseLookInShop(NetworkMessage& msg);
		void parsePlayerPurchase(NetworkMessage& msg);
		void parsePlayerSale(NetworkMessage& msg);

		void parseInviteToParty(NetworkMessage& msg);
		void parseJoinParty(NetworkMessage& msg);
		void parseRevokePartyInvite(NetworkMessage& msg);
		void parsePassPartyLeadership(NetworkMessage& msg);
		void parseEnableSharedPartyExperience(NetworkMessage& msg);

		//trade methods
		void parseRequestTrade(NetworkMessage& msg);
		void parseLookInTrade(NetworkMessage& msg);

		//VIP methods
		void parseAddVip(NetworkMessage& msg);
		void parseRemoveVip(NetworkMessage& msg);

		void parseRotateItem(NetworkMessage& msg);

		//Channel tabs
		void parseChannelInvite(NetworkMessage& msg);
		void parseChannelExclude(NetworkMessage& msg);
		void parseOpenChannel(NetworkMessage& msg);
		void parseOpenPrivateChannel(NetworkMessage& msg);
		void parseCloseChannel(NetworkMessage& msg);

		//Battlepass
		void parseBattlepass(NetworkMessage& msg);
		void sendBattlepassPremium(uint8_t id, bool isPremium);
		void sendBattlepassQuest(uint16_t questId, const BattlePassPlayerData& data);
		void sendBattlepassQuests(uint32_t experience, uint16_t level, const BattlePassPlayerDataMap& data, const BattlePassRewardsMap& levels, bool hasPremium, bool sendLevels);
		void sendUpdateBattlepassQuest(uint16_t id, uint32_t experience, uint16_t level, time_t cooldown);
		void sendUpdateBattlepassQuest(const BattlePassQuestsVector& data);

		//Send functions
		void sendChannelMessage(const std::string& author, const std::string& text, SpeakClasses type, uint16_t channel);
		void sendClosePrivate(uint16_t channelId);
		void sendCreatePrivateChannel(uint16_t channelId, const std::string& channelName);
		void sendChannelsDialog();
		void sendChannel(uint16_t channelId, const std::string& channelName);
		void sendOpenPrivateChannel(const std::string& receiver);
		void sendToChannel(const Creature* creature, SpeakClasses type, const std::string& text, uint16_t channelId);
		void sendPrivateMessage(const Player* speaker, SpeakClasses type, const std::string& text);
		void sendIcons(uint16_t icons);
		void sendFYIBox(const std::string& message);

		void sendDistanceShoot(const Position& from, const Position& to, uint8_t type);
		void sendMagicEffect(const Position& pos, uint8_t type);
		void sendCreatureHealth(const Creature* creature);
		void sendSkills();
		void sendPing();
		void sendPingBack();
		void sendCreatureTurn(const Creature* creature, uint32_t stackPos);
		void sendCreatureSay(const Creature* creature, SpeakClasses type, const std::string& text, const Position* pos = nullptr);

		void sendCancelWalk();
		void sendChangeSpeed(const Creature* creature, uint32_t speed);
		void sendCancelTarget();
		void sendCreatureOutfit(const Creature* creature, const Outfit_t& outfit);
		void sendStats();
		void sendTextMessage(const TextMessage& message);
		void sendAnimatedText(const Position& pos, uint8_t color, const std::string& text);
		void sendAdvancedAnimatedText(const Position& pos, uint8_t color, const std::string& text, const std::string& font);

		void sendCreatureShield(const Creature* creature);
		void sendCreatureSkull(const Creature* creature);
		void sendCreatureStar(const Creature* creature);

		void sendShop(Npc* npc, const ShopInfoList& itemList);
		void sendCloseShop();
		void sendSaleItemList(const std::list<ShopInfo>& shop, bool isHalloween = false);

		void sendTradeItemRequest(const std::string& traderName, const Item* item, bool ack);
		void sendCloseTrade();

		void sendTextWindow(uint32_t windowTextId, Item* item, uint16_t maxlen, bool canWrite);
		void sendTextWindow(uint32_t windowTextId, uint32_t itemId, const std::string& text);
		void sendHouseWindow(uint32_t windowTextId, const std::string& text);
		void sendOutfitWindow();

		void sendUpdatedVIPStatus(uint32_t guid, VipStatus_t newStatus);
		void sendVIP(uint32_t guid, const std::string& name, VipStatus_t status);
		void sendVIPEntries();

		void sendFightModes();

		void sendCreatureLight(const Creature* creature);
		void sendWorldLight(LightInfo lightInfo);

		void sendCreatureSquare(const Creature* creature, SquareColor_t color);

		//rule violations
		void sendRemoveRuleViolationReport(const std::string& name);
		void sendLockRuleViolation();
		void sendRuleViolationCancel(const std::string& name);
		void sendRuleViolationsChannel(uint16_t channelId);

		//tiles
		void sendMapDescription(const Position& pos);

		void sendAddTileItem(const Position& pos, uint32_t stackpos, const Item* item);
		void sendUpdateTileItem(const Position& pos, uint32_t stackpos, const Item* item);
		void sendRemoveTileThing(const Position& pos, uint32_t stackpos);
		void sendUpdateTileCreature(const Position& pos, uint32_t stackpos, const Creature* creature);
		void sendRemoveTileCreature(const Creature* creature, const Position& pos, uint32_t stackpos);
		void sendUpdateTile(const Tile* tile, const Position& pos);

		void sendAddCreature(const Creature* creature, const Position& pos, int32_t stackpos);
		void sendMoveCreature(const Creature* creature, const Position& newPos, int32_t newStackPos,
		                      const Position& oldPos, int32_t oldStackPos, bool teleport);

		//containers
		void sendAddContainerItem(uint8_t cid, const Item* item);
		void sendUpdateContainerItem(uint8_t cid, uint16_t slot, const Item* item);
		void sendRemoveContainerItem(uint8_t cid, uint16_t slot);

		void sendContainer(uint8_t cid, const Container* container, bool hasParent);
		void sendCloseContainer(uint8_t cid);

		//inventory
		void sendInventoryItem(slots_t slot, const Item* item);

		//Help functions

		// translate a tile to client-readable format
		void GetTileDescription(const Tile* tile, NetworkMessage& msg);

		// translate a floor to client-readable format
		void GetFloorDescription(NetworkMessage& msg, int32_t x, int32_t y, int32_t z,
		                         int32_t width, int32_t height, int32_t offset, int32_t& skip);

		// translate a map area to client-readable format
		void GetMapDescription(int32_t x, int32_t y, int32_t z,
		                       int32_t width, int32_t height, NetworkMessage& msg);

		void AddCreature(NetworkMessage& msg, const Creature* creature, bool known, uint32_t remove);
		void AddPlayerStats(NetworkMessage& msg);
		void AddOutfit(NetworkMessage& msg, const Outfit_t& outfit);
		void AddPlayerSkills(NetworkMessage& msg);
		void AddWorldLight(NetworkMessage& msg, LightInfo lightInfo);
		void AddCreatureLight(NetworkMessage& msg, const Creature* creature);

		//tiles
		static void RemoveTileThing(NetworkMessage& msg, const Position& pos, uint32_t stackpos);
		static void RemoveTileCreature(NetworkMessage& msg, const Creature* creature, const Position& pos, uint32_t stackpos);

		void MoveUpCreature(NetworkMessage& msg, const Creature* creature, const Position& newPos, const Position& oldPos);
		void MoveDownCreature(NetworkMessage& msg, const Creature* creature, const Position& newPos, const Position& oldPos);

		//shop
		void AddShopItem(NetworkMessage& msg, const ShopInfo& item);
		void AddAnyValue(NetworkMessage& msg, BattlePassQuests_t id, const std::any& value);

		//otclient
		void parseExtendedOpcode(NetworkMessage& msg);

		void sendFloorDescription(const Position& pos, int floor);
		void parseChangeAwareRange(NetworkMessage& msg);
		void updateAwareRange(int width, int height);
		void sendAwareRange();

		friend class Player;

		// Helpers so we don't need to bind every time
		template <typename Callable, typename... Args>
		void addGameTaskWithStats(Callable&& function, const std::string& function_str, const std::string& extra_info, Args&&... args) {
			g_dispatcher.addTask(createTaskWithStats(std::bind(std::forward<Callable>(function), &g_game, std::forward<Args>(args)...), function_str, extra_info));
		}

		template <typename Callable, typename... Args>
		void addGameTaskTimedWithStats(uint32_t delay, Callable&& function, const std::string& function_str, const std::string& extra_info, Args&&... args) {
			g_dispatcher.addTask(createTaskWithStats(delay, std::bind(std::forward<Callable>(function), &g_game, std::forward<Args>(args)...), function_str, extra_info));
		}

		std::unordered_set<uint32_t> knownCreatureSet;
		Player* player = nullptr;

		uint32_t eventConnect = 0;
		uint16_t version = CLIENT_VERSION_MIN;
		uint16_t otclientV8 = 0;
		struct AwareRange {
			int width = 17;
			int height = 13;
			int left() const { return width / 2; }
			int right() const { return 1 + width / 2; }
			int top() const { return height / 2; }
			int bottom() const { return 1 + height / 2; }
			int horizontal() const { return width + 1; }
			int vertical() const { return height + 1; }
		} awareRange;

		bool debugAssertSent = false;
		bool acceptPackets = false;

		void parseNewWalking(NetworkMessage& msg);
		void checkPredictiveWalking(const Position& pos);
		void sendPredictiveCancel(const Position& pos, int value);
		void sendWalkId();
		void sendNewCancelWalk();

		uint32_t walkId = 0;
		WalkMatrix walkMatrix;
};

#endif
