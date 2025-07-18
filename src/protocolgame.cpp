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

#include "otpch.h"

#include <boost/range/adaptor/reversed.hpp>

#include "protocolgame.h"

#include "outputmessage.h"

#include "player.h"

#include "configmanager.h"
#include "actions.h"
#include "game.h"
#include "iologindata.h"
#include "ban.h"
#include "scheduler.h"
#include "logger.h"

#include <fmt/format.h>

extern ConfigManager g_config;
extern Actions actions;
extern CreatureEvents* g_creatureEvents;
extern Chat* g_chat;

namespace {

using WaitList = std::deque<std::pair<int64_t, uint32_t>>; // (timeout, player guid)

WaitList priorityWaitList, waitList;

std::tuple<WaitList&, WaitList::iterator, WaitList::size_type> findClient(const Player& player) {
	const auto fn = [&](const WaitList::value_type& it) { return it.second == player.getGUID(); };

	auto it = std::find_if(priorityWaitList.begin(), priorityWaitList.end(), fn);
	if (it != priorityWaitList.end()) {
		return std::make_tuple(std::ref(priorityWaitList), it, std::distance(it, priorityWaitList.end()) + 1);
	}

	it = std::find_if(waitList.begin(), waitList.end(), fn);
	if (it != waitList.end()) {
		return std::make_tuple(std::ref(waitList), it, priorityWaitList.size() + std::distance(it, waitList.end()) + 1);
	}

	return std::make_tuple(std::ref(waitList), waitList.end(), priorityWaitList.size() + waitList.size());
}

uint8_t getWaitTime(std::size_t slot)
{
	if (slot < 5) {
		return 5;
	} else if (slot < 10) {
		return 10;
	} else if (slot < 20) {
		return 20;
	} else if (slot < 50) {
		return 60;
	} else {
		return 120;
	}
}

int64_t getTimeout(std::size_t slot)
{
	// timeout is set to 15 seconds longer than expected retry attempt
	return getWaitTime(slot) + 15;
}

void cleanupList(WaitList& list)
{
	int64_t time = OTSYS_TIME();

	auto it = list.begin();
	while (it != list.end()) {
		if (it->first <= time) {
			it = list.erase(it);
		} else {
			++it;
		}
	}
}

std::size_t clientLogin(const Player& player)
{
	// Currentslot = position in wait list, 0 for direct access
	if (player.hasFlag(PlayerFlag_CanAlwaysLogin) || player.getAccountType() >= ACCOUNT_TYPE_GAMEMASTER) {
		return 0;
	}

	cleanupList(priorityWaitList);
	cleanupList(waitList);

	uint32_t maxPlayers = static_cast<uint32_t>(g_config.getNumber(ConfigManager::MAX_PLAYERS));
	if (maxPlayers == 0 || (priorityWaitList.empty() && waitList.empty() && g_game.getPlayersOnline() < maxPlayers)) {
		return 0;
	}

	auto result = findClient(player);
	if (std::get<1>(result) != std::get<0>(result).end()) {
		auto currentSlot = std::get<2>(result);
		// If server has capacity for this client, let him in even though his current slot might be higher than 0.
		if ((g_game.getPlayersOnline() + currentSlot) <= maxPlayers) {
			std::get<0>(result).erase(std::get<1>(result));
			return 0;
		}

		//let them wait a bit longer
		std::get<1>(result)->second = OTSYS_TIME() + (getTimeout(currentSlot) * 1000);
		return currentSlot;
	}

	auto currentSlot = priorityWaitList.size();
	if (player.isPremium()) {
		priorityWaitList.emplace_back(OTSYS_TIME() + (getTimeout(++currentSlot) * 1000), player.getGUID());
	} else {
		currentSlot += waitList.size();
		waitList.emplace_back(OTSYS_TIME() + (getTimeout(++currentSlot) * 1000), player.getGUID());
	}
	return currentSlot;
}

}

void ProtocolGame::release()
{
	//dispatcher thread
	if (player && player->client == shared_from_this()) {
		player->client.reset();
		player->decrementReferenceCounter();
		player = nullptr;
	}

	OutputMessagePool::getInstance().removeProtocolFromAutosend(shared_from_this());
	Protocol::release();
}

void ProtocolGame::login(const std::string& name, uint32_t accountId, OperatingSystem_t operatingSystem)
{
	// OTCv8 extended opcodes
	if (otclientV8 || operatingSystem >= CLIENTOS_OTCLIENT_LINUX) {
		NetworkMessage opcodeMessage;
		opcodeMessage.addByte(0x32);
		opcodeMessage.addByte(0x00);
		opcodeMessage.add<uint16_t>(0x00);
		writeToOutputBuffer(opcodeMessage);
	}

	if (otclientV8 < static_cast<uint16_t>(g_config.getNumber(ConfigManager::CLIENT_VERSION)))
	{
		disconnectClient("Your client is outdated. Please, download the new client from our website.");
		return;
	}

	//dispatcher thread
	Player* foundPlayer = g_game.getPlayerByName(name);
	if (!foundPlayer || g_config.getBoolean(ConfigManager::ALLOW_CLONES)) {
		player = new Player(getThis());
		player->setName(name);

		player->incrementReferenceCounter();
		player->setID();

		if (!IOLoginData::preloadPlayer(player, name)) {
			disconnectClient("Your character could not be loaded.");
			return;
		}

		if (IOBan::isPlayerNamelocked(player->getGUID())) {
			disconnectClient("Your character has been namelocked for breaking name rules.\nYou need to change your character name to login again.\n\nAccess our website to change your character name.");
			return;
		}

		if (g_game.getGameState() == GAME_STATE_CLOSING && !player->hasFlag(PlayerFlag_CanAlwaysLogin)) {
			disconnectClient("The game is just going down.\nPlease try again later.");
			return;
		}

		if (g_game.getGameState() == GAME_STATE_CLOSED && !player->hasFlag(PlayerFlag_CanAlwaysLogin)) {
			disconnectClient("Server is currently closed.\nPlease try again later.");
			return;
		}

		if (g_config.getBoolean(ConfigManager::ONE_PLAYER_ON_ACCOUNT) && player->getAccountType() < ACCOUNT_TYPE_GAMEMASTER && g_game.getPlayerByAccount(player->getAccount())) {
			disconnectClient("You may only login with one character\nof your account at the same time.");
			return;
		}

		if (!player->hasFlag(PlayerFlag_CannotBeBanned)) {
			BanInfo banInfo;
			if (IOBan::isAccountBanned(accountId, banInfo)) {
				if (banInfo.reason.empty()) {
					banInfo.reason = "(none)";
				}

				if (banInfo.expiresAt > 0) {
					disconnectClient(fmt::format("Your account has been banned until {:s} by {:s}.\n\nReason specified:\n{:s}", formatDateShort(banInfo.expiresAt), banInfo.bannedBy, banInfo.reason));
				} else {
					disconnectClient(fmt::format("Your account has been permanently banned by {:s}.\n\nReason specified:\n{:s}", banInfo.bannedBy, banInfo.reason));
				}
				return;
			}
		}

		if (std::size_t currentSlot = clientLogin(*player)) {
			uint8_t retryTime = getWaitTime(currentSlot);
			auto output = OutputMessagePool::getOutputMessage();
			output->addByte(0x16);
			output->addString(fmt::format("Too many players online.\nYou are at place {:d} on the waiting list.", currentSlot));
			output->addByte(retryTime);
			send(output);
			disconnect();
			return;
		}

		if (!IOLoginData::loadPlayerById(player, player->getGUID())) {
			disconnectClient("Your character could not be loaded.");
			return;
		}

		player->setOperatingSystem(operatingSystem);

		if (!g_game.placeCreature(player, player->getLoginPosition())) {
			disconnectClient("Login failed due to corrupt data.");
			return;
		}

		player->autoOpenContainers();

		if (operatingSystem >= CLIENTOS_OTCLIENT_LINUX) {
			player->registerCreatureEvent("ExtendedOpcode");
		}

		player->lastIP = player->getIP();
		player->lastLoginSaved = std::max<time_t>(time(nullptr), player->lastLoginSaved + 1);
		acceptPackets = true;
	} else {
		if (eventConnect != 0 || !g_config.getBoolean(ConfigManager::REPLACE_KICK_ON_LOGIN) || foundPlayer->isLoggingOut) {
			//Already trying to connect
			disconnectClient("You are already logged in.");
			return;
		}

		foundPlayer->incrementReferenceCounter();
		if (foundPlayer->client) {
			foundPlayer->disconnect();
			foundPlayer->resetIdleTime();
			foundPlayer->lastPing = OTSYS_TIME();
			foundPlayer->isConnecting = true;

			eventConnect = g_scheduler.addEvent(createSchedulerTask(1000, std::bind(&ProtocolGame::connect, getThis(), foundPlayer->getID(), operatingSystem)));
		} else {
			connect(foundPlayer->getID(), operatingSystem);
		}
	}
	OutputMessagePool::getInstance().addProtocolToAutosend(shared_from_this());
}

void ProtocolGame::connect(uint32_t playerId, OperatingSystem_t operatingSystem)
{
	eventConnect = 0;

	Player* foundPlayer = g_game.getPlayerByID(playerId);
	if (!foundPlayer || foundPlayer->client || foundPlayer->isLoggingOut) {
		disconnectClient("You are already logged in.");
		return;
	}

	foundPlayer->decrementReferenceCounter();

	if (isConnectionExpired()) {
		//ProtocolGame::release() has been called at this point and the Connection object
		//no longer exists, so we return to prevent leakage of the Player.
		return;
	}

	player = foundPlayer;
	player->incrementReferenceCounter();

	g_chat->removeUserFromAllChannels(*player);
	player->setOperatingSystem(operatingSystem);
	player->isConnecting = false;

	player->lastPing = OTSYS_TIME();
	player->resetIdleTime();
	player->client = getThis();
	sendAddCreature(player, player->getPosition(), 0);
	player->autoOpenContainers();
	player->lastIP = player->getIP();
	player->lastLoginSaved = std::max<time_t>(time(nullptr), player->lastLoginSaved + 1);
	acceptPackets = true;
}

void ProtocolGame::logout(bool forced)
{
	//dispatcher thread
	if (!player || player->isLoggingOut) {
		return;
	}

	if (!player->isRemoved()) {
		if (!forced) {
			if (!player->isAccessPlayer()) {
				if (player->getTile()->hasFlag(TILESTATE_NOLOGOUT)) {
					player->sendCancelMessage(RETURNVALUE_YOUCANNOTLOGOUTHERE);
					return;
				}

				if (player->hasCondition(CONDITION_INFIGHT)) {
					player->sendCancelMessage(RETURNVALUE_YOUMAYNOTLOGOUTDURINGAFIGHT);
					return;
				}
			}

			//scripting event - onLogout
			if (!g_creatureEvents->playerLogout(player)) {
				//Let the script handle the error message
				return;
			}
		}

		player->isLoggingOut = true;
		g_game.executeRemoveCreature(player);
	} else {
		disconnect();
	}
}

