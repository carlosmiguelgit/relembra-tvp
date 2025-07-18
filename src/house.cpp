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

#include "pugicast.h"

#include "house.h"
#include "iologindata.h"
#include "game.h"
#include "configmanager.h"
#include "globalevent.h"
#include "bed.h"
#include "logger.h"

#include <fmt/format.h>

extern ConfigManager g_config;
extern Game g_game;
extern GlobalEvents* g_globalEvents;

House::House(uint32_t houseId) : id(houseId) {}

void House::addTile(HouseTile* tile)
{
	tile->setFlag(TILESTATE_PROTECTIONZONE);
	tile->resetFlag(TILESTATE_REFRESH);
	houseTiles.push_back(tile);
}

void House::setOwner(uint32_t guid, bool updateDatabase/* = true*/, bool sendItemsToDepot /* = true*/, Player* player/* = nullptr*/)
{
	if (updateDatabase && owner != guid) {
		Database& db = Database::getInstance();
		db.executeQuery(fmt::format("UPDATE `houses` SET `owner` = {:d}, `bid` = 0, `bid_end` = 0, `last_bid` = 0, `highest_bidder` = 0  WHERE `id` = {:d}", guid, id));
	}

	if (isLoaded && owner == guid) {
		return;
	}

	isLoaded = true;

	if (owner != 0) {
		//send items to depot
		if (sendItemsToDepot) {
			if (player) {
				transferToDepot(player);
			} else {
				transferToDepot();
			}
		}

		for (HouseTile* tile : houseTiles) {
			if (const CreatureVector* creatures = tile->getCreatures()) {
				for (int32_t i = creatures->size(); --i >= 0;) {
					kickPlayer(nullptr, (*creatures)[i]->getPlayer());
				}
			}
		}

		// Remove players from beds
		for (BedItem* bed : bedsList) {
			if (bed->getSleeper() != 0) {
				bed->wakeUp(nullptr);
			}
		}

		//clean access lists
		owner = 0;
		ownerAccountId = 0;
		setAccessList(SUBOWNER_LIST, "");
		setAccessList(GUEST_LIST, "");

		for (Door* door : doorSet) {
			door->setAccessList("");
		}
	} else {
		std::string strRentPeriod = asLowerCaseString(g_config.getString(ConfigManager::HOUSE_RENT_PERIOD));
		time_t currentTime = time(nullptr);
		if (strRentPeriod == "yearly") {
			currentTime += 24 * 60 * 60 * 365;
		} else if (strRentPeriod == "monthly") {
			currentTime += 24 * 60 * 60 * 30;
		} else if (strRentPeriod == "weekly") {
			currentTime += 24 * 60 * 60 * 7;
		} else if (strRentPeriod == "daily") {
			currentTime += 24 * 60 * 60;
		} else {
			currentTime = 0;
		}

		paidUntil = currentTime;
		rentWarnings = 0;
	}

	if (guid != 0) {
		std::string name = IOLoginData::getNameByGuid(guid);
		if (!name.empty()) {
			owner = guid;
			ownerName = name;
			ownerAccountId = IOLoginData::getAccountIdByPlayerName(name);

			if (updateDatabase) {
				Item* letter = Item::CreateItem(ITEM_LETTER_STAMPED);

				std::string strRentPeriod = asLowerCaseString(g_config.getString(ConfigManager::HOUSE_RENT_PERIOD));
				if (strRentPeriod == "yearly") {
					strRentPeriod = "365 days"; // kappa
				} else if (strRentPeriod == "monthly") {
					strRentPeriod = "thirty days";
				} else if (strRentPeriod == "weekly") {
					strRentPeriod = "seven days";
				} else if (strRentPeriod == "daily") {
					strRentPeriod = "a day";
				}

				letter->setText(fmt::format("Welcome!\n\nCongratulations on your choice for house \"{:s}\".\nThe rent for the first month has already been debited to your depot. The next rent will be payable in {:s}.\nHave a good time in your new home!", getName(), strRentPeriod));

				if (Player* playerOnline = g_game.getPlayerByGUID(guid)) {
					// player bought house in-game with command or GM gave it to them
					DepotLocker* depotLocker = playerOnline->getDepotLocker(townId, true);
					depotLocker->addItemBack(letter);

					playerOnline->setLastDepotId(townId);
				} else {
					Player newOwner(nullptr);
					if (IOLoginData::loadPlayerById(&newOwner, guid)) {
						DepotLocker* depotLocker = newOwner.getDepotLocker(townId, true);
						depotLocker->addItemBack(letter);

						newOwner.setLastDepotId(townId);
						IOLoginData::savePlayer(&newOwner, true);
					}
				}
			}
		}
	}

	updateDoorDescription();
}

