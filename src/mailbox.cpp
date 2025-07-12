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

#include "mailbox.h"
#include "game.h"
#include "iologindata.h"
#include "configmanager.h"
#include "logger.h"

#include <boost/algorithm/string.hpp>

extern ConfigManager g_config;
extern Game g_game;

ReturnValue Mailbox::queryAdd(int32_t, const Thing& thing, uint32_t, uint32_t, Creature* actor) const
{
	const Item* item = thing.getItem();
	if (item && Mailbox::canSend(item)) {
		if (actor) {
			g_logger.gameLog(spdlog::level::info, fmt::format("{:s} sent previous parcel to target player.", actor->getName()), true);
		}
		else {
			g_logger.gameLog(spdlog::level::info, fmt::format("{:s} sent previous parcel to target player.", "unknown player"), true);
		}
		return RETURNVALUE_NOERROR;
	}
	return RETURNVALUE_NOTPOSSIBLE;
}

ReturnValue Mailbox::queryMaxCount(int32_t, const Thing&, uint32_t count, uint32_t& maxQueryCount, uint32_t) const
{
	maxQueryCount = std::max<uint32_t>(1, count);
	return RETURNVALUE_NOERROR;
}

ReturnValue Mailbox::queryRemove(const Thing&, uint32_t, uint32_t, Creature* /*= nullptr */) const
{
	return RETURNVALUE_NOTPOSSIBLE;
}

Cylinder* Mailbox::queryDestination(int32_t&, const Thing&, Item**, uint32_t&)
{
	return this;
}

void Mailbox::addThing(Thing* thing)
{
	return addThing(0, thing);
}

void Mailbox::addThing(int32_t, Thing* thing)
{
	Item* item = thing->getItem();
	if (item && Mailbox::canSend(item)) {
		if (!g_config.getBoolean(ConfigManager::TRASHABLE_MAILBOX)) {
			const TileItemVector* items = getTile()->getItemList();
			if (items) {
				uint32_t movableItemCount = 0;
				for (const Item* item : *items) {
					// if (item->isMoveable()) {
					// 	movableItemCount++;
					// }

					if (movableItemCount > 1) {
						return;
					}
				}
			}
		}
		sendItem(item);
	}
}

void Mailbox::updateThing(Thing*, uint16_t, uint32_t)
{
	//
}

void Mailbox::replaceThing(uint32_t, Thing*)
{
	//
}

void Mailbox::removeThing(Thing*, uint32_t)
{
	//
}

void Mailbox::postAddNotification(Thing* thing, const Cylinder* oldParent, int32_t index, cylinderlink_t)
{
	getParent()->postAddNotification(thing, oldParent, index, LINK_PARENT);
}

void Mailbox::postRemoveNotification(Thing* thing, const Cylinder* newParent, int32_t index, cylinderlink_t)
{
	getParent()->postRemoveNotification(thing, newParent, index, LINK_PARENT);
}

bool Mailbox::sendItem(Item* item) const
{
	std::string receiver, townName;
	if (!getReceiver(item, receiver, townName)) {
		return false;
	}

	/**No need to continue if its still empty**/
	if (receiver.empty()) {
		return false;
	}

	Town* town = g_game.map.towns.getTown(townName);
	if (!town) {
		return false;
	}

	StringVector disabledMailboxes = explodeString(g_config.getString(ConfigManager::DISABLED_MAILBOXES), ",");
	for (const std::string& disabledTownName : disabledMailboxes) {
		if (boost::iequals(disabledTownName, town->getName())) {
			return false;
		}
	}

	Player* player = g_game.getPlayerByName(receiver);
	if (player) {
		DepotLocker* depotLocker = player->getDepotLocker(town->getID(), true);
		if (depotLocker) {
			if (g_game.internalMoveItem(item->getParent(), depotLocker, INDEX_WHEREEVER,
				item, item->getItemCount(), nullptr, FLAG_NOLIMIT) == RETURNVALUE_NOERROR) {
				player->setLastDepotId(town->getID());
				g_game.transformItem(item, item->getID() + 1);
				player->onReceiveMail();

				std::string contentDescription = std::string();
				if (const Container* container = item->getContainer()) {
					contentDescription = container->getContentDescription();
				} else {
					contentDescription = item->getText();
				}
				g_logger.gameLog(spdlog::level::info, fmt::format("Parcel has been sent to {:s}, with content: {:s}", player->getName(), contentDescription), true);
				return true;
			}
		}
	} else {
		Player tmpPlayer(nullptr);
		if (!IOLoginData::loadPlayerByName(&tmpPlayer, receiver)) {
			return false;
		}

		DepotLocker* depotLocker = tmpPlayer.getDepotLocker(town->getID(), true);
		if (depotLocker) {
			if (g_game.internalMoveItem(item->getParent(), depotLocker, INDEX_WHEREEVER,
				item, item->getItemCount(), nullptr, FLAG_NOLIMIT) == RETURNVALUE_NOERROR) {
				tmpPlayer.setLastDepotId(town->getID());
				g_game.transformItem(item, item->getID() + 1);
				IOLoginData::savePlayer(&tmpPlayer, true);
				
				std::string contentDescription = std::string();
				if (const Container* container = item->getContainer()) {
					contentDescription = container->getContentDescription();
				}
				else {
					contentDescription = item->getText();
				}
				g_logger.gameLog(spdlog::level::info, fmt::format("Parcel has been sent to {:s}, with content: {:s}", tmpPlayer.getName(), contentDescription), true);
				return true;
			}
		}
	}
	return false;
}

bool Mailbox::getReceiver(Item* item, std::string& name, std::string& town) const
{
	const Container* container = item->getContainer();
	if (container) {
		for (Item* containerItem : container->getItemList()) {
			if (containerItem->getID() == ITEM_LABEL && getReceiver(containerItem, name, town)) {
				return true;
			}
		}
		return false;
	}

	const std::string& text = item->getText();
	if (text.empty()) {
		return false;
	}

	std::istringstream iss(text, std::istringstream::in);
	std::string temp;
	uint32_t currentLine = 1;

	while (getline(iss, temp, '\n')) {
		if (currentLine == 1) {
			name = temp;
		}
		else if (currentLine == 2) {
			town = temp;
		}
		else {
			break;
		}

		++currentLine;
	}

	trimString(name);
	trimString(town);
	return true;
}

bool Mailbox::canSend(const Item* item)
{
	return item->getID() == ITEM_PARCEL || item->getID() == ITEM_LETTER;
}