void ProtocolGame::onRecvFirstMessage(NetworkMessage& msg)
{
	if (g_game.isIPLocked(getIP())) {
		disconnectClient(g_config.getString(ConfigManager::IP_LOCK_MESSAGE));
		return;
	}
	if (g_game.getGameState() == GAME_STATE_SHUTDOWN) {
		disconnect();
		return;
	}

	OperatingSystem_t operatingSystem = static_cast<OperatingSystem_t>(msg.get<uint16_t>());
	version = msg.get<uint16_t>();

	if (!Protocol::RSA_decrypt(msg)) {
		disconnect();
		return;
	}

	xtea::key key;
	key[0] = msg.get<uint32_t>();
	key[1] = msg.get<uint32_t>();
	key[2] = msg.get<uint32_t>();
	key[3] = msg.get<uint32_t>();
	enableXTEAEncryption();
	setXTEAKey(std::move(key));

	if (operatingSystem >= CLIENTOS_OTCLIENT_LINUX) {
		NetworkMessage opcodeMessage;
		opcodeMessage.addByte(0x32);
		opcodeMessage.addByte(0x00);
		opcodeMessage.add<uint16_t>(0x00);
		writeToOutputBuffer(opcodeMessage);
	}

	msg.skipBytes(1); // gamemaster flag
	uint32_t accountNumber = msg.get<uint32_t>();
	std::string character = msg.getString();
	std::string password = msg.getString();

	uint16_t otcv8StringLength = msg.get<uint16_t>();
	if (otcv8StringLength == 5 && msg.getString(5) == "OTCv8") {
		otclientV8 = msg.get<uint16_t>();
	}

	if (accountNumber == 0) {
		disconnectClient("You must enter your account number.");
		return;
	}
	
	if (version < CLIENT_VERSION_MIN || version > CLIENT_VERSION_MAX) {
		disconnectClient(fmt::format("Only clients with protocol {:s} allowed!", CLIENT_VERSION_STR));
		return;
	}

	if (g_game.getGameState() == GAME_STATE_STARTUP) {
		disconnectClient("Gameworld is starting up. Please wait.");
		return;
	}

	if (g_game.getGameState() == GAME_STATE_MAINTAIN) {
		disconnectClient("Gameworld is under maintenance. Please re-connect in a while.");
		return;
	}

	if (g_game.isAccountLocked(accountNumber)) {
		disconnectClient(g_config.getString(ConfigManager::ACCOUNT_LOCK_MESSAGE));
		return;
	}

	BanInfo banInfo;
	if (IOBan::isIpBanned(getIP(), banInfo)) {
		if (banInfo.reason.empty()) {
			banInfo.reason = "(none)";
		}

		if (!banInfo.bannedBy.empty()) {
			disconnectClient(fmt::format("Your IP has been banned until {:s} by {:s}.\n\nReason specified:\n{:s}", formatDateShort(banInfo.expiresAt), banInfo.bannedBy, banInfo.reason));
		}
		else {
			disconnectClient(g_config.getString(ConfigManager::IP_LOCK_MESSAGE));
		}
		return;
	}

	uint32_t accountId = IOLoginData::gameworldAuthentication(accountNumber, password, character);
	if (accountId == 0) {
    g_game.registerFailedIPLogin(getIP());
		g_game.registerFailedAccountLogin(accountNumber);

		disconnectClient("Please enter a valid account number and password.");
		return;
	}

	g_game.resetAccountLoginAttempts(accountNumber);
	g_game.resetIpLoginAttempts(getIP());

	g_dispatcher.addTask(createTask(std::bind(&ProtocolGame::login, getThis(), character, accountId, operatingSystem)));
}

void ProtocolGame::disconnectClient(const std::string& message) const
{
	auto output = OutputMessagePool::getOutputMessage();
	output->addByte(0x14);
	output->addString(message);
	send(output);
	disconnect();
}

void ProtocolGame::writeToOutputBuffer(const NetworkMessage& msg)
{
	auto out = getOutputBuffer(msg.getLength());
	out->append(msg);
}

void ProtocolGame::parsePacket(NetworkMessage& msg)
{
	g_dispatcher.addTask(createTask(std::bind(&ProtocolGame::parsePacketOnDispatcher, this, std::move(msg))));
}

void ProtocolGame::parsePacketOnDispatcher(NetworkMessage msg)
{
	if (!acceptPackets || g_game.getGameState() == GAME_STATE_SHUTDOWN || msg.getLength() == 0) {
		return;
	}

	uint8_t recvbyte = msg.getByte();

	if (!player) {
		if (recvbyte == 0x0F) {
			disconnect();
		}

		return;
	}

	//a dead player can not performs actions
	if (player->isRemoved() || player->getHealth() <= 0) {
		if (recvbyte == 0x0F) {
			disconnect();
			return;
		}

		if (recvbyte != 0x14) {
			return;
		}
	}

	switch (recvbyte) {
	case 0x45: parseNewWalking(msg); break;
	case 0x14: logout(false); break;
	case 0x1D: g_game.playerReceivePingBack(player->getID()); break;
	case 0x1E: g_game.playerReceivePing(player->getID()); break;
	case 0x32: parseExtendedOpcode(msg); break; //otclient extended opcode
	case 0x42: parseChangeAwareRange(msg); break;
	case 0x64: parseAutoWalk(msg); break;
	case 0x65: g_game.playerMove(player->getID(), DIRECTION_NORTH); break;
	case 0x66: g_game.playerMove(player->getID(), DIRECTION_EAST); break;
	case 0x67: g_game.playerMove(player->getID(), DIRECTION_SOUTH); break;
	case 0x68: g_game.playerMove(player->getID(), DIRECTION_WEST); break;
	case 0x69: g_game.playerStopAutoWalk(player->getID()); break;
	case 0x6A: g_game.playerMove(player->getID(), DIRECTION_NORTHEAST); break;
	case 0x6B: g_game.playerMove(player->getID(), DIRECTION_SOUTHEAST); break;
	case 0x6C: g_game.playerMove(player->getID(), DIRECTION_SOUTHWEST); break;
	case 0x6D: g_game.playerMove(player->getID(), DIRECTION_NORTHWEST); break;
	case 0x6F: g_game.playerTurn(player->getID(), DIRECTION_NORTH); break;
	case 0x70: g_game.playerTurn(player->getID(), DIRECTION_EAST); break;
	case 0x71: g_game.playerTurn(player->getID(), DIRECTION_SOUTH); break;
	case 0x72: g_game.playerTurn(player->getID(), DIRECTION_WEST); break;
	case 0x78: parseThrow(msg); break;
	case 0x79: parseLookInShop(msg); break;
	case 0x7A: parsePlayerPurchase(msg); break;
	case 0x7B: parsePlayerSale(msg); break;
	case 0x7C: addGameTask(&Game::playerCloseShop, player->getID()); break;
	case 0x7D: parseRequestTrade(msg); break;
	case 0x7E: parseLookInTrade(msg); break;
	case 0x7F: g_game.playerAcceptTrade(player->getID()); break;
	case 0x80: g_game.playerCloseTrade(player->getID()); break;
	case 0x82: parseUseItem(msg); break;
	case 0x83: parseUseItemEx(msg); break;
	case 0x84: parseUseWithCreature(msg); break;
	case 0x85: parseRotateItem(msg); break;
	case 0x87: parseCloseContainer(msg); break;
	case 0x88: parseUpArrowContainer(msg); break;
	case 0x89: parseTextWindow(msg); break;
	case 0x8A: parseHouseWindow(msg); break;
	case 0x8C: parseLookAt(msg); break;
	case 0x8D: parseLookInBattleList(msg); break;
	case 0x96: parseSay(msg); break;
	case 0x97: g_game.playerRequestChannels(player->getID()); break;
	case 0x98: parseOpenChannel(msg); break;
	case 0x99: parseCloseChannel(msg); break;
	case 0x9A: parseOpenPrivateChannel(msg); break;
	case 0x9B: parseProcessRuleViolationReport(msg); break;
	case 0x9C: parseCloseRuleViolationReport(msg); break;
	case 0x9D: addGameTask(&Game::playerCancelRuleViolationReport, player->getID()); break;
	case 0xA0: parseFightModes(msg); break;
	case 0xA1: parseAttack(msg); break;
	case 0xA2: parseFollow(msg); break;
	case 0xA3: parseInviteToParty(msg); break;
	case 0xA4: parseJoinParty(msg); break;
	case 0xA5: parseRevokePartyInvite(msg); break;
	case 0xA6: parsePassPartyLeadership(msg); break;
	case 0xA7: g_game.playerLeaveParty(player->getID()); break;
	case 0xA8: parseEnableSharedPartyExperience(msg); break;
	case 0xAA: g_game.playerCreatePrivateChannel(player->getID()); break;
	case 0xAB: parseChannelInvite(msg); break;
	case 0xAC: parseChannelExclude(msg); break;
	case 0xBE: g_game.playerCancelAttackAndFollow(player->getID()); break;
	case 0xC9: /* update tile */ break;
	case 0xCA: parseUpdateContainer(msg); break;
	case 0xD2: g_game.playerRequestOutfit(player->getID()); break;
	case 0xD3: parseSetOutfit(msg); break;
	case 0xDC: parseAddVip(msg); break;
	case 0xDD: parseRemoveVip(msg); break;
	case 0xE6: parseBugReport(msg); break;
	case 0xE8: parseDebugAssert(msg); break;
	case 0x61: parseBattlepass(msg); break;

	default:
		g_logger.gameLog(spdlog::level::info, fmt::format("{:s} sent an unknown packet type: {:d}", player->getName(), static_cast<uint16_t>(recvbyte)));
		break;
	}

	if (msg.isOverrun()) {
		disconnect();
	}
}

void ProtocolGame::GetTileDescription(const Tile* tile, NetworkMessage& msg)
{
	Item* ground = tile->getGround();

	if (otclientV8) { // OTCLIENTV8 new walking
		uint16_t groundSpeed = 150;
		// I've noticed that sometimes ground speed was incorrect in otclient,
		// so server just sends 100% correct ground speed
		bool isBlocking = false;
		// if isBlocking is true, next prewalking from this tile will be impossible
		// till server confirmation/rejection, used for teleport or stairs
		if (ground) {
			groundSpeed = (uint16_t)Item::items[ground->getID()].speed;
			if (groundSpeed == 0) {
				groundSpeed = 150;
			}
			// you can uncomment to make walk animation slower when going into stairs, looks cool
			// floor change speed is the same, only animation is slower 
			//if(tile->hasFlag(TILESTATE_FLOORCHANGE))
			//    groundSpeed *= 3;
			if (tile->hasFlag(TILESTATE_FLOORCHANGE) || tile->hasFlag(TILESTATE_TELEPORT))
				isBlocking = true;
		}
		msg.add<uint16_t>(groundSpeed);
		msg.add<uint8_t>(isBlocking ? 1 : 0);
	} else {
		//msg.add<uint16_t>(0x00); //environmental effects
	}

	int32_t count;
	if (ground) {
		msg.addItem(ground);
		count = 1;
	} else {
		count = 0;
	}

	const TileItemVector* items = tile->getItemList();
	if (items) {
		for (auto it = items->getBeginTopItem(), end = items->getEndTopItem(); it != end; ++it) {
			msg.addItem(*it);

			count++;
			if (count == 9 && tile->getPosition() == player->getPosition() && !otclientV8) {
				break;
			} else if (count == 10) {
				return;
			}
		}
	}

	const CreatureVector* creatures = tile->getCreatures();
	if (creatures) {
		bool playerAdded = false;
		for (const Creature* creature : boost::adaptors::reverse(*creatures)) {
			if (!player->canSeeCreature(creature)) {
				continue;
			}

			if (tile->getPosition() == player->getPosition() && count == 9 && !playerAdded && !otclientV8) {
				creature = player;
			}

			if (creature->getID() == player->getID()) {
				playerAdded = true;
			}

			bool known;
			uint32_t removedKnown;
			checkCreatureAsKnown(creature->getID(), known, removedKnown);
			AddCreature(msg, creature, known, removedKnown);

			if (++count == 10) {
				return;
			}
		}
	}

	if (items && count < 10) {
		for (auto it = items->getBeginDownItem(), end = items->getEndDownItem(); it != end; ++it) {
			msg.addItem(*it);

			if (++count == 10) {
				return;
			}
		}
	}
}

void ProtocolGame::GetMapDescription(int32_t x, int32_t y, int32_t z, int32_t width, int32_t height, NetworkMessage& msg)
{
	int32_t skip = -1;
	int32_t startz, endz, zstep;

	if (z > 7) {
		startz = z - 2;
		endz = std::min<int32_t>(MAP_MAX_LAYERS - 1, z + 2);
		zstep = 1;
	} else {
		startz = 7;
		endz = 0;
		zstep = -1;
	}

	for (int32_t nz = startz; nz != endz + zstep; nz += zstep) {
		GetFloorDescription(msg, x, y, nz, width, height, z - nz, skip);
	}

	if (skip >= 0) {
		msg.addByte(skip);
		msg.addByte(0xFF);
	}
}