#include <cmath> // para std::ceil

void House::updateDoorDescription() const
{
	const int32_t housePrice = g_config.getNumber(ConfigManager::HOUSE_PRICE);

	if (g_config.getBoolean(ConfigManager::HOUSE_DOOR_SHOW_PRICE)) {
		for (const auto& it : doorSet) {
			std::string priceString;
			if (owner == 0) {
				int tileCount = static_cast<int>(getTiles().size());
				int goldCost = housePrice * tileCount;
				int premiumPoints = static_cast<int>(std::ceil(tileCount / 2.0));

				priceString = fmt::format(" It costs {:d} gold coins or {:d} premium points.", goldCost, premiumPoints);
			}

			it->setSpecialDescription(fmt::format(
				"It belongs to house '{:s}'. {:s} owns this house.{:s}",
				houseName,
				(owner != 0) ? ownerName : "Nobody",
				priceString
			));
		}
	}
	else {
		for (const auto& it : doorSet) {
			it->setSpecialDescription(fmt::format(
				"It belongs to house '{:s}'. {:s} owns this house.",
				houseName,
				(owner != 0) ? ownerName : "Nobody"
			));
		}
	}
}

AccessHouseLevel_t House::getHouseAccessLevel(const Player* player) const
{
	if (!player) {
		return HOUSE_OWNER;
	}

	if (g_config.getBoolean(ConfigManager::HOUSE_OWNED_BY_ACCOUNT)) {
		if (ownerAccountId == player->getAccount()) {
			return HOUSE_OWNER;
		}
	}

	if (player->hasFlag(PlayerFlag_CanEditHouses)) {
		return HOUSE_OWNER;
	}

	if (player->getGUID() == owner) {
		return HOUSE_OWNER;
	}

	if (subOwnerList.isInList(player)) {
		return HOUSE_SUBOWNER;
	}

	if (guestList.isInList(player)) {
		return HOUSE_GUEST;
	}

	return HOUSE_NOT_INVITED;
}

bool House::kickPlayer(Player* player, Player* target)
{
	if (!target) {
		return false;
	}

	HouseTile* houseTile = dynamic_cast<HouseTile*>(target->getTile());
	if (!houseTile || houseTile->getHouse() != this) {
		return false;
	}

	if (getHouseAccessLevel(player) < getHouseAccessLevel(target) || target->hasFlag(PlayerFlag_CanEditHouses)) {
		return false;
	}

	Position oldPosition = target->getPosition();
	if (g_game.internalTeleport(target, getEntryPosition()) == RETURNVALUE_NOERROR) {
		g_game.addMagicEffect(oldPosition, CONST_ME_POFF);
		g_game.addMagicEffect(getEntryPosition(), CONST_ME_TELEPORT);
	}
	return true;
}

void House::setAccessList(uint32_t listId, const std::string& textlist)
{
	if (listId == GUEST_LIST) {
		guestList.parseList(textlist);
	} else if (listId == SUBOWNER_LIST) {
		subOwnerList.parseList(textlist);
	} else {
		Door* door = getDoorByNumber(listId);
		if (door) {
			door->setAccessList(textlist);
		}

		// We do not have to kick anyone
		return;
	}

	//kick uninvited players
	for (HouseTile* tile : houseTiles) {
		if (CreatureVector* creatures = tile->getCreatures()) {
			for (int32_t i = creatures->size(); --i >= 0;) {
				Player* player = (*creatures)[i]->getPlayer();
				if (player && !isInvited(player)) {
					kickPlayer(nullptr, player);
				}
			}
		}
	}
}

bool House::transferToDepot() const
{
	if (townId == 0 || owner == 0) {
		return false;
	}

	Player* player = g_game.getPlayerByGUID(owner);
	if (player) {
		player->setLastDepotId(townId);
		transferToDepot(player);
	} else {
		Player tmpPlayer(nullptr);
		if (!IOLoginData::loadPlayerById(&tmpPlayer, owner)) {
			g_logger.houseLog(spdlog::level::critical, fmt::format("Could not transfer items to depot, player was not loaded. GUID:{:d}, HouseID:{:d}", owner, getId()));
			return false;
		}

		tmpPlayer.setLastDepotId(townId);
		transferToDepot(&tmpPlayer);
		IOLoginData::savePlayer(&tmpPlayer, true);
	}
	return true;
}

