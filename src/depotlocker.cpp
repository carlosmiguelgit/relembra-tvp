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

#include "depotlocker.h"
#include "housetile.h"

#include <fmt/format.h>

DepotLocker::DepotLocker(uint16_t type) :
	Container(type, 30), depotId(0), maxDepotItems(2000) {}

Item* DepotLocker::clone() const
{
	DepotLocker* clone = static_cast<DepotLocker*>(Item::clone());
	clone->setDepotId(depotId);
	clone->setMaxDepotItems(maxDepotItems);
	return clone;
}

Attr_ReadValue DepotLocker::readAttr(AttrTypes_t attr, PropStream& propStream)
{
	if (attr == ATTR_DEPOT_ID) {
		if (!propStream.read<uint16_t>(depotId)) {
			return ATTR_READ_ERROR;
		}
		return ATTR_READ_CONTINUE;
	}
	return Item::readAttr(attr, propStream);
}

void DepotLocker::serializeAttr(PropWriteStream& propWriteStream) const
{
	Item::serializeAttr(propWriteStream);
	
	propWriteStream.write<uint8_t>(ATTR_DEPOT_ID);
	propWriteStream.write<uint16_t>(depotId);
}

ReturnValue DepotLocker::queryAdd(int32_t index, const Thing& thing, uint32_t count,
	uint32_t flags, Creature* actor/* = nullptr*/) const
{
	const Item* item = thing.getItem();
	if (item == nullptr) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	bool skipLimit = hasBitSet(FLAG_NOLIMIT, flags);
	if (!skipLimit) {
		int32_t addCount = 0;

		if ((item->isStackable() && item->getItemCount() != count)) {
			addCount = 1;
		}

		if (item->getTopParent() != this) {
			if (const Container* container = item->getContainer()) {
				addCount = container->getItemHoldingCount() + 1;
			}
			else {
				addCount = 1;
			}


			if (getItemHoldingCount() + addCount > maxDepotItems) {
				return RETURNVALUE_DEPOTISFULL;
			}
		}
	}

	return Container::queryAdd(index, thing, count, flags, actor);
}

void DepotLocker::postAddNotification(Thing* thing, const Cylinder* oldParent, int32_t index, cylinderlink_t)
{
	if (parent != nullptr) {
		parent->postAddNotification(thing, oldParent, index, LINK_PARENT);
	}
}

void DepotLocker::postRemoveNotification(Thing* thing, const Cylinder* newParent, int32_t index, cylinderlink_t)
{
	if (parent != nullptr) {
		parent->postRemoveNotification(thing, newParent, index, LINK_PARENT);
	}
}

uint16_t DepotLocker::getDepotId() const {
	if (const HouseTile* const houseTile = dynamic_cast<const HouseTile*>(getTile())) {
		return 12;
	}
	return depotId;
}