void ProtocolGame::GetFloorDescription(NetworkMessage& msg, int32_t x, int32_t y, int32_t z, int32_t width, int32_t height, int32_t offset, int32_t& skip)
{
	for (int32_t nx = 0; nx < width; nx++) {
		for (int32_t ny = 0; ny < height; ny++) {
			Tile* tile = g_game.map.getTile(x + nx + offset, y + ny + offset, z);
			if (tile) {
				if (g_config.getBoolean(ConfigManager::ENABLE_MAP_REFRESH)) {
					tile->updateRefreshTime();
				}

				if (skip >= 0) {
					msg.addByte(skip);
					msg.addByte(0xFF);
				}

				skip = 0;
				GetTileDescription(tile, msg);
			} else if (skip == 0xFE) {
				msg.addByte(0xFF);
				msg.addByte(0xFF);
				skip = -1;
			} else {
				++skip;
			}
		}
	}
}

void ProtocolGame::checkCreatureAsKnown(uint32_t id, bool& known, uint32_t& removedKnown)
{
	auto result = knownCreatureSet.insert(id);
	if (!result.second) {
		known = true;
		return;
	}

	known = false;

	if (knownCreatureSet.size() > 150) {
		// Look for a creature to remove
		for (auto it = knownCreatureSet.begin(), end = knownCreatureSet.end(); it != end; ++it) {
			Creature* creature = g_game.getCreatureByID(*it);
			if (!canSee(creature)) {
				removedKnown = *it;
				knownCreatureSet.erase(it);
				return;
			}
		}

		// Bad situation. Let's just remove anyone.
		auto it = knownCreatureSet.begin();
		if (*it == id) {
			++it;
		}

		removedKnown = *it;
		knownCreatureSet.erase(it);
	} else {
		removedKnown = 0;
	}
}

bool ProtocolGame::canSee(const Creature* c) const
{
	if (!c || !player || c->isRemoved()) {
		return false;
	}

	if (!player->canSeeCreature(c)) {
		return false;
	}

	return canSee(c->getPosition());
}

bool ProtocolGame::canSee(const Position& pos) const
{
	return canSee(pos.x, pos.y, pos.z);
}

bool ProtocolGame::canSee(int32_t x, int32_t y, int32_t z) const
{
	if (!player) {
		return false;
	}

	const Position& myPos = player->getPosition();
	if (myPos.z <= 7) {
		//we are on ground level or above (7 -> 0)
		//view is from 7 -> 0
		if (z > 7) {
			return false;
		}
	} else { // if (myPos.z >= 8) {
		//we are underground (8 -> 15)
		//view is +/- 2 from the floor we stand on
		if (std::abs(myPos.getZ() - z) > 2) {
			return false;
		}
	}

	//negative offset means that the action taken place is on a lower floor than ourself
	int32_t offsetz = myPos.getZ() - z;
	if ((x >= myPos.getX() - awareRange.left() + offsetz) && (x <= myPos.getX() + awareRange.right() + offsetz) &&
		(y >= myPos.getY() - awareRange.top() + offsetz) && (y <= myPos.getY() + awareRange.bottom() + offsetz)) {
		return true;
	}
	return false;
}

// Parse methods
void ProtocolGame::parseChannelInvite(NetworkMessage& msg)
{
	const std::string name = msg.getString();
	g_game.playerChannelInvite(player->getID(), name);
}

void ProtocolGame::parseChannelExclude(NetworkMessage& msg)
{
	const std::string name = msg.getString();
	g_game.playerChannelExclude(player->getID(), name);
}

void ProtocolGame::parseOpenChannel(NetworkMessage& msg)
{
	uint16_t channelId = msg.get<uint16_t>();
	g_game.playerOpenChannel(player->getID(), channelId);
}

void ProtocolGame::parseCloseChannel(NetworkMessage& msg)
{
	uint16_t channelId = msg.get<uint16_t>();
	g_game.playerCloseChannel(player->getID(), channelId);
}

void ProtocolGame::parseOpenPrivateChannel(NetworkMessage& msg)
{
	std::string receiver = msg.getString();
	g_game.playerOpenPrivateChannel(player->getID(), receiver);
}

void ProtocolGame::parseAutoWalk(NetworkMessage& msg)
{
	uint8_t numdirs = msg.getByte();
	if (numdirs == 0 || (msg.getBufferPosition() + numdirs) != (msg.getLength() + 4) || numdirs > 128) {
		return;
	}

	msg.skipBytes(numdirs);

	std::vector<Direction> path;
	path.reserve(numdirs);

	for (uint8_t i = 0; i < numdirs; ++i) {
		uint8_t rawdir = msg.getPreviousByte();
		switch (rawdir) {
			case 1: path.push_back(DIRECTION_EAST); break;
			case 2: path.push_back(DIRECTION_NORTHEAST); break;
			case 3: path.push_back(DIRECTION_NORTH); break;
			case 4: path.push_back(DIRECTION_NORTHWEST); break;
			case 5: path.push_back(DIRECTION_WEST); break;
			case 6: path.push_back(DIRECTION_SOUTHWEST); break;
			case 7: path.push_back(DIRECTION_SOUTH); break;
			case 8: path.push_back(DIRECTION_SOUTHEAST); break;
			default: break;
		}
	}

	if (path.empty()) {
		return;
	}

	std::reverse(path.begin(), path.end());
	g_game.playerAutoWalk(player->getID(), path);
}

void ProtocolGame::parseSetOutfit(NetworkMessage& msg)
{
	Outfit_t newOutfit;
	newOutfit.lookType = msg.get<uint16_t>();
	newOutfit.lookHead = msg.getByte();
	newOutfit.lookBody = msg.getByte();
	newOutfit.lookLegs = msg.getByte();
	newOutfit.lookFeet = msg.getByte();
	newOutfit.lookAddons = msg.getByte();
	newOutfit.lookWings = otclientV8 ? msg.get<uint16_t>() : 0;
	newOutfit.lookAura = otclientV8 ? msg.get<uint16_t>() : 0;

	std::string shaderName = otclientV8 ? msg.getString() : "";
	Shader* shader = g_game.shaders.getShaderByName(shaderName);
	newOutfit.lookShader = shader ? shader->id : 0;

	g_game.playerChangeOutfit(player->getID(), newOutfit);
}

void ProtocolGame::parseUseItem(NetworkMessage& msg)
{
	Position pos = msg.getPosition();
	uint16_t spriteId = msg.get<uint16_t>();
	uint8_t stackpos = msg.getByte();
	uint8_t index = msg.getByte();

	if (otclientV8) {
		g_game.playerUseItem(player->getID(), pos, stackpos, index, spriteId);
	} else {
		player->addWaitToDo(g_config.getNumber(ConfigManager::ACTIONS_DELAY_INTERVAL));
		player->addActionToDo(std::bind(&Game::playerUseItem, &g_game, player->getID(), pos, stackpos, index, spriteId));
		player->startToDo();
	}
}

void ProtocolGame::parseUseItemEx(NetworkMessage& msg)
{
	Position fromPos = msg.getPosition();
	uint16_t fromSpriteId = msg.get<uint16_t>();
	uint8_t fromStackPos = msg.getByte();
	Position toPos = msg.getPosition();
	uint16_t toSpriteId = msg.get<uint16_t>();
	uint8_t toStackPos = msg.getByte();

	// We cannot make the needed changes for network & dispatcher for this system in particular.
	// What we are doing here is that if the player attempts to use an item
	// And they are therefore, able to use items within "cip next game round"
	// Allow them on purpose.

	if (player->earliestMultiUseTime <= OTSYS_TIME() && player->toDoEntries.size() <= 2 && player->earliestWalkTime <= OTSYS_TIME() + (g_config.getNumber(ConfigManager::ACTIONS_DELAY_INTERVAL) * 2)) {
		g_game.playerUseItemEx(player->getID(), fromPos, fromStackPos, fromSpriteId, toPos, toStackPos, toSpriteId);
	} else {
		player->addWaitToDo(g_config.getNumber(ConfigManager::ACTIONS_DELAY_INTERVAL));
		player->addActionToDo(TODO_USEEX, std::bind(&Game::playerUseItemEx, &g_game, player->getID(), fromPos, fromStackPos, fromSpriteId, toPos, toStackPos, toSpriteId));
		player->startToDo();
	}
}

void ProtocolGame::parseUseWithCreature(NetworkMessage& msg)
{
	Position fromPos = msg.getPosition();
	uint16_t spriteId = msg.get<uint16_t>();
	uint8_t fromStackPos = msg.getByte();
	uint32_t creatureId = msg.get<uint32_t>();

	// We cannot make the needed changes for network & dispatcher for this system in particular.
	// What we are doing here is that if the player attempts to use an item
	// And they are therefore, able to use items within "cip next game round"
	// Allow them on purpose.

	if (player->earliestMultiUseTime <= OTSYS_TIME() && player->toDoEntries.size() <= 2 && player->earliestWalkTime <= OTSYS_TIME() + (g_config.getNumber(ConfigManager::ACTIONS_DELAY_INTERVAL) * 2)) {
		g_game.playerUseWithCreature(player->getID(), fromPos, fromStackPos, creatureId, spriteId);
	} else {
		player->addWaitToDo(g_config.getNumber(ConfigManager::ACTIONS_DELAY_INTERVAL));
		player->addActionToDo(TODO_USEEX, std::bind(&Game::playerUseWithCreature, &g_game, player->getID(), fromPos, fromStackPos, creatureId, spriteId));
		player->startToDo();
	}
}

void ProtocolGame::parseCloseContainer(NetworkMessage& msg)
{
	uint8_t cid = msg.getByte();
	g_game.playerCloseContainer(player->getID(), cid);
}

void ProtocolGame::parseUpArrowContainer(NetworkMessage& msg)
{
	uint8_t cid = msg.getByte();
	g_game.playerMoveUpContainer(player->getID(), cid);
}

void ProtocolGame::parseUpdateContainer(NetworkMessage& msg)
{
	uint8_t cid = msg.getByte();
	g_game.playerUpdateContainer(player->getID(), cid);
}

void ProtocolGame::parseThrow(NetworkMessage& msg)
{
	Position fromPos = msg.getPosition();
	uint16_t spriteId = msg.get<uint16_t>();
	uint8_t fromStackpos = msg.getByte();
	Position toPos = msg.getPosition();
	uint8_t count = msg.getByte();

	if (toPos != fromPos) {
		if (otclientV8) {
			g_game.playerMoveThing(player->getID(), fromPos, spriteId, fromStackpos, toPos, count);
		} else {
			if (toPos.x != 0xFFFF) {
				g_scheduler.addEvent(createSchedulerTask(100, std::bind(&Game::playerMoveThing, &g_game, player->getID(), fromPos, spriteId, fromStackpos, toPos, count)));
			} else {
				if (spriteId != 99) {
					player->addWaitToDo(100);
				}

				player->addActionToDo(std::bind(&Game::playerMoveThing, &g_game, player->getID(), fromPos, spriteId, fromStackpos, toPos, count));
				player->startToDo();
			}
		}
	}
}

void ProtocolGame::parseLookAt(NetworkMessage& msg)
{
	Position pos = msg.getPosition();
	msg.skipBytes(2); // spriteId
	uint8_t stackpos = msg.getByte();
	g_game.playerLookAt(player->getID(), pos, stackpos);
}

void ProtocolGame::parseLookInBattleList(NetworkMessage& msg)
{
	uint32_t creatureId = msg.get<uint32_t>();
	g_game.playerLookInBattleList(player->getID(), creatureId);
}

void ProtocolGame::parseSay(NetworkMessage& msg)
{
	std::string receiver;
	uint16_t channelId;

	SpeakClasses type = static_cast<SpeakClasses>(msg.getByte());
	switch (type) {
		case TALKTYPE_PRIVATE:
		case TALKTYPE_PRIVATE_RED:
		case TALKTYPE_RVR_ANSWER:
			receiver = msg.getString();
			channelId = 0;
			break;

		case TALKTYPE_CHANNEL_Y:
		case TALKTYPE_CHANNEL_R1:
			channelId = msg.get<uint16_t>();
			break;

		default:
			channelId = 0;
			break;
	}

	const std::string text = msg.getString();
	if (text.length() > 255) {
		g_logger.chatLog(spdlog::level::info, fmt::format("[ProtocolGame::parseSay] {:s} attempted to talk over 255 chars -> {:s}.", player->getName(), text));
		return;
	}

	// OTC does not automatically schedule auto walking upon talking, only real Tibia client does
	if (otclientV8) {
		g_game.playerSay(player->getID(), channelId, type, receiver, text);
	} else {
		if (player->isExecuting && player->clearToDo()) {
			sendCancelWalk();
		}

		player->addActionToDo(std::bind(&Game::playerSay, &g_game, player->getID(), channelId, type, receiver, text));
		player->startToDo();
	}
}