bool House::transferToDepot(Player* player) const
{
	if (townId == 0 || owner == 0) {
		return false;
	}

	for (auto& it : g_globalEvents->getEventMap(GLOBALEVENT_HOUSE_TRANSFER_ITEMS)) {
		it.second.executeHouseTransferToDepot(player, townId);
	}

	player->setLastDepotId(townId);
	return true;
}

bool House::getAccessList(uint32_t listId, std::string& list) const
{
	if (listId == GUEST_LIST) {
		guestList.getList(list);
		return true;
	} else if (listId == SUBOWNER_LIST) {
		subOwnerList.getList(list);
		return true;
	}

	Door* door = getDoorByNumber(listId);
	if (!door) {
		return false;
	}

	return door->getAccessList(list);
}

bool House::isInvited(const Player* player) const
{
	return getHouseAccessLevel(player) != HOUSE_NOT_INVITED;
}

void House::addDoor(Door* door)
{
	door->incrementReferenceCounter();
	doorSet.insert(door);
	door->setHouse(this);
	updateDoorDescription();
}

void House::removeDoor(Door* door)
{
	auto it = doorSet.find(door);
	if (it != doorSet.end()) {
		door->decrementReferenceCounter();
		doorSet.erase(it);
	}
}

void House::addBed(BedItem* bed)
{
	bedsList.push_back(bed);
	bed->incrementReferenceCounter();
	bed->setHouse(this);
}

void House::removeBed(BedItem* bed)
{
	auto it = std::find(bedsList.begin(), bedsList.end(), bed);
	if (it != bedsList.end()) {
		bed->decrementReferenceCounter();
		bedsList.erase(it);
	}
}

Door* House::getDoorByNumber(uint32_t doorId) const
{
	for (Door* door : doorSet) {
		if (door->getDoorId() == doorId) {
			return door;
		}
	}
	return nullptr;
}

Door* House::getDoorByPosition(const Position& pos)
{
	for (Door* door : doorSet) {
		if (door->getPosition() == pos) {
			return door;
		}
	}
	return nullptr;
}

bool House::canEditAccessList(uint32_t listId, const Player* player)
{
	switch (getHouseAccessLevel(player)) {
		case HOUSE_OWNER:
			return true;

		case HOUSE_SUBOWNER:
			return listId == GUEST_LIST;

		default:
			return false;
	}
}

HouseTransferItem* House::getTransferItem()
{
	if (transferItem != nullptr) {
		return nullptr;
	}

	transfer_container.setParent(nullptr);
	transferItem = HouseTransferItem::createHouseTransferItem(this);
	transfer_container.addThing(transferItem);
	return transferItem;
}

void House::resetTransferItem()
{
	if (transferItem) {
		Item* tmpItem = transferItem;
		transferItem = nullptr;
		transfer_container.setParent(nullptr);

		transfer_container.removeThing(tmpItem, tmpItem->getItemCount());
		g_game.ReleaseItem(tmpItem);
	}
}

HouseTransferItem* HouseTransferItem::createHouseTransferItem(House* house)
{
	HouseTransferItem* transferItem = new HouseTransferItem(house);
	transferItem->incrementReferenceCounter();
	transferItem->setID(ITEM_DOCUMENT_RO);
	transferItem->setSubType(1);
	transferItem->setSpecialDescription(fmt::format("It is a house transfer document for '{:s}'.", house->getName()));
	return transferItem;
}

void HouseTransferItem::onTradeEvent(TradeEvents_t event, Player* owner)
{
	if (event == ON_TRADE_TRANSFER) {
		if (house) {
			house->executeTransfer(this, owner);
		}

		g_game.internalRemoveItem(this, 1);
	} else if (event == ON_TRADE_CANCEL) {
		if (house) {
			house->resetTransferItem();
		}
	}
}

bool House::executeTransfer(HouseTransferItem* item, Player* newOwner)
{
	if (transferItem != item) {
		return false;
	}

	Database& db = Database::getInstance();
	time_t currentTime = time(nullptr);
	db.executeQuery(fmt::format("INSERT INTO `transfer_house_history`(`player_id`, `new_owner`, `house_id`, `time`, `date_str`) VALUES ({:d}, {:d}, {:d}, {:d}, \"{:s}\")", owner, newOwner->getGUID(), id, currentTime, formatDate(currentTime)));

	setOwner(newOwner->getGUID());
	transferItem = nullptr;
	return true;
}

void AccessList::parseList(const std::string& list)
{
	playerList.clear();
	guildRankList.clear();
	allowEveryone = false;
	this->list = list;
	if (list.empty()) {
		return;
	}

	std::istringstream listStream(list);
	std::string line;

	uint16_t lineNo = 1;
	while (getline(listStream, line)) {
		if (++lineNo > 100) {
			break;
		}

		trimString(line);
		trim_left(line, '\t');
		trim_right(line, '\t');
		trimString(line);

		if (line.empty() || line.front() == '#' || line.length() > 100) {
			continue;
		}

		toLowerCaseString(line);

		std::string::size_type at_pos = line.find("@");
		if (at_pos != std::string::npos) {
			if (at_pos == 0) {
				addGuild(line.substr(1));
			} else {
				addGuildRank(line.substr(0, at_pos), line.substr(at_pos + 1));
			}
		} else if (line == "*") {
			allowEveryone = true;
		} else if (line.find("!") != std::string::npos || line.find("*") != std::string::npos || line.find("?") != std::string::npos) {
			continue; // regexp no longer supported
		} else {
			addPlayer(line);
		}
	}
}

void AccessList::addPlayer(const std::string& name)
{
	Player* player = g_game.getPlayerByName(name);
	if (player) {
		playerList.insert(player->getGUID());
	} else {
		uint32_t guid = IOLoginData::getGuidByName(name);
		if (guid != 0) {
			playerList.insert(guid);
		}
	}
}

namespace {

const Guild* getGuildByName(const std::string& name)
{
	uint32_t guildId = IOGuild::getGuildIdByName(name);
	if (guildId == 0) {
		return nullptr;
	}

	const Guild* guild = g_game.getGuild(guildId);
	if (guild) {
		return guild;
	}

	return IOGuild::loadGuild(guildId);
}

}

void AccessList::addGuild(const std::string& name)
{
	const Guild* guild = getGuildByName(name);
	if (guild) {
		for (auto rank : guild->getRanks()) {
			guildRankList.insert(rank->id);
		}
	}
}

void AccessList::addGuildRank(const std::string& name, const std::string& rankName)
{
	const Guild* guild = getGuildByName(name);
	if (guild) {
		GuildRank_ptr rank = guild->getRankByName(rankName);
		if (rank) {
			guildRankList.insert(rank->id);
		}
	}
}

bool AccessList::isInList(const Player* player) const
{
	if (allowEveryone) {
		return true;
	}

	auto playerIt = playerList.find(player->getGUID());
	if (playerIt != playerList.end()) {
		return true;
	}

	GuildRank_ptr rank = player->getGuildRank();
	return rank && guildRankList.find(rank->id) != guildRankList.end();
}

void AccessList::getList(std::string& list) const
{
	list = this->list;
}

Door::Door(uint16_t type) :	Item(type) {}

Attr_ReadValue Door::readAttr(AttrTypes_t attr, PropStream& propStream)
{
	if (attr == ATTR_HOUSEDOORID) {
		uint8_t doorId;
		if (!propStream.read<uint8_t>(doorId)) {
			return ATTR_READ_ERROR;
		}

		setDoorId(doorId);
		return ATTR_READ_CONTINUE;
	}
	return Item::readAttr(attr, propStream);
}

void Door::serializeAttr(PropWriteStream& writeStream) const
{
	if (getDoorId() > 0) {
		writeStream.write<uint8_t>(ATTR_HOUSEDOORID);
		writeStream.write<uint8_t>(getDoorId());
	}

	Item::serializeAttr(writeStream);
}

void Door::setHouse(House* house)
{
	if (this->house != nullptr) {
		return;
	}

	this->house = house;

	if (!accessList) {
		accessList.reset(new AccessList());
	}
}

bool Door::canUse(const Player* player)
{
	if (!house) {
		return true;
	}

	if (house->getHouseAccessLevel(player) >= HOUSE_SUBOWNER) {
		return true;
	}

	return accessList->isInList(player);
}

void Door::setAccessList(const std::string& textlist)
{
	if (!accessList) {
		accessList.reset(new AccessList());
	}

	accessList->parseList(textlist);
}

bool Door::getAccessList(std::string& list) const
{
	if (!house) {
		return false;
	}

	accessList->getList(list);
	return true;
}

void Door::onRemoved()
{
	Item::onRemoved();

	if (house) {
		house->removeDoor(this);
	}
}