void ProtocolGame::parseFightModes(NetworkMessage& msg)
{
	uint8_t rawFightMode = msg.getByte(); // 1 - offensive, 2 - balanced, 3 - defensive
	uint8_t rawChaseMode = msg.getByte(); // 0 - stand while fighting, 1 - chase opponent
	uint8_t rawSecureMode = msg.getByte(); // 0 - can't attack unmarked, 1 - can attack unmarked

	fightMode_t fightMode;
	if (rawFightMode == 1) {
		fightMode = FIGHTMODE_ATTACK;
	} else if (rawFightMode == 2) {
		fightMode = FIGHTMODE_BALANCED;
	} else {
		fightMode = FIGHTMODE_DEFENSE;
	}

	g_game.playerSetFightModes(player->getID(), fightMode, rawChaseMode != 0, rawSecureMode != 0);
}

void ProtocolGame::parseAttack(NetworkMessage& msg)
{
	uint32_t creatureId = msg.get<uint32_t>();
	g_game.playerSetAttackedCreature(player->getID(), creatureId);
}

void ProtocolGame::parseFollow(NetworkMessage& msg)
{
	uint32_t creatureId = msg.get<uint32_t>();
	g_game.playerFollowCreature(player->getID(), creatureId);
}

void ProtocolGame::parseProcessRuleViolationReport(NetworkMessage& msg)
{
	const std::string reporter = msg.getString();
	addGameTask(&Game::playerProcessRuleViolationReport, player->getID(), reporter);
}

void ProtocolGame::parseCloseRuleViolationReport(NetworkMessage& msg)
{
	const std::string reporter = msg.getString();
	addGameTask(&Game::playerCloseRuleViolationReport, player->getID(), reporter);
}

void ProtocolGame::parseTextWindow(NetworkMessage& msg)
{
	uint32_t windowTextId = msg.get<uint32_t>();
	const std::string newText = msg.getString();
	g_game.playerWriteItem(player->getID(), windowTextId, newText);
}

void ProtocolGame::parseHouseWindow(NetworkMessage& msg)
{
	uint8_t doorId = msg.getByte();
	uint32_t id = msg.get<uint32_t>();
	const std::string text = msg.getString();
	g_game.playerUpdateHouseWindow(player->getID(), doorId, id, text);
}

void ProtocolGame::parseLookInShop(NetworkMessage& msg)
{
	uint16_t id = msg.get<uint16_t>();
	uint8_t count = msg.getByte();
	addGameTaskTimed(DISPATCHER_TASK_EXPIRATION, &Game::playerLookInShop, player->getID(), id, count);
}

void ProtocolGame::parsePlayerPurchase(NetworkMessage& msg)
{
	uint16_t id = msg.get<uint16_t>();
	uint8_t count = msg.getByte();
	uint16_t amount = msg.get<uint16_t>(); //INFO: Change to 16U
	bool ignoreCap = msg.getByte() != 0;
	bool inBackpacks = msg.getByte() != 0;
	addGameTaskTimed(DISPATCHER_TASK_EXPIRATION, &Game::playerPurchaseItem, player->getID(), id, count, amount, ignoreCap, inBackpacks);
}

void ProtocolGame::parsePlayerSale(NetworkMessage& msg)
{
	uint16_t id = msg.get<uint16_t>();
	uint8_t count = msg.getByte();
	uint8_t amount = msg.getByte(); //INFO: Change to 16U
	bool ignoreEquipped = msg.getByte() != 0;
	addGameTaskTimed(DISPATCHER_TASK_EXPIRATION, &Game::playerSellItem, player->getID(), id, count, amount, ignoreEquipped);
}

void ProtocolGame::parseRequestTrade(NetworkMessage& msg)
{
	Position pos = msg.getPosition();
	uint16_t spriteId = msg.get<uint16_t>();
	uint8_t stackpos = msg.getByte();
	uint32_t playerId = msg.get<uint32_t>();

	if (otclientV8) {
		g_game.playerRequestTrade(player->getID(), pos, stackpos, playerId, spriteId);
	} else {
		player->addActionToDo(std::bind(&Game::playerRequestTrade, &g_game, player->getID(), pos, stackpos, playerId, spriteId));
		player->startToDo();
	}
}

void ProtocolGame::parseLookInTrade(NetworkMessage& msg)
{
	bool counterOffer = (msg.getByte() == 0x01);
	uint8_t index = msg.getByte();
	g_game.playerLookInTrade(player->getID(), counterOffer, index);
}

void ProtocolGame::parseAddVip(NetworkMessage& msg)
{
	const std::string name = msg.getString();
	g_game.playerRequestAddVip(player->getID(), name);
}

void ProtocolGame::parseRemoveVip(NetworkMessage& msg)
{
	uint32_t guid = msg.get<uint32_t>();
	g_game.playerRequestRemoveVip(player->getID(), guid);
}

void ProtocolGame::parseRotateItem(NetworkMessage& msg)
{
	Position pos = msg.getPosition();
	uint16_t spriteId = msg.get<uint16_t>();
	uint8_t stackpos = msg.getByte();
	
	if (otclientV8) {
		g_game.playerRotateItem(player->getID(), pos, stackpos, spriteId);
	} else {
		player->addActionToDo(std::bind(&Game::playerRotateItem, &g_game, player->getID(), pos, stackpos, spriteId));
		player->startToDo();
	}
}

void ProtocolGame::parseBugReport(NetworkMessage& msg)
{
	std::string message = msg.getString();
	g_game.playerReportBug(player->getID(), message);
}

void ProtocolGame::parseDebugAssert(NetworkMessage& msg)
{
	if (debugAssertSent) {
		return;
	}

	debugAssertSent = true;

	std::string assertLine = msg.getString();
	std::string date = msg.getString();
	std::string description = msg.getString();
	std::string comment = msg.getString();
	g_game.playerDebugAssert(player->getID(), assertLine, date, description, comment);
}

void ProtocolGame::parseInviteToParty(NetworkMessage& msg)
{
	uint32_t targetId = msg.get<uint32_t>();
	g_game.playerInviteToParty(player->getID(), targetId);
}

void ProtocolGame::parseJoinParty(NetworkMessage& msg)
{
	uint32_t targetId = msg.get<uint32_t>();
	g_game.playerJoinParty(player->getID(), targetId);
}

void ProtocolGame::parseRevokePartyInvite(NetworkMessage& msg)
{
	uint32_t targetId = msg.get<uint32_t>();
	g_game.playerRevokePartyInvitation(player->getID(), targetId);
}

void ProtocolGame::parsePassPartyLeadership(NetworkMessage& msg)
{
	uint32_t targetId = msg.get<uint32_t>();
	g_game.playerPassPartyLeadership(player->getID(), targetId);
}

void ProtocolGame::parseEnableSharedPartyExperience(NetworkMessage& msg)
{
	bool sharedExpActive = msg.getByte() == 1;
	g_game.playerEnableSharedPartyExperience(player->getID(), sharedExpActive);
}