House* Houses::getHouseByPlayerId(uint32_t playerId)
{
	for (const auto& it : houseMap) {
		if (it.second->getOwner() == playerId) {
			return it.second;
		}
	}
	return nullptr;
}

bool Houses::loadHousesXML(const std::string& filename)
{
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(filename.c_str());
	if (!result) {
		printXMLError("Error - Houses::loadHousesXML", filename, result);
		return false;
	}

	for (auto houseNode : doc.child("houses").children()) {
		pugi::xml_attribute houseIdAttribute = houseNode.attribute("houseid");
		if (!houseIdAttribute) {
			return false;
		}

		int32_t houseId = pugi::cast<int32_t>(houseIdAttribute.value());

		House* house = getHouse(houseId);
		if (!house) {
			std::cout << "Error: [Houses::loadHousesXML] Unknown house, id = " << houseId << std::endl;
			return false;
		}

		house->setName(houseNode.attribute("name").as_string());
		house->setGuildHall(houseNode.attribute("guildhall").as_bool());

		Position entryPos(
			pugi::cast<uint16_t>(houseNode.attribute("entryx").value()),
			pugi::cast<uint16_t>(houseNode.attribute("entryy").value()),
			pugi::cast<uint16_t>(houseNode.attribute("entryz").value())
		);
		if (entryPos.x == 0 && entryPos.y == 0 && entryPos.z == 0) {
			std::cout << "[Warning - Houses::loadHousesXML] House entry not set"
					    << " - Name: " << house->getName()
					    << " - House id: " << houseId << std::endl;
		}
		house->setEntryPos(entryPos);

		house->setRent(pugi::cast<uint32_t>(houseNode.attribute("rent").value()));
		house->setTownId(pugi::cast<uint32_t>(houseNode.attribute("townid").value()));
		house->setSize(pugi::cast<uint32_t>(houseNode.attribute("size").value()));

		house->setOwner(0, false);
	}
	return true;
}