// Send methods
void ProtocolGame::sendOpenPrivateChannel(const std::string& receiver)
{
	NetworkMessage msg;
	msg.addByte(0xAD);
	msg.addString(receiver);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureOutfit(const Creature* creature, const Outfit_t& outfit)
{
	if (!canSee(creature)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x8E);
	msg.add<uint32_t>(creature->getID());
	AddOutfit(msg, outfit);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureLight(const Creature* creature)
{
	if (!canSee(creature)) {
		return;
	}

	NetworkMessage msg;
	AddCreatureLight(msg, creature);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendWorldLight(LightInfo lightInfo)
{
	NetworkMessage msg;
	AddWorldLight(msg, lightInfo);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureShield(const Creature* creature)
{
	if (!canSee(creature)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x91);
	msg.add<uint32_t>(creature->getID());
	msg.addByte(player->getPartyShield(creature->getPlayer()));
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureSkull(const Creature* creature)
{
	if (g_game.getWorldType() != WORLD_TYPE_PVP) {
		return;
	}

	if (!canSee(creature)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x90);
	msg.add<uint32_t>(creature->getID());
	msg.addByte(player->getSkullClient(creature));
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureStar(const Creature* creature)
{
	if (!canSee(creature)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x80);
	msg.add<uint32_t>(creature->getID());
	msg.addByte(creature->getStarAppearance());
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureSquare(const Creature* creature, SquareColor_t color)
{
	if (!canSee(creature)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x86);
	msg.add<uint32_t>(creature->getID());
	msg.addByte(color);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendRemoveRuleViolationReport(const std::string& name)
{
	NetworkMessage msg;
	msg.addByte(0xAF);
	msg.addString(name);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendLockRuleViolation()
{
	NetworkMessage msg;
	msg.addByte(0xB1);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendRuleViolationCancel(const std::string& name)
{
	NetworkMessage msg;
	msg.addByte(0xB0);
	msg.addString(name);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendRuleViolationsChannel(uint16_t channelId)
{
	NetworkMessage msg;
	msg.addByte(0xAE);
	msg.add<uint16_t>(channelId);
	auto it = g_game.getRuleViolationReports().begin();
	for (; it != g_game.getRuleViolationReports().end(); ++it) {
		const RuleViolation& rvr = it->second;
		if (rvr.pending) {
			Player* reporter = g_game.getPlayerByID(rvr.reporterId);
			if (reporter) {
				msg.addByte(0xAA);
				msg.add<uint32_t>(0);
				msg.addString(reporter->getName());
				msg.addByte(TALKTYPE_RVR_CHANNEL);
				msg.add<uint32_t>(0);
				msg.addString(rvr.text);
			}
		}
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendStats()
{
	NetworkMessage msg;
	AddPlayerStats(msg);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendTextMessage(const TextMessage& message)
{
	NetworkMessage msg;
	msg.addByte(0xB4);
	msg.addByte(message.type);
	msg.addString(message.text);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendAnimatedText(const Position& pos, uint8_t color, const std::string& text)
{
	NetworkMessage msg;
	msg.addByte(0x84);
	msg.addPosition(pos);
	msg.addByte(color);
	msg.addString("verdana-11px-rounded");
	msg.addString(text);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendAdvancedAnimatedText(const Position& pos, uint8_t color, const std::string& text, const std::string& font)
{
	NetworkMessage msg;
	msg.addByte(0x84);
	msg.addPosition(pos);
	msg.addByte(color);
	msg.addString(font); // "verdana-11px-rounded"
	msg.addString(text);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendClosePrivate(uint16_t channelId)
{
	NetworkMessage msg;
	msg.addByte(0xB3);
	msg.add<uint16_t>(channelId);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatePrivateChannel(uint16_t channelId, const std::string& channelName)
{
	NetworkMessage msg;
	msg.addByte(0xB2);
	msg.add<uint16_t>(channelId);
	msg.addString(channelName);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendChannelsDialog()
{
	NetworkMessage msg;
	msg.addByte(0xAB);

	const ChannelList& list = g_chat->getChannelList(*player);
	msg.addByte(list.size());
	for (ChatChannel* channel : list) {
		msg.add<uint16_t>(channel->getId());
		msg.addString(channel->getName());
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendChannel(uint16_t channelId, const std::string& channelName)
{
	NetworkMessage msg;
	msg.addByte(0xAC);
	msg.add<uint16_t>(channelId);
	msg.addString(channelName);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendChannelMessage(const std::string& author, const std::string& text, SpeakClasses type, uint16_t channel)
{
	NetworkMessage msg;
	msg.addByte(0xAA);
	msg.add<uint32_t>(0x00);
	msg.addString(author);
	msg.addByte(type);
	msg.add<uint16_t>(channel);
	msg.addString(text);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendIcons(uint16_t icons)
{
	NetworkMessage msg;
	msg.addByte(0xA2);
	msg.addByte(static_cast<uint8_t>(icons));
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendShop(Npc* npc, const ShopInfoList& itemList)
{
	NetworkMessage msg;
	msg.addByte(0x7A);
	msg.addString(npc->getName());

	uint8_t itemsToSend = std::min<size_t>(itemList.size(), std::numeric_limits<uint8_t>::max());
	msg.add<uint8_t>(itemsToSend);

	uint8_t i = 0;
	for (auto it = itemList.begin(); i < itemsToSend; ++it, ++i) {
		AddShopItem(msg, *it);
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCloseShop()
{
	NetworkMessage msg;
	msg.addByte(0x7C);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendSaleItemList(const std::list<ShopInfo>& shop, bool isHalloween)
{
	NetworkMessage msg;
	msg.addByte(0x7B);
	if (isHalloween)
		msg.add<uint64_t>(player->getItemTypeCount(ITEM_HALLOWEEN_COIN));
	else
		msg.add<uint64_t>(player->getMoney() + player->getBankBalance());

	std::map<uint16_t, uint32_t> saleMap;

	if (shop.size() <= 5) {
		// For very small shops it's not worth it to create the complete map
		for (const ShopInfo& shopInfo : shop) {
			if (shopInfo.sellPrice == 0) {
				continue;
			}

			int8_t subtype = -1;

			const ItemType& itemType = Item::items[shopInfo.itemId];
			if (itemType.hasSubType() && !itemType.stackable) {
				subtype = (shopInfo.subType == 0 ? -1 : shopInfo.subType);
			}

			uint32_t count = player->getItemTypeCount(shopInfo.itemId, subtype);
			if (count > 0) {
				saleMap[shopInfo.itemId] = count;
			}
		}
	} else {
		// Large shop, it's better to get a cached map of all item counts and use it
		// We need a temporary map since the finished map should only contain items
		// available in the shop
		std::map<uint32_t, uint32_t> tempSaleMap;
		player->getAllItemTypeCount(tempSaleMap);

		// We must still check manually for the special items that require subtype matches
		// (That is, fluids such as potions etc., actually these items are very few since
		// health potions now use their own ID)
		for (const ShopInfo& shopInfo : shop) {
			if (shopInfo.sellPrice == 0) {
				continue;
			}

			int8_t subtype = -1;

			const ItemType& itemType = Item::items[shopInfo.itemId];
			if (itemType.hasSubType() && !itemType.stackable) {
				subtype = (shopInfo.subType == 0 ? -1 : shopInfo.subType);
			}

			if (subtype != -1) {
				uint32_t count;
				if (itemType.isFluidContainer() || itemType.isSplash()) {
					count = player->getItemTypeCount(shopInfo.itemId, subtype); // This shop item requires extra checks
				} else {
					count = subtype;
				}

				if (count > 0) {
					saleMap[shopInfo.itemId] = count;
				}
			} else {
				std::map<uint32_t, uint32_t>::const_iterator findIt = tempSaleMap.find(shopInfo.itemId);
				if (findIt != tempSaleMap.end() && findIt->second > 0) {
					saleMap[shopInfo.itemId] = findIt->second;
				}
			}
		}
	}

	uint8_t itemsToSend = std::min<size_t>(saleMap.size(), std::numeric_limits<uint8_t>::max());
	msg.addByte(itemsToSend);

	uint8_t i = 0;
	for (std::map<uint16_t, uint32_t>::const_iterator it = saleMap.begin(); i < itemsToSend; ++it, ++i) {
		msg.addItemId(it->first);
		msg.addByte(std::min<uint32_t>(it->second, std::numeric_limits<uint8_t>::max()));
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendContainer(uint8_t cid, const Container* container, bool hasParent)
{
	NetworkMessage msg;
	msg.addByte(0x6E);
	msg.addByte(cid);
	msg.addItem(container);
	msg.addString(container->getName());
	msg.addByte(container->capacity());
	msg.addByte(hasParent ? 0x01 : 0x00);

	uint32_t containerSize = container->size();
	uint8_t itemsToSend = std::min<uint32_t>(std::min<uint32_t>(container->capacity(), containerSize), std::numeric_limits<uint8_t>::max());

	if (itemsToSend > 0) {
		msg.addByte(itemsToSend);
		for (auto it = container->getItemList().begin(), end = it + itemsToSend; it != end; ++it) {
			msg.addItem(*it);
		}
	}
	else {
		msg.addByte(0x00);
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendTradeItemRequest(const std::string& traderName, const Item* item, bool ack)
{
	NetworkMessage msg;

	if (ack) {
		msg.addByte(0x7D);
	} else {
		msg.addByte(0x7E);
	}

	msg.addString(traderName);

	if (const Container* tradeContainer = item->getContainer()) {
		std::list<const Container*> listContainer {tradeContainer};
		std::list<const Item*> itemList {tradeContainer};
		while (!listContainer.empty()) {
			const Container* container = listContainer.front();
			listContainer.pop_front();

			for (Item* containerItem : container->getItemList()) {
				Container* tmpContainer = containerItem->getContainer();
				if (tmpContainer) {
					listContainer.push_back(tmpContainer);
				}
				itemList.push_back(containerItem);
			}
		}

		msg.addByte(itemList.size());
		for (const Item* listItem : itemList) {
			msg.addItem(listItem);
		}
	} else {
		msg.addByte(0x01);
		msg.addItem(item);
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCloseTrade()
{
	NetworkMessage msg;
	msg.addByte(0x7F);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCloseContainer(uint8_t cid)
{
	NetworkMessage msg;
	msg.addByte(0x6F);
	msg.addByte(cid);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureTurn(const Creature* creature, uint32_t stackPos)
{
	if (!canSee(creature) || stackPos >= 10) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x6B);
	msg.addPosition(creature->getPosition());
	msg.addByte(stackPos);
	msg.add<uint16_t>(0x63);
	msg.add<uint32_t>(creature->getID());
	msg.addByte(creature->getDirection());
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureSay(const Creature* creature, SpeakClasses type, const std::string& text, const Position* pos/* = nullptr*/)
{
	NetworkMessage msg;
	msg.addByte(0xAA);

	static uint32_t statementId = 0;
	msg.add<uint32_t>(++statementId);
	msg.addString(creature->getName());
	msg.addByte(type);

	if (pos) {
		msg.addPosition(*pos);
	} else {
		msg.addPosition(creature->getPosition());
	}

	msg.addString(text);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendToChannel(const Creature* creature, SpeakClasses type, const std::string& text, uint16_t channelId)
{
	NetworkMessage msg;
	msg.addByte(0xAA);

	static uint32_t statementId = 0;
	msg.add<uint32_t>(++statementId);
	if (!creature) {
		msg.add<uint32_t>(0x00);
	} else {
		msg.addString(creature->getName());
	}

	msg.addByte(type);
	if (channelId == CHANNEL_RULE_REP) {
		msg.add<uint32_t>(std::time(nullptr));
	} else {
		msg.add<uint16_t>(channelId);
	}
	msg.addString(text);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendPrivateMessage(const Player* speaker, SpeakClasses type, const std::string& text)
{
	NetworkMessage msg;
	msg.addByte(0xAA);
	static uint32_t statementId = 0;
	msg.add<uint32_t>(++statementId);
	if (speaker) {
			if (type == TALKTYPE_RVR_ANSWER) {
			msg.addString("Gamemaster");
		} else {
			msg.addString(speaker->getName());
		}
	} else {
		msg.add<uint16_t>(0x00);
	}
	msg.addByte(type);
	msg.addString(text);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCancelTarget()
{
	NetworkMessage msg;
	msg.addByte(0xA3);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendChangeSpeed(const Creature* creature, uint32_t speed)
{
	NetworkMessage msg;
	msg.addByte(0x8F);
	msg.add<uint32_t>(creature->getID());
	msg.add<uint16_t>(speed);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCancelWalk()
{
	NetworkMessage msg;
	msg.addByte(0xB5);
	msg.addByte(player->getDirection());
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendSkills()
{
	NetworkMessage msg;
	AddPlayerSkills(msg);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendPing()
{
	NetworkMessage msg;
	if (player->getOperatingSystem() >= CLIENTOS_OTCLIENT_LINUX) {
		msg.addByte(0x1D);
	}
	else {
		msg.addByte(0x1E);
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendPingBack()
{
	NetworkMessage msg;
	msg.addByte(0x1E);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendDistanceShoot(const Position& from, const Position& to, uint8_t type)
{
	NetworkMessage msg;
	msg.addByte(0x85);
	msg.addPosition(from);
	msg.addPosition(to);
	msg.addByte(type);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendMagicEffect(const Position& pos, uint8_t type)
{
	if (!canSee(pos)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x83);
	msg.addPosition(pos);
	msg.addByte(type);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureHealth(const Creature* creature)
{
	NetworkMessage msg;
	msg.addByte(0x8C);
	msg.add<uint32_t>(creature->getID());

	if (creature->isHealthHidden()) {
		msg.addByte(0x00);
	} else {
		msg.addByte(std::ceil((static_cast<double>(creature->getHealth()) / std::max<int32_t>(creature->getMaxHealth(), 1)) * 100));
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendFYIBox(const std::string& message)
{
	NetworkMessage msg;
	msg.addByte(0x15);
	msg.addString(message);
	writeToOutputBuffer(msg);
}

//tile
void ProtocolGame::sendMapDescription(const Position& pos)
{
	if (otclientV8) {
		int32_t startz, endz, zstep;

		if (pos.z > 7) {
			startz = pos.z - 2;
			endz = std::min<int32_t>(MAP_MAX_LAYERS - 1, pos.z + 2);
			zstep = 1;
		} else {
			startz = 7;
			endz = 0;
			zstep = -1;
		}

		for (int32_t nz = startz; nz != endz + zstep; nz += zstep) {
			sendFloorDescription(pos, nz);
		}
	} else {
		NetworkMessage msg;
		msg.addByte(0x64);
		msg.addPosition(player->getPosition());
		GetMapDescription(pos.x - awareRange.left(), pos.y - awareRange.top(), pos.z, awareRange.horizontal(), awareRange.vertical(), msg);
		writeToOutputBuffer(msg);
	}
}

void ProtocolGame::sendFloorDescription(const Position& pos, int floor)
{
	// When map view range is big, let's say 30x20 all floors may not fit in single packets
	// So we split one packet with every floor to few packets with single floor
	NetworkMessage msg;
	msg.addByte(0x4B);
	msg.addPosition(player->getPosition());
	msg.addByte(floor);
	int32_t skip = -1;
	GetFloorDescription(msg, pos.x - awareRange.left(), pos.y - awareRange.top(), floor, awareRange.horizontal(), awareRange.vertical(), pos.z - floor, skip);
	if (skip >= 0) {
		msg.addByte(skip);
		msg.addByte(0xFF);
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendAddTileItem(const Position& pos, uint32_t stackpos, const Item* item)
{
	if (!canSee(pos)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x6A);
	msg.addPosition(pos);
	if (player->getOperatingSystem() >= CLIENTOS_OTCLIENT_LINUX) {
		msg.addByte(stackpos);
	}
	msg.addItem(item);
	writeToOutputBuffer(msg);

	checkPredictiveWalking(pos);
}

void ProtocolGame::sendUpdateTileItem(const Position& pos, uint32_t stackpos, const Item* item)
{
	if (!canSee(pos)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x6B);
	msg.addPosition(pos);
	msg.addByte(stackpos);
	msg.addItem(item);
	writeToOutputBuffer(msg);
	
	checkPredictiveWalking(pos);
}

void ProtocolGame::sendRemoveTileThing(const Position& pos, uint32_t stackpos)
{
	if (!canSee(pos)) {
		return;
	}

	NetworkMessage msg;
	RemoveTileThing(msg, pos, stackpos);
	writeToOutputBuffer(msg);

	checkPredictiveWalking(pos);
}

void ProtocolGame::sendUpdateTileCreature(const Position& pos, uint32_t stackpos, const Creature* creature)
{
	if (!canSee(pos)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x6B);
	msg.addPosition(pos);
	msg.addByte(stackpos);

	bool known;
	uint32_t removedKnown;
	checkCreatureAsKnown(creature->getID(), known, removedKnown);
	AddCreature(msg, creature, false, removedKnown);

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendRemoveTileCreature(const Creature*, const Position& pos, uint32_t stackpos)
{
	if (!canSee(pos) || stackpos >= 10) {
		return;
	}

	NetworkMessage msg;
	RemoveTileThing(msg, pos, stackpos);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendUpdateTile(const Tile* tile, const Position& pos)
{
	if (!canSee(pos)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x69);
	msg.addPosition(pos);

	if (tile) {
		GetTileDescription(tile, msg);
		msg.addByte(0x00);
		msg.addByte(0xFF);
	} else {
		msg.addByte(0x01);
		msg.addByte(0xFF);
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendFightModes()
{
	NetworkMessage msg;
	msg.addByte(0xA7);
	msg.addByte(player->fightMode);
	msg.addByte(player->chaseMode);
	msg.addByte(player->secureMode);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendAddCreature(const Creature* creature, const Position& pos, int32_t stackpos)
{
	if (!canSee(pos)) {
		return;
	}

	if (creature != player) {
		// stack pos is always real index now, so it can exceed the limit
		// if stack pos exceeds the limit, we need to refresh the tile instead
		// 1. this is a rare case, and is only triggered by forcing summon in a position
		// 2. since no stackpos will be send to the client about that creature, removing
		//    it must be done with its id if its stackpos remains >= 10. this is done to
		//    add creatures to battle list instead of rendering on screen
		if (stackpos >= 10) {
			// @todo: should we avoid this check?
			if (const Tile* tile = creature->getTile()) {
				sendUpdateTile(tile, pos);
			}
		} else {
			// if stackpos is -1, the client will automatically detect it
			NetworkMessage msg;
			msg.addByte(0x6A);
			msg.addPosition(pos);

			if (player->getOperatingSystem() >= CLIENTOS_OTCLIENT_LINUX) {
				msg.addByte(stackpos);
			}

			bool known;
			uint32_t removedKnown;
			checkCreatureAsKnown(creature->getID(), known, removedKnown);
			AddCreature(msg, creature, known, removedKnown);
			writeToOutputBuffer(msg);
		}

		checkPredictiveWalking(pos);

		return;
	}

	NetworkMessage msg;
	msg.addByte(0x0A);
	msg.add<uint32_t>(player->getID());
	msg.add<uint16_t>(0x32); // beat duration (50)

	// can report bugs?
	if (player->getAccountType() >= ACCOUNT_TYPE_TUTOR) {
		msg.addByte(0x01);
	} else {
		msg.addByte(0x00);
	}

	writeToOutputBuffer(msg);

	sendMapDescription(pos);

	for (int i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; ++i) {
		sendInventoryItem(static_cast<slots_t>(i), player->getInventoryItem(static_cast<slots_t>(i)));
	}

	sendInventoryItem(CONST_SLOT_STORE_INBOX, player->getStoreInbox()->getItem());

	sendStats();
	sendSkills();

	//gameworld light-settings*/
	sendWorldLight(g_game.getWorldLightInfo());

	//player light level
	sendCreatureLight(creature);

	sendVIPEntries();

	player->sendIcons();
}

void ProtocolGame::sendMoveCreature(const Creature* creature, const Position& newPos, int32_t newStackPos, const Position& oldPos, int32_t oldStackPos, bool teleport)
{
	if (creature == player) {
		if (teleport || oldStackPos >= 10) {
			sendRemoveTileCreature(creature, oldPos, oldStackPos);
			if (newPos.z != 8 && oldPos.z != 7) {
				if (oldStackPos >= 10 && canSee(newPos) && canSee(oldPos)) {
					sendUpdateTile(g_game.map.getTile(oldPos), oldPos);
					NetworkMessage msg;

					msg.addByte(0x6A);
					msg.addPosition(newPos);

					if (player->getOperatingSystem() >= CLIENTOS_OTCLIENT_LINUX) {
						msg.addByte(newStackPos);
					}

					bool known;
					uint32_t removedKnown;
					checkCreatureAsKnown(creature->getID(), known, removedKnown);
					AddCreature(msg, creature, known, removedKnown);
					writeToOutputBuffer(msg);
				}
			}
			sendMapDescription(newPos);
			sendCreatureLight(creature);
		} else {
			NetworkMessage msg;
			if (oldPos.z == 7 && newPos.z >= 8) {
				RemoveTileCreature(msg, creature, oldPos, oldStackPos);
			} else {
				msg.addByte(0x6D);
				if (oldStackPos < 10) {
					msg.addPosition(oldPos);
					msg.addByte(oldStackPos);
				} else {
					msg.add<uint16_t>(0xFFFF);
					msg.add<uint32_t>(creature->getID());
				}
				msg.addPosition(newPos);
				if (otclientV8) {
					msg.add<uint16_t>(creature->getStepDuration());
				}
			}

			if (newPos.z > oldPos.z) {
				MoveDownCreature(msg, creature, newPos, oldPos);
			} else if (newPos.z < oldPos.z) {
				MoveUpCreature(msg, creature, newPos, oldPos);
			}

			if (oldPos.y > newPos.y) { // north, for old x              
				msg.addByte(0x65);
				GetMapDescription(oldPos.x - awareRange.left(), newPos.y - awareRange.top(), newPos.z, awareRange.horizontal(), 1, msg);
			} else if (oldPos.y < newPos.y) { // south, for old x
				msg.addByte(0x67);
				GetMapDescription(oldPos.x - awareRange.left(), newPos.y + awareRange.bottom(), newPos.z, awareRange.horizontal(), 1, msg);
			}

			if (oldPos.x < newPos.x) { // east, [with new y]
				msg.addByte(0x66);
				GetMapDescription(newPos.x + awareRange.right(), newPos.y - awareRange.top(), newPos.z, 1, awareRange.vertical(), msg);
			} else if (oldPos.x > newPos.x) { // west, [with new y]
				msg.addByte(0x68);
				GetMapDescription(newPos.x - awareRange.left(), newPos.y - awareRange.top(), newPos.z, 1, awareRange.vertical(), msg);
			}
			writeToOutputBuffer(msg);
		}
	} else if (canSee(oldPos) && canSee(creature->getPosition())) {
		if (teleport || (oldPos.z == 7 && newPos.z >= 8) || oldStackPos >= 10) {
			sendRemoveTileCreature(creature, oldPos, oldStackPos);
			if (oldStackPos >= 10) {
				sendUpdateTile(g_game.map.getTile(oldPos), oldPos);
			}
			sendAddCreature(creature, newPos, newStackPos);
		} else {
			NetworkMessage msg;
			msg.addByte(0x6D);
			if (oldStackPos < 10) {
				msg.addPosition(oldPos);
				msg.addByte(oldStackPos);
			} else {
				msg.add<uint16_t>(0xFFFF);
				msg.add<uint32_t>(creature->getID());
			}
			msg.addPosition(creature->getPosition());
			if (otclientV8) {
				msg.add<uint16_t>(creature->getStepDuration());
			}
			writeToOutputBuffer(msg);
			checkPredictiveWalking(oldPos);
			checkPredictiveWalking(newPos);
		}
	} else if (canSee(oldPos)) {
		sendRemoveTileCreature(creature, oldPos, oldStackPos);
	} else if (canSee(creature->getPosition())) {
		sendAddCreature(creature, newPos, newStackPos);
	}
}

void ProtocolGame::sendInventoryItem(slots_t slot, const Item* item)
{
	NetworkMessage msg;
	if (item) {
		msg.addByte(0x78);
		msg.addByte(slot);
		msg.addItem(item);
	} else {
		msg.addByte(0x79);
		msg.addByte(slot);
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendAddContainerItem(uint8_t cid, const Item* item)
{
	NetworkMessage msg;
	msg.addByte(0x70);
	msg.addByte(cid);
	msg.addItem(item);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendUpdateContainerItem(uint8_t cid, uint16_t slot, const Item* item)
{
	NetworkMessage msg;
	msg.addByte(0x71);
	msg.addByte(cid);
	msg.addByte(static_cast<uint8_t>(slot));
	msg.addItem(item);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendRemoveContainerItem(uint8_t cid, uint16_t slot)
{
	NetworkMessage msg;
	msg.addByte(0x72);
	msg.addByte(cid);
	msg.addByte(static_cast<uint8_t>(slot));
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendTextWindow(uint32_t windowTextId, Item* item, uint16_t maxlen, bool canWrite)
{
	NetworkMessage msg;
	msg.addByte(0x96);
	msg.add<uint32_t>(windowTextId);
	msg.addItemId(item);

	if (canWrite) {
		msg.add<uint16_t>(maxlen);
		msg.addString(item->getText());
	} else {
		const std::string& text = item->getText();
		msg.add<uint16_t>(text.size());
		msg.addString(text);
	}

	const std::string& writer = item->getWriter();
	if (!writer.empty()) {
		msg.addString(writer);
	} else {
		msg.add<uint16_t>(0x00);
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendTextWindow(uint32_t windowTextId, uint32_t itemId, const std::string& text)
{
	NetworkMessage msg;
	msg.addByte(0x96);
	msg.add<uint32_t>(windowTextId);
	msg.addItem(itemId, 1);
	msg.add<uint16_t>(text.size());
	msg.addString(text);
	msg.add<uint16_t>(0x00);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendHouseWindow(uint32_t windowTextId, const std::string& text)
{
	NetworkMessage msg;
	msg.addByte(0x97);
	msg.addByte(0x00);
	msg.add<uint32_t>(windowTextId);
	msg.addString(text);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendOutfitWindow()
{
	const auto& outfits = Outfits::getInstance().getOutfits(player->getSex());
	if (outfits.size() == 0) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xC8);

	if (player->getOperatingSystem() >= CLIENTOS_OTCLIENT_LINUX) {
		Outfit_t currentOutfit = player->getDefaultOutfit();
		if (currentOutfit.lookType == 0) {
			Outfit_t newOutfit;
			newOutfit.lookType = outfits.front().lookType;
			currentOutfit = newOutfit;
		}

		AddOutfit(msg, currentOutfit);

		std::vector<ProtocolOutfit> protocolOutfits;
		if (player->isAccessPlayer()) {
			static const std::string gamemasterOutfitName = "Gamemaster";
			protocolOutfits.emplace_back(gamemasterOutfitName, 75, 0);
		}

		protocolOutfits.reserve(outfits.size());
		for (const Outfit& outfit : outfits) {
			uint8_t addons;
			if (!player->getOutfitAddons(outfit, addons)) {
				continue;
			}
			
			protocolOutfits.emplace_back(outfit.name, outfit.lookType, addons);
			if (protocolOutfits.size() == std::numeric_limits<uint8_t>::max()) { // Game client currently doesn't allow more than 255 outfits
				break;
			}
		}

		msg.addByte(protocolOutfits.size());
		for (const ProtocolOutfit& outfit : protocolOutfits) {
			msg.add<uint16_t>(outfit.lookType);
			msg.addString(outfit.name);
			msg.addByte(outfit.addons);
		}
		
		if (otclientV8) {
		std::vector<const Wing*> wings;
		for (const Wing& wing: g_game.wings.getWings()) {
			if (player->hasWing(&wing)) {
				wings.push_back(&wing);
			}
		}

		msg.addByte(wings.size());
		for (const Wing* wing : wings) {
			msg.add<uint16_t>(wing->clientId);
			msg.addString(wing->name);
		}

		std::vector<const Aura*> auras;
		for (const Aura& aura : g_game.auras.getAuras()) {
			if (player->hasAura(&aura)) {
				auras.push_back(&aura);
			}
		}

		msg.addByte(auras.size());
		for (const Aura* aura : auras) {
			msg.add<uint16_t>(aura->clientId);
			msg.addString(aura->name);
		}
		
		std::vector<const Shader*> shaders;
		for (const Shader& shader : g_game.shaders.getShaders()) {
			if (player->hasShader(&shader)) {
				shaders.push_back(&shader);
			}
		}

		msg.addByte(shaders.size());
		for (const Shader* shader : shaders) {
			msg.add<uint16_t>(shader->id);
			msg.addString(shader->name);
		}
	}
		
	} else {
		Outfit_t currentOutfit = player->getDefaultOutfit();
		AddOutfit(msg, currentOutfit);

		if (player->getSex() == PLAYERSEX_MALE) {
			msg.add<uint16_t>(128);
			if (player->isPremium()) {
				msg.add<uint16_t>(134);
			} else {
				msg.add<uint16_t>(131);
			}
		} else {
			msg.add<uint16_t>(136);
			if (player->isPremium()) {
				msg.add<uint16_t>(142);
			} else {
				msg.add<uint16_t>(139);
			}
		}
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendUpdatedVIPStatus(uint32_t guid, VipStatus_t newStatus)
{
	NetworkMessage msg;
	if (newStatus == VIPSTATUS_ONLINE) {
		msg.addByte(0xD3);
	}
	else {
		msg.addByte(0xD4);
	}
	msg.add<uint32_t>(guid);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendVIP(uint32_t guid, const std::string& name, VipStatus_t status)
{
	NetworkMessage msg;
	msg.addByte(0xD2);
	msg.add<uint32_t>(guid);
	msg.addString(name);
	msg.addByte(status);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendVIPEntries()
{
	const std::forward_list<VIPEntry>& vipEntries = IOLoginData::getVIPEntries(player->getAccount());

	for (const VIPEntry& entry : vipEntries) {
		VipStatus_t vipStatus = VIPSTATUS_ONLINE;

		Player* vipPlayer = g_game.getPlayerByGUID(entry.guid);

		if (!vipPlayer || !player->canSeeCreature(vipPlayer)) {
			vipStatus = VIPSTATUS_OFFLINE;
		}

		sendVIP(entry.guid, entry.name, vipStatus);
	}
}

////////////// Add common messages
void ProtocolGame::AddCreature(NetworkMessage& msg, const Creature* creature, bool known, uint32_t remove)
{
	const Player* otherPlayer = creature->getPlayer();

	if (known) {
		msg.add<uint16_t>(0x62);
		msg.add<uint32_t>(creature->getID());
	} else {
		msg.add<uint16_t>(0x61);
		msg.add<uint32_t>(remove);
		msg.add<uint32_t>(creature->getID());
		msg.addString(creature->getName());
		msg.addString(creature->getMonster() ? creature->getMarketDescription() : "");
	}

	if (creature->isHealthHidden()) {
		msg.addByte(0x00);
	} else {
		msg.addByte(std::ceil((static_cast<double>(creature->getHealth()) / std::max<int32_t>(creature->getMaxHealth(), 1)) * 100));
	}

	msg.addByte(creature->getDirection());

	if (!creature->isInGhostMode() && !creature->isInvisible()) {
		AddOutfit(msg, creature->getCurrentOutfit());
	} else {
		static Outfit_t outfit;
		AddOutfit(msg, outfit);
	}
	
	msg.addByte(creature->getStarAppearance());

	LightInfo lightInfo = creature->getCreatureLight();
	uint8_t lightColor = lightInfo.color;
	uint8_t lightLevel = lightInfo.level;

	if (otherPlayer) {
		if (player->getID() == otherPlayer->getID())
		{
			if (player->hasLightScroll())
			{
				lightColor = 215;
				lightLevel = 9;
			}
		}
	}

	msg.addByte(lightLevel);
	msg.addByte(lightColor);

	msg.add<uint16_t>(creature->getStepSpeed());

	msg.addByte(player->getSkullClient(creature));
	msg.addByte(player->getPartyShield(otherPlayer));
	msg.addByte(creature->getSpeechBubble());
}

void ProtocolGame::AddPlayerStats(NetworkMessage& msg)
{
	msg.addByte(0xA0);

	msg.add<uint16_t>(std::min<int32_t>(player->getHealth(), std::numeric_limits<uint16_t>::max()));
	msg.add<uint16_t>(std::min<int32_t>(player->getMaxHealth(), std::numeric_limits<uint16_t>::max()));
	
	if (player->hasFlag(PlayerFlag_HasInfiniteCapacity)) {
		// This has to be done here, because getFreeCapacity handles inventory space.
		msg.add<uint16_t>(0);
	} else {
		msg.add<uint16_t>(static_cast<uint16_t>(player->getFreeCapacity() / 100.));
	}

	if (player->getExperience() >= std::numeric_limits<uint32_t>::max() - 1) {
		msg.add<uint32_t>(0);
	}
	else {
		msg.add<uint32_t>(static_cast<uint32_t>(player->getExperience()));
	}

	msg.add<uint16_t>(static_cast<uint16_t>(player->getLevel()));
	msg.addByte(player->getLevelPercent());

	msg.add<uint16_t>(std::min<int32_t>(player->getMana(), std::numeric_limits<uint16_t>::max()));
	msg.add<uint16_t>(std::min<int32_t>(player->getMaxMana(), std::numeric_limits<uint16_t>::max()));

	msg.addByte(std::min<uint32_t>(player->getMagicLevel(), std::numeric_limits<uint8_t>::max()));
	msg.addByte(player->getMagicLevelPercent());

	msg.addByte(player->getSoul());
}

void ProtocolGame::AddPlayerSkills(NetworkMessage& msg)
{
	msg.addByte(0xA1);

	for (uint8_t i = SKILL_FIRST; i <= SKILL_LAST; ++i) {
		msg.addByte(std::min<int32_t>(player->getSkillLevel(i), std::numeric_limits<uint16_t>::max()));
		msg.addByte(player->getSkillPercent(i));
	}
}

void ProtocolGame::AddOutfit(NetworkMessage& msg, const Outfit_t& outfit)
{
	msg.add<uint16_t>(outfit.lookType);

	if (outfit.lookType != 0) {
		msg.addByte(outfit.lookHead);
		msg.addByte(outfit.lookBody);
		msg.addByte(outfit.lookLegs);
		msg.addByte(outfit.lookFeet);
		msg.addByte(outfit.lookAddons);
	} else {
		msg.addItemId(outfit.lookTypeEx);
	}
	if (otclientV8) {
		msg.add<uint16_t>(outfit.lookWings);
		msg.add<uint16_t>(outfit.lookAura);
		Shader* shader = g_game.shaders.getShaderByID(outfit.lookShader);
		msg.addString(shader ? shader->name : "");
	}
}

void ProtocolGame::AddWorldLight(NetworkMessage& msg, LightInfo lightInfo)
{
	msg.addByte(0x82);
	msg.addByte(lightInfo.level);
	msg.addByte(lightInfo.color);
}

void ProtocolGame::AddCreatureLight(NetworkMessage& msg, const Creature* creature)
{

	LightInfo lightInfo = creature->getCreatureLight();
	uint8_t lightColor = lightInfo.color;
	uint8_t lightLevel = lightInfo.level;

	if (const Player* otherPlayer = creature->getPlayer()) {
		if (player->getID() == otherPlayer->getID())
		{
			if (player->hasLightScroll())
			{
				lightColor = 215;
				lightLevel = 9;
			}
		}
	}

	msg.addByte(0x8D);
	msg.add<uint32_t>(creature->getID());
	msg.addByte(lightLevel);
	msg.addByte(lightColor);
}

//tile
void ProtocolGame::RemoveTileThing(NetworkMessage& msg, const Position& pos, uint32_t stackpos)
{
	if (stackpos >= 10) {
		return;
	}

	msg.addByte(0x6C);
	msg.addPosition(pos);
	msg.addByte(stackpos);
}

void ProtocolGame::RemoveTileCreature(NetworkMessage& msg, const Creature*, const Position& pos, uint32_t stackpos)
{
	if (stackpos >= 10) {
		return;
	}

	RemoveTileThing(msg, pos, stackpos);
}

void ProtocolGame::MoveUpCreature(NetworkMessage& msg, const Creature* creature, const Position& newPos, const Position& oldPos)
{
	if (creature != player) {
		return;
	}

	//floor change up
	msg.addByte(0xBE);

	//going to surface
	if (newPos.z == 7) {
		int32_t skip = -1;
		if (otclientV8) {
			for (int z = 5; z >= 0; --z) {
				sendFloorDescription(oldPos, z);
			}
		} else {
			GetFloorDescription(msg, oldPos.x - awareRange.left(), oldPos.y - awareRange.top(), 5, awareRange.horizontal(), awareRange.vertical(), 3, skip); //(floor 7 and 6 already set)
			GetFloorDescription(msg, oldPos.x - awareRange.left(), oldPos.y - awareRange.top(), 4, awareRange.horizontal(), awareRange.vertical(), 4, skip);
			GetFloorDescription(msg, oldPos.x - awareRange.left(), oldPos.y - awareRange.top(), 3, awareRange.horizontal(), awareRange.vertical(), 5, skip);
			GetFloorDescription(msg, oldPos.x - awareRange.left(), oldPos.y - awareRange.top(), 2, awareRange.horizontal(), awareRange.vertical(), 6, skip);
			GetFloorDescription(msg, oldPos.x - awareRange.left(), oldPos.y - awareRange.top(), 1, awareRange.horizontal(), awareRange.vertical(), 7, skip);
			GetFloorDescription(msg, oldPos.x - awareRange.left(), oldPos.y - awareRange.top(), 0, awareRange.horizontal(), awareRange.vertical(), 8, skip);
			if (skip >= 0) {
				msg.addByte(skip);
				msg.addByte(0xFF);
			}
		}
	}
	//underground, going one floor up (still underground)
	else if (newPos.z > 7) {
		int32_t skip = -1;
		GetFloorDescription(msg, oldPos.x - awareRange.left(), oldPos.y - awareRange.top(), oldPos.getZ() - 3, awareRange.horizontal(), awareRange.vertical(), 3, skip);

		if (skip >= 0) {
			msg.addByte(skip);
			msg.addByte(0xFF);
		}
	}

	//moving up a floor up makes us out of sync
	//west
	msg.addByte(0x68);
	GetMapDescription(oldPos.x - awareRange.left(), oldPos.y - (awareRange.top() - 1), newPos.z, 1, awareRange.vertical(), msg);

	//north
	msg.addByte(0x65);
	GetMapDescription(oldPos.x - awareRange.left(), oldPos.y - awareRange.top(), newPos.z, awareRange.horizontal(), 1, msg);
}

void ProtocolGame::MoveDownCreature(NetworkMessage& msg, const Creature* creature, const Position& newPos, const Position& oldPos)
{
	if (creature != player) {
		return;
	}

	//floor change down
	msg.addByte(0xBF);

	//going from surface to underground
	if (newPos.z == 8) {
		int32_t skip = -1;

		if (otclientV8) {
			for (int z = 0; z < 3; ++z) {
				sendFloorDescription(oldPos, newPos.z + z);
			}
		} else {
			GetFloorDescription(msg, oldPos.x - awareRange.left(), oldPos.y - awareRange.top(), newPos.z, awareRange.horizontal(), awareRange.vertical(), -1, skip);
			GetFloorDescription(msg, oldPos.x - awareRange.left(), oldPos.y - awareRange.top(), newPos.z + 1, awareRange.horizontal(), awareRange.vertical(), -2, skip);
			GetFloorDescription(msg, oldPos.x - awareRange.left(), oldPos.y - awareRange.top(), newPos.z + 2, awareRange.horizontal(), awareRange.vertical(), -3, skip);
			if (skip >= 0) {
				msg.addByte(skip);
				msg.addByte(0xFF);
			}
		}
	}
	//going further down
	else if (newPos.z > oldPos.z && newPos.z > 8 && newPos.z < 14) {
		int32_t skip = -1;
		GetFloorDescription(msg, oldPos.x - awareRange.left(), oldPos.y - awareRange.top(), newPos.z + 2, awareRange.horizontal(), awareRange.vertical(), -3, skip);

		if (skip >= 0) {
			msg.addByte(skip);
			msg.addByte(0xFF);
		}
	}

	//moving down a floor makes us out of sync
	//east
	msg.addByte(0x66);
	GetMapDescription(newPos.x + awareRange.right(), newPos.y - awareRange.top(), newPos.z, 1, awareRange.vertical(), msg);

	//south
	msg.addByte(0x67);
	GetMapDescription(newPos.x - awareRange.left(), newPos.y - awareRange.top(), newPos.z, 1, awareRange.vertical(), msg);
}

void ProtocolGame::AddShopItem(NetworkMessage& msg, const ShopInfo& item)
{
	const ItemType& it = Item::items[item.itemId];
	msg.add<uint16_t>(it.clientId);

	if (it.isSplash() || it.isFluidContainer()) {
		msg.addByte(getLiquidColor(item.subType));
	} else {
		msg.addByte(0x00);
	}

	msg.addString(item.realName);
	msg.add<uint32_t>(it.weight);
	msg.add<uint32_t>(item.buyPrice);
	msg.add<uint32_t>(item.sellPrice);
}

void ProtocolGame::parseExtendedOpcode(NetworkMessage& msg)
{
	uint8_t opcode = msg.getByte();
	const std::string& buffer = msg.getString();

	// process additional opcodes via lua script event
	g_game.parsePlayerExtendedOpcode(player->getID(), opcode, buffer);
}

void ProtocolGame::parseNewWalking(NetworkMessage& msg)
{
	uint32_t playerWalkId = msg.get<uint32_t>();
	int32_t predictiveWalkId = msg.get<int32_t>(); // extension for proxy system, currently not used
	Position playerPosition = msg.getPosition(); // local player position before moving, including prewalk
	uint8_t flags = msg.getByte(); // 0x01 - prewalk, 0x02 - autowalk

	uint16_t numdirs = msg.get<uint16_t>();
	if (numdirs == 0 || numdirs > 4096) {
		return;
	}
	
	if (OTSYS_TIME() < player->earliestTeleportTime) {
		sendNewCancelWalk();
		return;
	}

	if (numdirs > 1 && OTSYS_TIME() < player->earliestAutoWalkTime) {
		// Do not allow auto map walking at this moment
		// This added code helps against certain rubberband effects when players
		// Are abusing map auto walk constantly & also killing the server's performance
		// Because this walkmatrix system relies on the server's pathfinding to calculate the path when 
		// The player is blocked.
		sendNewCancelWalk();
		return;
	}

	if (numdirs > 1) {
		// Only cooldown auto walk for actual auto walking
		player->earliestAutoWalkTime = OTSYS_TIME() + 200;
	}

	std::list<Direction> path;
	for (uint16_t i = 0; i < numdirs; ++i) {
		uint8_t rawdir = msg.getByte();
		switch (rawdir) {
			case 1: path.push_back(DIRECTION_EAST); break;
			case 2: path.push_back(DIRECTION_NORTHEAST); break;
			case 3: path.push_back(DIRECTION_NORTH); break;
			case 4: path.push_back(DIRECTION_NORTHWEST); break;
			case 5: path.push_back(DIRECTION_WEST); break;
			case 6: path.push_back(DIRECTION_SOUTHWEST); break;
			case 7: path.push_back(DIRECTION_SOUTH); break;
			case 8: path.push_back(DIRECTION_SOUTHEAST); break;
			default: break;
		}
	}

	uint32_t playerId = player->getID();

	bool preWalk = flags & 0x01;
	Position destination = getNextPosition(*(path.begin()), playerPosition);
	if (preWalk && predictiveWalkId < walkMatrix.get(destination)) {
		walkId += 1;
		sendWalkId();
		return;
	}

	if (playerWalkId < walkId) {
		// this walk has been sent before player received previous newCancelWalk, so it's invalid, ignore it
		return;
	}

	g_game.playerNewWalk(playerId, playerPosition, flags, path);
}

void ProtocolGame::parseBattlepass(NetworkMessage& msg)
{
	uint8_t id = msg.getByte();
	switch (id) {
	case 0: {
		// The battlepass window has been opened, send all current quests
		addGameTask(&Game::playerOpenBattlepass, player->getID(), msg.getByte() != 0);
		break;
	}
	case 1: {
		// The battlepass window has been closed
		addGameTask(&Game::playerCloseBattlepass, player->getID());
		break;
	}
	case 2:
	case 3: {
		// Player hit the "Complete" (2) or "Shuffle" (3) button
		addGameTask(&Game::playerModifyQuest, player->getID(), id, msg.get<uint16_t>());
		break;
	}
	case 4: {
		// Player buys premium battlepass
		addGameTask(&Game::playerBuyPremiumBattlepass, player->getID());
		break;
	}
	default:
		break;
	}
}

void ProtocolGame::sendBattlepassQuests(uint32_t experience, uint16_t level, const BattlePassPlayerDataMap& data, const BattlePassRewardsMap& levels, bool hasPremium, bool sendLevels)
{
	NetworkMessage msg;
	msg.addByte(0x27);
	msg.addByte(0);
	msg.add<uint32_t>(experience);
	msg.add<uint16_t>(level);
	msg.addByte(hasPremium ? 0x01 : 0x00);
	msg.addByte(data.size());

	const auto timeNow = OTSYS_TIME();
	for (auto& itBattlepass : data) {
		msg.addByte(itBattlepass.first);
		msg.addByte(itBattlepass.second.size());
		for (auto& itQuest : itBattlepass.second) {
			msg.addByte(itQuest.id);
			msg.addByte(itQuest.shuffled ? 0x01 : 0x00);
			msg.add<uint16_t>(itQuest.data->id);
			msg.add<uint32_t>(itQuest.amount);
			msg.add<uint32_t>(itQuest.data->amount);
			msg.add<uint32_t>(itQuest.data->experience);
			if (itQuest.cooldown > timeNow) {
				msg.add<uint32_t>(itQuest.cooldown - timeNow);
			}
			else if (itQuest.cooldown == 1) {
				msg.add<uint32_t>(1);
			}
			else {
				msg.add<uint32_t>(0);
			}

			AddAnyValue(msg, itQuest.id, itQuest.data->value);
		}
	}

	if (sendLevels) {
		msg.addByte(1);
		msg.add<uint16_t>(levels.size());
		for (auto& [level, rewards] : levels) {
			msg.add<uint16_t>(level); // Battlepass level
			// Free rewards
			msg.addByte(rewards.freeRewards.size());
			for (auto& [type, rewardId, clientId, amount] : rewards.freeRewards) {
				msg.addByte(type);
				if (type == BATTLEPASS_REWARD_ITEM) {
					msg.add<uint16_t>(clientId); // Client ID
				}
				else {
					msg.add<uint16_t>(rewardId); // Outfit/mount/wings ID
				}
				msg.add<uint16_t>(amount); // Amount
			}

			// Premium rewards
			msg.addByte(rewards.premiumRewards.size());
			for (auto& [type, rewardId, clientId, amount] : rewards.premiumRewards) {
				msg.addByte(type);
				if (type == BATTLEPASS_REWARD_ITEM) {
					msg.add<uint16_t>(clientId); // Client ID
				}
				else {
					msg.add<uint16_t>(rewardId); // Outfit/mount/wings ID
				}
				msg.add<uint16_t>(amount); // Amount
			}
		}
	}
	else {
		msg.addByte(0);
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendUpdateBattlepassQuest(const BattlePassQuestsVector& quests)
{
	NetworkMessage msg;
	msg.addByte(0x27);
	msg.addByte(1);
	msg.addByte(quests.size());

	const auto timeNow = OTSYS_TIME();
	for (auto& itBattlepass : quests) {
		msg.addByte(itBattlepass->id);
		msg.addByte(itBattlepass->type);
		msg.add<uint16_t>(itBattlepass->data->id);
		msg.add<uint32_t>(itBattlepass->amount);
		AddAnyValue(msg, itBattlepass->id, itBattlepass->data->value);

		if (itBattlepass->amount >= itBattlepass->data->amount) {
			if (itBattlepass->cooldown > timeNow) {
				msg.add<uint32_t>(itBattlepass->cooldown - timeNow);
			}
			else if (itBattlepass->cooldown <= 1) {
				msg.add<uint32_t>(itBattlepass->cooldown);
			}
			else {
				msg.add<uint32_t>(1);
			}			
		}
		else {
				msg.add<uint32_t>(1);
		}		
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendUpdateBattlepassQuest(uint16_t id, uint32_t experience, uint16_t level, time_t cooldown)
{
	NetworkMessage msg;
	msg.addByte(0x27);
	msg.addByte(2);
	msg.add<uint16_t>(id);
	msg.add<uint32_t>(experience);
	msg.add<uint16_t>(level);
	msg.add<uint32_t>(cooldown);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendBattlepassQuest(uint16_t questId, const BattlePassPlayerData& data)
{
	NetworkMessage msg;
	msg.addByte(0x27);
	msg.addByte(3);
	msg.add<uint16_t>(questId);
	msg.addByte(data.id);
	msg.add<uint16_t>(data.data->id);
	msg.add<uint32_t>(data.data->amount);
	msg.add<uint32_t>(data.data->experience);
	AddAnyValue(msg, data.id, data.data->value);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendBattlepassPremium(uint8_t id, bool isPremium)
{
	NetworkMessage msg;
	msg.addByte(0x27);
	msg.addByte(4);
	msg.addByte(id);
	msg.addByte(isPremium ? 0x01 : 0x00);
	writeToOutputBuffer(msg);
}

void ProtocolGame::AddAnyValue(NetworkMessage& msg, BattlePassQuests_t id, const std::any& value)
{
	if (value.has_value()) {
		msg.addByte(0x01);

		const auto& valueType = value.type();
		if (valueType == typeid(uint16_t)) {
			switch (id) {
			case BATTLEPASS_QUEST_KILL_MONSTERS: {
				msg.addByte(ItemAttributesStruct::STRING);
				msg.addString(getMonsterClassName(std::any_cast<uint16_t>(value)));
				break;
			}
											   /*case BATTLEPASS_QUEST_GET_ITEM: {
												   msg.addByte(ItemAttributesStruct::STRING);
												   msg.addString(getRarityName(std::any_cast<uint16_t>(value)));
												   break;
											   }*/
			default: {
				msg.addByte(ItemAttributesStruct::INTEGER);
				msg.add<uint16_t>(std::any_cast<uint16_t>(value));
				break;
			}
			}
		}
		else if (valueType == typeid(std::string)) {
			msg.addByte(ItemAttributesStruct::STRING);
			msg.addString(std::any_cast<std::string>(value));
		}
	}
	else {
		msg.addByte(0x00);
	}
}

void ProtocolGame::checkPredictiveWalking(const Position& pos)
{
	if (!otclientV8)
		return;

	if (walkMatrix.inRange(pos) && !g_game.map.canWalkTo(*player, pos)) {
		int newValue = walkMatrix.update(pos);
		sendPredictiveCancel(pos, newValue);
	}
}

void ProtocolGame::sendPredictiveCancel(const Position& pos, int value)
{
	if (!otclientV8)
		return;

	NetworkMessage msg;
	msg.addByte(0x46);
	msg.addPosition(pos);
	msg.addByte(player->getDirection());
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendWalkId()
{
	if (!otclientV8)
		return;

	NetworkMessage msg;
	msg.addByte(0x47);
	msg.add<uint32_t>(walkId);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendNewCancelWalk()
{
	if (!otclientV8)
		return;

	NetworkMessage msg;
	msg.addByte(0x45);
	msg.addByte(player->getDirection());
	writeToOutputBuffer(msg);
	walkId += 1;
	sendWalkId();
}

void ProtocolGame::parseChangeAwareRange(NetworkMessage& msg)
{
	uint8_t width = msg.get<uint8_t>();
	uint8_t height = msg.get<uint8_t>();

	g_dispatcher.addTask(createTask(std::bind(&ProtocolGame::updateAwareRange, getThis(), width, height)));
}

void ProtocolGame::updateAwareRange(int width, int height)
{
	if (!otclientV8)
		return;

	// If you want to change max awareRange, edit maxViewportX, maxViewportY, maxClientViewportX, maxClientViewportY in map.h
	awareRange.width = std::min(Map::maxViewportX * 2 - 1, std::min(Map::maxClientViewportX * 2 + 1, std::max(15, width)));
	awareRange.height = std::min(Map::maxViewportY * 2 - 1, std::min(Map::maxClientViewportY * 2 + 1, std::max(11, height)));
	// numbers must be odd
	if (awareRange.width % 2 != 1)
		awareRange.width -= 1;
	if (awareRange.height % 2 != 1)
		awareRange.height -= 1;

	sendAwareRange();
	sendMapDescription(player->getPosition()); // refresh map
}

void ProtocolGame::sendAwareRange()
{
	if (!otclientV8)
		return;

	NetworkMessage msg;
	msg.addByte(0x42);
	msg.add<uint8_t>(awareRange.width);
	msg.add<uint8_t>(awareRange.height);
	writeToOutputBuffer(msg);
}