void Houses::payHouses(RentPeriod_t rentPeriod) const
{
	if (rentPeriod == RENTPERIOD_NEVER) {
		return;
	}

	g_logger.houseLog(spdlog::level::info, "Paying houses...");

	time_t currentTime = time(nullptr);
	for (const auto& it : houseMap) {
		House* house = it.second;
		if (house->getOwner() == 0) {
			continue;
		}

		const int32_t housePrice = g_config.getNumber(ConfigManager::HOUSE_PRICE);
		const uint32_t houseRent = (housePrice > -1) ? (housePrice * house->getTiles().size()) : house->getRent();
		if (houseRent <= 0 || house->getPaidUntil() > currentTime) {
			continue;
		}

		const uint32_t ownerId = house->getOwner();
		const uint32_t townId = house->getTownId();
		const uint32_t houseTownId = 12;
		Town* town = g_game.map.towns.getTown(townId);
		if (!town) {
			g_logger.houseLog(spdlog::level::warn, fmt::format("Could not pay house because town was not found. GUID:{:d}, HouseID:{:d}", ownerId, house->getId()));
			continue;
		}
		
		Town* houseTown = g_game.map.towns.getTown(houseTownId);
		if (!houseTown) {
			g_logger.houseLog(spdlog::level::warn, fmt::format("Could not pay house because house town was not found. GUID:{:d}, HouseID:{:d}", ownerId, house->getId()));
			continue;
		}

		Player player(nullptr);
		if (!IOLoginData::loadPlayerById(&player, ownerId)) {
			// Player doesn't exist, reset house owner
			house->setOwner(0);
			g_logger.houseLog(spdlog::level::warn, fmt::format("Could not pay house because player no longer exists. GUID:{:d}, HouseID:{:d}", ownerId, house->getId()));
			continue;
		}

		bool eligibleToPayRent = true;

		if (g_config.getBoolean(ConfigManager::GUILHALLS_ONLYFOR_LEADERS) &&
			(house->isGuildHall() && !player.getGuildRank() || 
			house->isGuildHall() && player.getGuildRank()->level < 3)) {
			eligibleToPayRent = false;
		}

		if (g_config.getBoolean(ConfigManager::HOUSES_ONLY_PREMIUM) && !player.isPremium()) {
			eligibleToPayRent = false;
		}

		bool paidRent = false;

		if (eligibleToPayRent)
		{
			if (g_config.getBoolean (ConfigManager::HOUSES_BANKSYSTEM))
				{
					if (player.getBankBalance () >= houseRent)
						{
							player.setBankBalance (player.getBankBalance () - houseRent);
							paidRent = true;
							g_logger.houseLog (spdlog::level::info, fmt::format ("{:s} paid house ID {:d} for {:d} rent using bank balance.", player.getName (), house->getId (), houseRent));
						}
					else
						{
							g_logger.houseLog (spdlog::level::info, fmt::format ("{:s} could not pay house ID {:d} for {:d} rent using bank balance.", player.getName (), house->getId (), houseRent));
						}
				}
			else {

				if (DepotLocker *const depotLocker = player.getDepotLocker (houseTownId, false))
				{
					if (!depotLocker->getParent ())
						depotLocker->setParent (&player);

					if (g_game.removeMoney (depotLocker, houseRent))
					{
						paidRent = true;
						g_logger.houseLog (spdlog::level::info, fmt::format ("{:s} paid house ID {:d} for {:d} rent using their home depot.", player.getName (), house->getId (), houseRent));
					}
				}

				if (!paidRent)
				{
					if (DepotLocker *const depotLocker = player.getDepotLocker(townId, false))
					{
						if (!depotLocker->getParent())
							depotLocker->setParent (&player);

						if (g_game.removeMoney(depotLocker, houseRent))
						{
							paidRent = true;
							g_logger.houseLog(spdlog::level::info, fmt::format("{:s} paid house ID {:d} for {:d} rent using their town depot.", player.getName (), house->getId (), houseRent));
						}
						else
						{
							g_logger.houseLog(spdlog::level::info, fmt::format("{:s} could not pay house ID {:d} for {:d} rent using any depot.", player.getName (), house->getId (), houseRent));
						}
					}
				}

			}
		}

		if (paidRent) {

			Database& db = Database::getInstance();
			time_t currentTime = time(nullptr);
			db.executeQuery(fmt::format("INSERT INTO `payment_house_history`(`player_id`, `rent`, `house_id`, `time`, `date_str`) VALUES ({:d}, {:d}, {:d}, {:d}, \"{:s}\")", ownerId, houseRent, house->getId(), currentTime, formatDate(currentTime)));

			time_t paidUntil = house->getPaidUntil();
			switch (rentPeriod) {
				case RENTPERIOD_DAILY:
					paidUntil += 24 * 60 * 60;
					break;
				case RENTPERIOD_WEEKLY:
					paidUntil += 24 * 60 * 60 * 7;
					break;
				case RENTPERIOD_MONTHLY:
					paidUntil += 24 * 60 * 60 * 30;
					break;
				case RENTPERIOD_YEARLY:
					paidUntil += 24 * 60 * 60 * 365;
					break;
				default:
					break;
			}

			house->setPaidUntil(paidUntil);
			house->setPayRentWarnings(0);
		} else {
			if (house->getPayRentWarnings() < 7) {
				int32_t daysLeft = 7 - house->getPayRentWarnings();

				Item* letter = Item::CreateItem(ITEM_LETTER_STAMPED);
				std::string period;

				switch (rentPeriod) {
				case RENTPERIOD_DAILY:
					period = "daily";
					break;

				case RENTPERIOD_WEEKLY:
					period = "weekly";
					break;

				case RENTPERIOD_MONTHLY:
					period = "monthly";
					break;

				case RENTPERIOD_YEARLY:
					period = "annual";
					break;

				default:
					break;
				}
        		g_logger.houseLog(spdlog::level::info, fmt::format("Player did not pay house rent, sending pay warning. GUID:{:d}, Warnings:{:d}, HouseID:{:d}", player.getGUID(), house->getPayRentWarnings(), house->getId()));
				
				letter->setText(fmt::format("Warning! \nThe {:s} rent of {:d} gold for your house \"{:s}\" is payable. Have it available within {:d} days, or you will lose this house.", period, houseRent, house->getName(), daysLeft));
				g_game.internalAddItem(player.getDepotLocker(townId, true), letter, INDEX_WHEREEVER, FLAG_NOLIMIT);
				house->setPayRentWarnings(house->getPayRentWarnings() + 1);

				g_logger.houseLog(spdlog::level::info, fmt::format("{:s} did not pay house rent for {:d} days. House ID {:d}", player.getName(), house->getPayRentWarnings(), house->getId()));
			} else {
        g_logger.houseLog(spdlog::level::info, fmt::format("{:s} did not pay house rent for 7 days in a row and lost its house ID {:d}", player.getName(), house->getId()));
				house->setOwner(0, true, true, &player);
			}
		}

		player.setLastDepotId(house->getTownId());
		IOLoginData::savePlayer(&player, true);
	}
}