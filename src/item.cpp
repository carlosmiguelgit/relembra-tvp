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

#include "item.h"
#include "container.h"
#include "teleport.h"
#include "trashholder.h"
#include "mailbox.h"
#include "house.h"
#include "game.h"
#include "bed.h"

#include "actions.h"
#include "spells.h"

#include <fmt/format.h>
#include "iomap.h"

extern Game g_game;
extern Spells* g_spells;
extern Vocations g_vocations;

Items Item::items;

Item::~Item()
{
	if (getRealUID() > 0) {
		g_game.removeRealUniqueItem(getRealUID());
	}
}

Item* Item::CreateItem(const uint16_t type, uint16_t count /*= 0*/)
{
	Item* newItem = nullptr;

	const ItemType& it = Item::items[type];
	if (it.group == ITEM_GROUP_DEPRECATED) {
		return nullptr;
	}

	if (it.stackable && count == 0) {
		count = 1;
	}

	if (it.id != 0) {
		if (it.isDepot()) {
			newItem = new DepotLocker(type);
		} else if (it.isContainer()) {
			newItem = new Container(type);
		} else if (it.isTeleport()) {
			newItem = new Teleport(type);
		} else if (it.isMagicField()) {
			newItem = new MagicField(type);
		} else if (it.isDoor()) {
			newItem = new Door(type);
		} else if (it.isTrashHolder()) {
			newItem = new TrashHolder(type);
		} else if (it.isMailbox()) {
			newItem = new Mailbox(type);
		} else if (it.isBed()) {
			newItem = new BedItem(type);
		} else {
			newItem = new Item(type, count);
		}

		if (it.pickupable) {
			newItem->setRealUID(g_game.nextItemUID());
			g_game.addRealUniqueItem(newItem->getRealUID(), newItem);
		}

		newItem->incrementReferenceCounter();
		
		newItem->applyFixedBonuses();
	}

	return newItem;
}

Container* Item::CreateItemAsContainer(const uint16_t type, uint16_t size)
{
	const ItemType& it = Item::items[type];
	if (it.id == 0 || it.group == ITEM_GROUP_DEPRECATED || it.stackable || it.useable || it.moveable || it.pickupable || it.isDepot() || it.isSplash() || it.isDoor()) {
		return nullptr;
	}

	Container* newItem = new Container(type, size);
	newItem->incrementReferenceCounter();
	return newItem;
}

Item* Item::CreateItem(PropStream& propStream)
{
	uint16_t id;
	if (!propStream.read<uint16_t>(id)) {
		return nullptr;
	}

	return Item::CreateItem(id, 0);
}

Item* Item::CreateItem(ScriptReader& scriptReader)
{
	uint16_t id = scriptReader.getNumber();
	return Item::CreateItem(id, 0);
}

Item::Item(const uint16_t type, uint16_t count /*= 0*/) :
	id(type)
{
	const ItemType& it = items[id];

	if (it.isFluidContainer() || it.isSplash()) {
		setFluidType(count);
	} else if (it.stackable) {
		if (count != 0) {
			setItemCount(count);
		} else if (it.charges != 0) {
			setItemCount(it.charges);
		}
	} else if (it.charges != 0 || it.isRune()) {
		if (count != 0) {
			setCharges(count);
		} else {
			setCharges(it.charges);
		}
	} else if (it.isKey()) {
		setKeyNumber(count);
	}

	setDefaultDuration();
}

Item::Item(const Item& i) :
	Thing(), id(i.id), count(i.count)
{
	if (i.attributes) {
		attributes.reset(new ItemAttributes(*i.attributes));
	}
}

Item* Item::clone() const
{
	Item* item = Item::CreateItem(id, count);
	if (attributes) {
		item->attributes.reset(new ItemAttributes(*attributes));
		if (item->getDuration() > 0) {
			item->incrementReferenceCounter();
			item->setDecaying(DECAYING_TRUE);
			g_game.toDecayItems.push_front(item);
		}
	}

	return item;
}

bool Item::equals(const Item* otherItem) const
{
	if (!otherItem || id != otherItem->id) {
		return false;
	}

	const auto& otherAttributes = otherItem->attributes;
	if (!attributes) {
		return !otherAttributes || (otherAttributes->attributeBits == 0);
	} else if (!otherAttributes) {
		return (attributes->attributeBits == 0);
	}

	if (attributes->attributeBits != otherAttributes->attributeBits) {
		return false;
	}

	const auto& attributeList = attributes->attributes;
	const auto& otherAttributeList = otherAttributes->attributes;
	for (const auto& attribute : attributeList) {
		if (ItemAttributes::isStrAttrType(attribute.type)) {
			for (const auto& otherAttribute : otherAttributeList) {
				if (attribute.type == otherAttribute.type && *attribute.value.string != *otherAttribute.value.string) {
					return false;
				}
			}
		} else {
			for (const auto& otherAttribute : otherAttributeList) {
				if (attribute.type == otherAttribute.type && attribute.value.integer != otherAttribute.value.integer) {
					return false;
				}
			}
		}
	}
	return true;
}

void Item::setDefaultSubtype()
{
	const ItemType& it = items[id];

	setItemCount(1);

	if (it.charges != 0) {
		if (it.stackable) {
			setItemCount(it.charges);
		} else {
			setCharges(it.charges);
		}
	}
}

void Item::onRemoved()
{
	ScriptEnvironment::removeTempItem(this);

	if (hasAttribute(ITEM_ATTRIBUTE_UNIQUEID)) {
		g_game.removeUniqueItem(getUniqueId());
	}
}

void Item::setID(uint16_t newid)
{
	const ItemType& prevIt = Item::items[id];
	id = newid;

	const ItemType& it = Item::items[newid];
	uint32_t newDuration = it.decayTime * 1000;

	if (newDuration == 0 && !it.stopTime && it.decayTo < 0) {
		removeAttribute(ITEM_ATTRIBUTE_DECAYSTATE);
		removeAttribute(ITEM_ATTRIBUTE_DURATION);
	}

	removeAttribute(ITEM_ATTRIBUTE_CORPSEOWNER);

	if (newDuration > 0 && (!prevIt.stopTime || !hasAttribute(ITEM_ATTRIBUTE_DURATION))) {
		setDecaying(DECAYING_FALSE);
		setDuration(newDuration);
	}
}

void Item::decrementReferenceCounter() 
{
	if (--referenceCounter == 0) {
		// In the given situation that item is to be deleted but still has a parent, skip deletion
		if (Cylinder* parentCylinder = getParent()) {
            if (parentCylinder != VirtualCylinder::virtualCylinder) {
				bool parentTile = parentCylinder->getTile() != nullptr;
				bool parentContainer = parentCylinder->getContainer() != nullptr;
				bool parentPlayer = !parentTile && !parentContainer;

				const Position& pos = getPosition();
				std::cout << fmt::format("ERROR - Item::decrementReferenceCounter: Item was to be deleted {:s}:{:d} ~ ({:d},{:d},{:d}) ~ Pos ({:d},{:d},{:d}) but is still present in a parent cylinder.", getName(), getID(), parentTile, parentContainer, parentPlayer, pos.x, pos.y, static_cast<int>(pos.z)) << std::endl;
				// Since reference counter will be 1
				// It will be deleted during cylinder destructors anyway and proceed without warning
				// And if it was not to be deleted, it will be less than 10 bytes kept in memory.
				referenceCounter++;
				return;
			}
		}
		delete this;
	}
}

Cylinder* Item::getTopParent()
{
	Cylinder* aux = getParent();
	Cylinder* prevaux = dynamic_cast<Cylinder*>(this);
	if (!aux) {
		return prevaux;
	}

	while (aux->getParent() != nullptr) {
		prevaux = aux;
		aux = aux->getParent();
	}

	if (prevaux) {
		return prevaux;
	}
	return aux;
}

const Cylinder* Item::getTopParent() const
{
	const Cylinder* aux = getParent();
	const Cylinder* prevaux = dynamic_cast<const Cylinder*>(this);
	if (!aux) {
		return prevaux;
	}

	while (aux->getParent() != nullptr) {
		prevaux = aux;
		aux = aux->getParent();
	}

	if (prevaux) {
		return prevaux;
	}
	return aux;
}

Tile* Item::getTile()
{
	Cylinder* cylinder = getTopParent();
	//get root cylinder
	if (cylinder && cylinder->getParent()) {
		cylinder = cylinder->getParent();
	}
	return dynamic_cast<Tile*>(cylinder);
}

const Tile* Item::getTile() const
{
	const Cylinder* cylinder = getTopParent();
	//get root cylinder
	if (cylinder && cylinder->getParent()) {
		cylinder = cylinder->getParent();
	}
	return dynamic_cast<const Tile*>(cylinder);
}

uint16_t Item::getSubType() const
{
	const ItemType& it = items[id];
	if (it.isFluidContainer() || it.isSplash()) {
		return getFluidType();
	} else if (it.stackable) {
		return count;
	} else if (it.charges != 0 || it.isRune()) {
		return getCharges();
	}
	return count;
}

Player* Item::getHoldingPlayer() const
{
	Cylinder* p = getParent();
	while (p) {
		if (p->getCreature()) {
			return p->getCreature()->getPlayer();
		}

		p = p->getParent();
	}
	return nullptr;
}

void Item::setSubType(uint16_t n)
{
	const ItemType& it = items[id];
	if (it.isFluidContainer() || it.isSplash()) {
		setFluidType(n);
	} else if (it.stackable) {
		setItemCount(n);
	} else if (it.charges != 0 || it.isRune()) {
		setCharges(n);
	} else {
		setItemCount(n);
	}
}

Attr_ReadValue Item::readAttr(AttrTypes_t attr, PropStream& propStream)
{
	switch (attr) {
		case ATTR_COUNT:
		case ATTR_RUNE_CHARGES: {
			uint8_t count;
			if (!propStream.read<uint8_t>(count)) {
				return ATTR_READ_ERROR;
			}

			setSubType(count);
			break;
		}

		case ATTR_ACTION_ID: {
			uint16_t actionId;
			if (!propStream.read<uint16_t>(actionId)) {
				return ATTR_READ_ERROR;
			}

			setActionId(actionId);
			break;
		}

		case ATTR_UNIQUE_ID: {
			uint16_t uniqueId;
			if (!propStream.read<uint16_t>(uniqueId)) {
				return ATTR_READ_ERROR;
			}

			setUniqueId(uniqueId);
			break;
		}

		case ATTR_TEXT: {
			std::string text;
			if (!propStream.readString(text)) {
				return ATTR_READ_ERROR;
			}

			setText(text);
			break;
		}

		case ATTR_WRITTENDATE: {
			uint32_t writtenDate;
			if (!propStream.read<uint32_t>(writtenDate)) {
				return ATTR_READ_ERROR;
			}

			setDate(writtenDate);
			break;
		}

		case ATTR_WRITTENBY: {
			std::string writer;
			if (!propStream.readString(writer)) {
				return ATTR_READ_ERROR;
			}

			setWriter(writer);
			break;
		}

		case ATTR_DESC: {
			std::string text;
			if (!propStream.readString(text)) {
				return ATTR_READ_ERROR;
			}

			setSpecialDescription(text);
			break;
		}

		case ATTR_CHARGES: {
			uint16_t charges;
			if (!propStream.read<uint16_t>(charges)) {
				return ATTR_READ_ERROR;
			}

			setSubType(charges);
			break;
		}

		case ATTR_DURATION: {
			int32_t duration;
			if (!propStream.read<int32_t>(duration)) {
				return ATTR_READ_ERROR;
			}

			setDuration(std::max<int32_t>(0, duration));
			break;
		}

		case ATTR_DECAYING_STATE: {
			uint8_t state;
			if (!propStream.read<uint8_t>(state)) {
				return ATTR_READ_ERROR;
			}

			if (state != DECAYING_FALSE) {
				setDecaying(DECAYING_PENDING);
			}
			break;
		}

		case ATTR_NAME: {
			std::string name;
			if (!propStream.readString(name)) {
				return ATTR_READ_ERROR;
			}

			setStrAttr(ITEM_ATTRIBUTE_NAME, name);
			break;
		}

		case ATTR_ARTICLE: {
			std::string article;
			if (!propStream.readString(article)) {
				return ATTR_READ_ERROR;
			}

			setStrAttr(ITEM_ATTRIBUTE_ARTICLE, article);
			break;
		}

		case ATTR_PLURALNAME: {
			std::string pluralName;
			if (!propStream.readString(pluralName)) {
				return ATTR_READ_ERROR;
			}

			setStrAttr(ITEM_ATTRIBUTE_PLURALNAME, pluralName);
			break;
		}

		case ATTR_WEIGHT: {
			uint32_t weight;
			if (!propStream.read<uint32_t>(weight)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_WEIGHT, weight);
			break;
		}

		case ATTR_ATTACK: {
			int32_t attack;
			if (!propStream.read<int32_t>(attack)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_ATTACK, attack);
			break;
		}

		case ATTR_ATTACK_SPEED: {
			uint32_t attackSpeed;
			if (!propStream.read<uint32_t>(attackSpeed)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_ATTACK_SPEED, attackSpeed);
			break;
		}

		case ATTR_DEFENSE: {
			int32_t defense;
			if (!propStream.read<int32_t>(defense)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_DEFENSE, defense);
			break;
		}

		case ATTR_EXTRADEFENSE: {
			int32_t extraDefense;
			if (!propStream.read<int32_t>(extraDefense)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_EXTRADEFENSE, extraDefense);
			break;
		}

		case ATTR_ARMOR: {
			int32_t armor;
			if (!propStream.read<int32_t>(armor)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_ARMOR, armor);
			break;
		}

		case ATTR_HITCHANCE: {
			int8_t hitChance;
			if (!propStream.read<int8_t>(hitChance)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_HITCHANCE, hitChance);
			break;
		}

		case ATTR_SHOOTRANGE: {
			uint8_t shootRange;
			if (!propStream.read<uint8_t>(shootRange)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_SHOOTRANGE, shootRange);
			break;
		}

		case ATTR_DECAYTO: {
			int32_t decayTo;
			if (!propStream.read<int32_t>(decayTo)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_DECAYTO, decayTo);
			break;
		}

		case ATTR_KEYNUMBER: {
			uint16_t keyNumber;
			if (!propStream.read<uint16_t>(keyNumber)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_KEYNUMBER, keyNumber);
			break;
		}

		case ATTR_KEYHOLENUMBER: {
			uint16_t keyHoleNumber;
			if (!propStream.read<uint16_t>(keyHoleNumber)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_KEYHOLENUMBER, keyHoleNumber);
			break;
		}

		case ATTR_DOORQUESTNUMBER: {
			uint16_t doorQuestNumber;
			if (!propStream.read<uint16_t>(doorQuestNumber)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_DOORQUESTNUMBER, doorQuestNumber);
			break;
		}

		case ATTR_DOORQUESTVALUE: {
			uint16_t doorQuestValue;
			if (!propStream.read<uint16_t>(doorQuestValue)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_DOORQUESTVALUE, doorQuestValue);
			break;
		}

		case ATTR_DOORLEVEL: {
			uint16_t doorLevel;
			if (!propStream.read<uint16_t>(doorLevel)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_DOORLEVEL, doorLevel);
			break;
		}

		case ATTR_AUTOOPEN: {
			int8_t autoOpen;
			if (!propStream.read<int8_t>(autoOpen)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_AUTOOPEN, autoOpen);
			break;
		}

		//these should be handled through derived classes
		//If these are called then something has changed in the items.xml since the map was saved
		//just read the values

		//Depot class
		case ATTR_DEPOT_ID: {
			if (!propStream.skip(2)) {
				return ATTR_READ_ERROR;
			}
			break;
		}

		//Door class
		case ATTR_HOUSEDOORID: {
			if (!propStream.skip(1)) {
				return ATTR_READ_ERROR;
			}
			break;
		}

		//Bed class
		case ATTR_SLEEPERGUID: {
			if (!propStream.skip(4)) {
				return ATTR_READ_ERROR;
			}
			break;
		}

		case ATTR_SLEEPSTART: {
			if (!propStream.skip(4)) {
				return ATTR_READ_ERROR;
			}
			break;
		}

		//Teleport class
		case ATTR_TELE_DEST: {
			if (!propStream.skip(5)) {
				return ATTR_READ_ERROR;
			}
			break;
		}

		//Container class
		case ATTR_CONTAINER_ITEMS: {
			return ATTR_READ_ERROR;
		}

		case ATTR_CUSTOM_ATTRIBUTES: {
			uint64_t size;
			if (!propStream.read<uint64_t>(size)) {
				return ATTR_READ_ERROR;
			}

			for (uint64_t i = 0; i < size; i++) {
				// Unserialize key type and value
				std::string key;
				if (!propStream.readString(key)) {
					return ATTR_READ_ERROR;
				};

				// Unserialize value type and value
				ItemAttributes::CustomAttribute val;
				if (!val.unserialize(propStream)) {
					return ATTR_READ_ERROR;
				}

				setCustomAttribute(key, val);
			}
			break;
		}

		default:
			return ATTR_READ_ERROR;
	}

	return ATTR_READ_CONTINUE;
}

Attr_ReadValue Item::readAttr2(AttrTypes_t attr, PropStream& propStream)
{
	switch (attr) {
		case ATTR_COUNT:
		case ATTR_RUNE_CHARGES: {
			uint8_t count;
			if (!propStream.read<uint8_t>(count)) {
				return ATTR_READ_ERROR;
			}

			setSubType(count);
			break;
		}

		case ATTR_ACTION_ID: {
			uint16_t actionId;
			if (!propStream.read<uint16_t>(actionId)) {
				return ATTR_READ_ERROR;
			}

			setActionId(actionId);
			break;
		}

		case ATTR_UNIQUE_ID: {
			uint32_t uniqueId;
			if (!propStream.read<uint32_t>(uniqueId)) {
				return ATTR_READ_ERROR;
			}

			setUniqueId(uniqueId);
			break;
		}

		case ATTR_TEXT: {
			std::string text;
			if (!propStream.readString(text)) {
				return ATTR_READ_ERROR;
			}

			setText(text);
			break;
		}

		case ATTR_WRITTENDATE: {
			uint32_t writtenDate;
			if (!propStream.read<uint32_t>(writtenDate)) {
				return ATTR_READ_ERROR;
			}

			setDate(writtenDate);
			break;
		}

		case ATTR_WRITTENBY: {
			std::string writer;
			if (!propStream.readString(writer)) {
				return ATTR_READ_ERROR;
			}

			setWriter(writer);
			break;
		}

		case ATTR_DESC: {
			std::string text;
			if (!propStream.readString(text)) {
				return ATTR_READ_ERROR;
			}

			setSpecialDescription(text);
			break;
		}

		case ATTR_CHARGES: {
			uint16_t charges;
			if (!propStream.read<uint16_t>(charges)) {
				return ATTR_READ_ERROR;
			}

			setSubType(charges);
			break;
		}

		case ATTR_DURATION: {
			int32_t duration;
			if (!propStream.read<int32_t>(duration)) {
				return ATTR_READ_ERROR;
			}

			setDuration(std::max<int32_t>(0, duration));
			break;
		}

		case ATTR_DECAYING_STATE: {
			uint8_t state;
			if (!propStream.read<uint8_t>(state)) {
				return ATTR_READ_ERROR;
			}

			if (state != DECAYING_FALSE) {
				setDecaying(DECAYING_PENDING);
			}
			break;
		}

		case ATTR_NAME: {
			std::string name;
			if (!propStream.readString(name)) {
				return ATTR_READ_ERROR;
			}

			setStrAttr(ITEM_ATTRIBUTE_NAME, name);
			break;
		}

		case ATTR_ARTICLE: {
			std::string article;
			if (!propStream.readString(article)) {
				return ATTR_READ_ERROR;
			}

			setStrAttr(ITEM_ATTRIBUTE_ARTICLE, article);
			break;
		}

		case ATTR_PLURALNAME: {
			std::string pluralName;
			if (!propStream.readString(pluralName)) {
				return ATTR_READ_ERROR;
			}

			setStrAttr(ITEM_ATTRIBUTE_PLURALNAME, pluralName);
			break;
		}

		case ATTR_WEIGHT: {
			uint32_t weight;
			if (!propStream.read<uint32_t>(weight)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_WEIGHT, weight);
			break;
		}

		case ATTR_ATTACK: {
			int32_t attack;
			if (!propStream.read<int32_t>(attack)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_ATTACK, attack);
			break;
		}

		case ATTR_ATTACK_SPEED: {
			uint32_t attackSpeed;
			if (!propStream.read<uint32_t>(attackSpeed)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_ATTACK_SPEED, attackSpeed);
			break;
		}

		case ATTR_DEFENSE: {
			int32_t defense;
			if (!propStream.read<int32_t>(defense)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_DEFENSE, defense);
			break;
		}

		case ATTR_EXTRADEFENSE: {
			int32_t extraDefense;
			if (!propStream.read<int32_t>(extraDefense)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_EXTRADEFENSE, extraDefense);
			break;
		}

		case ATTR_ARMOR: {
			int32_t armor;
			if (!propStream.read<int32_t>(armor)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_ARMOR, armor);
			break;
		}

		case ATTR_HITCHANCE: {
			int8_t hitChance;
			if (!propStream.read<int8_t>(hitChance)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_HITCHANCE, hitChance);
			break;
		}

		case ATTR_SHOOTRANGE: {
			uint8_t shootRange;
			if (!propStream.read<uint8_t>(shootRange)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_SHOOTRANGE, shootRange);
			break;
		}

		case ATTR_DECAYTO: {
			int32_t decayTo;
			if (!propStream.read<int32_t>(decayTo)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_DECAYTO, decayTo);
			break;
		}

		case ATTR_KEYNUMBER: {
			uint16_t keyNumber;
			if (!propStream.read<uint16_t>(keyNumber)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_KEYNUMBER, keyNumber);
			break;
		}

		case ATTR_KEYHOLENUMBER: {
			uint16_t keyHoleNumber;
			if (!propStream.read<uint16_t>(keyHoleNumber)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_KEYHOLENUMBER, keyHoleNumber);
			break;
		}

		case ATTR_DOORQUESTNUMBER: {
			uint16_t doorQuestNumber;
			if (!propStream.read<uint16_t>(doorQuestNumber)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_DOORQUESTNUMBER, doorQuestNumber);
			break;
		}

		case ATTR_DOORQUESTVALUE: {
			uint16_t doorQuestValue;
			if (!propStream.read<uint16_t>(doorQuestValue)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_DOORQUESTVALUE, doorQuestValue);
			break;
		}

		case ATTR_DOORLEVEL: {
			uint16_t doorLevel;
			if (!propStream.read<uint16_t>(doorLevel)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_DOORLEVEL, doorLevel);
			break;
		}

		case ATTR_AUTOOPEN: {
			int8_t autoOpen;
			if (!propStream.read<int8_t>(autoOpen)) {
				return ATTR_READ_ERROR;
			}

			setIntAttr(ITEM_ATTRIBUTE_AUTOOPEN, autoOpen);
			break;
		}

		//these should be handled through derived classes
		//If these are called then something has changed in the items.xml since the map was saved
		//just read the values

		//Depot class
		case ATTR_DEPOT_ID: {
			if (!propStream.skip(2)) {
				return ATTR_READ_ERROR;
			}
			break;
		}

		//Door class
		case ATTR_HOUSEDOORID: {
			if (!propStream.skip(1)) {
				return ATTR_READ_ERROR;
			}
			break;
		}

		//Bed class
		case ATTR_SLEEPERGUID: {
			if (!propStream.skip(4)) {
				return ATTR_READ_ERROR;
			}
			break;
		}

		case ATTR_SLEEPSTART: {
			if (!propStream.skip(4)) {
				return ATTR_READ_ERROR;
			}
			break;
		}

		//Teleport class
		case ATTR_TELE_DEST: {
			if (!propStream.skip(5)) {
				return ATTR_READ_ERROR;
			}
			break;
		}

		//Container class
		case ATTR_CONTAINER_ITEMS: {
			return ATTR_READ_ERROR;
		}

		case ATTR_CUSTOM_ATTRIBUTES: {
			uint64_t size;
			if (!propStream.read<uint64_t>(size)) {
				return ATTR_READ_ERROR;
			}

			for (uint64_t i = 0; i < size; i++) {
				// Unserialize key type and value
				std::string key;
				if (!propStream.readString(key)) {
					return ATTR_READ_ERROR;
				};

				// Unserialize value type and value
				ItemAttributes::CustomAttribute val;
				if (!val.unserialize(propStream)) {
					return ATTR_READ_ERROR;
				}

				setCustomAttribute(key, val);
			}
			break;
		}

		default:
			return ATTR_READ_ERROR;
	}

	return ATTR_READ_CONTINUE;
}

bool Item::unserializeAttr(PropStream& propStream, bool uid)
{
	uint8_t attr_type;
	while (propStream.read<uint8_t>(attr_type) && attr_type != 0) {
		Attr_ReadValue ret = ATTR_READ_ERROR;
		if (!uid) {
			ret = readAttr(static_cast<AttrTypes_t>(attr_type), propStream);
		} else {
			ret = readAttr2(static_cast<AttrTypes_t>(attr_type), propStream);
		}
		if (ret == ATTR_READ_ERROR) {
			return false;
		} else if (ret == ATTR_READ_END) {
			return true;
		}
	}
	return true;
}

bool Item::unserializeTVPFormat(PropStream& propStream)
{
	propStream.skip(2);
	if (!unserializeAttr(propStream)) {
		return false;
	}
	propStream.skip(1);
	// 0 already skipped on unserializeAttr

	if (Container* container = getContainer()) {
		uint32_t totalItems = 0;
		if (!propStream.read<uint32_t>(totalItems)) {
			return false;
		}

		for (uint32_t i = 0; i < totalItems; i++) {
			Item* item = Item::CreateItem(propStream);
			if (!item) {
				return false;
			}

			if (!item->unserializeTVPFormat(propStream)) {
				delete item;
				return false;
			}

			container->addItemBack(item);
		}
	}

	return true;
}

bool Item::unserializeItemNode(OTB::Loader&, const OTB::Node&, PropStream& propStream)
{
	return unserializeAttr(propStream, false);
}

void Item::serializeAttr(PropWriteStream& propWriteStream) const
{
	const ItemType& it = items[id];
	if (it.stackable || it.isFluidContainer() || it.isSplash()) {
		propWriteStream.write<uint8_t>(ATTR_COUNT);
		propWriteStream.write<uint8_t>(getSubType());
	}

	uint16_t charges = getCharges();
	if (charges != 0) {
		propWriteStream.write<uint8_t>(ATTR_CHARGES);
		propWriteStream.write<uint16_t>(charges);
	}

	uint16_t actionId = getActionId();
	if (actionId != 0) {
		propWriteStream.write<uint8_t>(ATTR_ACTION_ID);
		propWriteStream.write<uint16_t>(actionId);
	}

	uint32_t uniqueId = getUniqueId();
	if (actionId != 0) {
		propWriteStream.write<uint8_t>(ATTR_UNIQUE_ID);
		propWriteStream.write<uint32_t>(uniqueId);
	}

	const std::string& text = getText();
	if (!text.empty()) {
		propWriteStream.write<uint8_t>(ATTR_TEXT);
		propWriteStream.writeString(text);
	}

	const time_t writtenDate = getDate();
	if (writtenDate != 0) {
		propWriteStream.write<uint8_t>(ATTR_WRITTENDATE);
		propWriteStream.write<uint32_t>(writtenDate);
	}

	const std::string& writer = getWriter();
	if (!writer.empty()) {
		propWriteStream.write<uint8_t>(ATTR_WRITTENBY);
		propWriteStream.writeString(writer);
	}

	const std::string& specialDesc = getSpecialDescription();
	if (!specialDesc.empty()) {
		propWriteStream.write<uint8_t>(ATTR_DESC);
		propWriteStream.writeString(specialDesc);
	}

	if (hasAttribute(ITEM_ATTRIBUTE_DURATION)) {
		propWriteStream.write<uint8_t>(ATTR_DURATION);
		propWriteStream.write<uint32_t>(getIntAttr(ITEM_ATTRIBUTE_DURATION));
	}

	ItemDecayState_t decayState = getDecaying();
	if (decayState == DECAYING_TRUE || decayState == DECAYING_PENDING) {
		propWriteStream.write<uint8_t>(ATTR_DECAYING_STATE);
		propWriteStream.write<uint8_t>(decayState);
	}

	if (hasAttribute(ITEM_ATTRIBUTE_NAME)) {
		propWriteStream.write<uint8_t>(ATTR_NAME);
		propWriteStream.writeString(getStrAttr(ITEM_ATTRIBUTE_NAME));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_ARTICLE)) {
		propWriteStream.write<uint8_t>(ATTR_ARTICLE);
		propWriteStream.writeString(getStrAttr(ITEM_ATTRIBUTE_ARTICLE));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_PLURALNAME)) {
		propWriteStream.write<uint8_t>(ATTR_PLURALNAME);
		propWriteStream.writeString(getStrAttr(ITEM_ATTRIBUTE_PLURALNAME));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_WEIGHT)) {
		propWriteStream.write<uint8_t>(ATTR_WEIGHT);
		propWriteStream.write<uint32_t>(getIntAttr(ITEM_ATTRIBUTE_WEIGHT));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_ATTACK)) {
		propWriteStream.write<uint8_t>(ATTR_ATTACK);
		propWriteStream.write<int32_t>(getIntAttr(ITEM_ATTRIBUTE_ATTACK));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_ATTACK_SPEED)) {
		propWriteStream.write<uint8_t>(ATTR_ATTACK_SPEED);
		propWriteStream.write<uint32_t>(getIntAttr(ITEM_ATTRIBUTE_ATTACK_SPEED));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_DEFENSE)) {
		propWriteStream.write<uint8_t>(ATTR_DEFENSE);
		propWriteStream.write<int32_t>(getIntAttr(ITEM_ATTRIBUTE_DEFENSE));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_EXTRADEFENSE)) {
		propWriteStream.write<uint8_t>(ATTR_EXTRADEFENSE);
		propWriteStream.write<int32_t>(getIntAttr(ITEM_ATTRIBUTE_EXTRADEFENSE));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_ARMOR)) {
		propWriteStream.write<uint8_t>(ATTR_ARMOR);
		propWriteStream.write<int32_t>(getIntAttr(ITEM_ATTRIBUTE_ARMOR));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_HITCHANCE)) {
		propWriteStream.write<uint8_t>(ATTR_HITCHANCE);
		propWriteStream.write<int8_t>(getIntAttr(ITEM_ATTRIBUTE_HITCHANCE));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_SHOOTRANGE)) {
		propWriteStream.write<uint8_t>(ATTR_SHOOTRANGE);
		propWriteStream.write<uint8_t>(getIntAttr(ITEM_ATTRIBUTE_SHOOTRANGE));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_DECAYTO)) {
		propWriteStream.write<uint8_t>(ATTR_DECAYTO);
		propWriteStream.write<int32_t>(getIntAttr(ITEM_ATTRIBUTE_DECAYTO));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_KEYNUMBER)) {
		propWriteStream.write<uint8_t>(ATTR_KEYNUMBER);
		propWriteStream.write<int16_t>(getIntAttr(ITEM_ATTRIBUTE_KEYNUMBER));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_KEYHOLENUMBER)) {
		propWriteStream.write<uint8_t>(ATTR_KEYHOLENUMBER);
		propWriteStream.write<int16_t>(getIntAttr(ITEM_ATTRIBUTE_KEYHOLENUMBER));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_DOORLEVEL)) {
		propWriteStream.write<uint8_t>(ATTR_DOORLEVEL);
		propWriteStream.write<int16_t>(getIntAttr(ITEM_ATTRIBUTE_DOORLEVEL));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_DOORQUESTNUMBER)) {
		propWriteStream.write<uint8_t>(ATTR_DOORQUESTNUMBER);
		propWriteStream.write<int16_t>(getIntAttr(ITEM_ATTRIBUTE_DOORQUESTNUMBER));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_DOORQUESTVALUE)) {
		propWriteStream.write<uint8_t>(ATTR_DOORQUESTVALUE);
		propWriteStream.write<int16_t>(getIntAttr(ITEM_ATTRIBUTE_DOORQUESTVALUE));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_AUTOOPEN)) {
		propWriteStream.write<uint8_t>(ATTR_AUTOOPEN);
		propWriteStream.write<int8_t>(getIntAttr(ITEM_ATTRIBUTE_AUTOOPEN));
	}

	if (hasAttribute(ITEM_ATTRIBUTE_CUSTOM)) {
		const ItemAttributes::CustomAttributeMap* customAttrMap = attributes->getCustomAttributeMap();
		propWriteStream.write<uint8_t>(ATTR_CUSTOM_ATTRIBUTES);
		propWriteStream.write<uint64_t>(static_cast<uint64_t>(customAttrMap->size()));
		for (const auto &entry : *customAttrMap) {
			// Serializing key type and value
			propWriteStream.writeString(entry.first);

			// Serializing value type and value
			entry.second.serialize(propWriteStream);
		}
	}
}

void Item::serializeTVPFormat(PropWriteStream& propWriteStream) const
{
	propWriteStream.write<uint16_t>(getID());
	propWriteStream.write<uint16_t>(0); // attr begin
	serializeAttr(propWriteStream);
	propWriteStream.write<uint16_t>(0); // attr end

	if (const Container* container = getContainer()) {
		propWriteStream.write<uint32_t>(container->size());
		for (Item* item : container->getItemList()) {
			item->serializeTVPFormat(propWriteStream);
		}
	}
}

bool Item::hasProperty(ITEMPROPERTY prop) const
{
	const ItemType& it = items[id];
	switch (prop) {
		case CONST_PROP_BLOCKSOLID: return it.blockSolid;
		case CONST_PROP_MOVEABLE: return it.moveable && !hasAttribute(ITEM_ATTRIBUTE_UNIQUEID);
		case CONST_PROP_HASHEIGHT: return it.hasHeight;
		case CONST_PROP_BLOCKPROJECTILE: return it.blockProjectile;
		case CONST_PROP_BLOCKPATH: return it.blockPathFind;
		case CONST_PROP_ISVERTICAL: return it.isVertical;
		case CONST_PROP_ISHORIZONTAL: return it.isHorizontal;
		case CONST_PROP_IMMOVABLEBLOCKSOLID: return it.blockSolid && (!it.moveable || hasAttribute(ITEM_ATTRIBUTE_UNIQUEID) || (getActionId() >= 1000 && getActionId() <= 2000));
		case CONST_PROP_IMMOVABLEBLOCKPATH: return it.blockPathFind && (!it.moveable || hasAttribute(ITEM_ATTRIBUTE_UNIQUEID) || getActionId() >= 1000 && getActionId() <= 2000);
		case CONST_PROP_IMMOVABLENOFIELDBLOCKPATH: return !it.isMagicField() && it.blockPathFind && (!it.moveable || hasAttribute(ITEM_ATTRIBUTE_UNIQUEID) || getActionId() >= 1000 && getActionId() <= 2000);
		case CONST_PROP_NOFIELDBLOCKPATH: return !it.isMagicField() && it.blockPathFind;
		case CONST_PROP_SUPPORTHANGABLE: return it.isHorizontal || it.isVertical;
		case CONST_PROP_SPECIALFIELDBLOCKPATH: return it.specialFieldBlockPath;
		default: return false;
	}
}

uint32_t Item::getWeight() const
{
		if (hasAttribute(ITEM_ATTRIBUTE_WEIGHT)) {
		return getIntAttr(ITEM_ATTRIBUTE_WEIGHT);
	}
	
	uint32_t weight = getBaseWeight();
	if (isStackable()) {
		return weight * std::max<uint32_t>(1, getItemCount());
	}
	return weight;
}

int32_t getRarityAttributeRequiredLevelFunction(ItemRarityLevel rarityLevel, ItemTierLevel tierLevel)
{
	if (tierLevel == ITEM_TIER_1)
	{
		if (rarityLevel == ITEM_UNCOMMON)
			return 10;
		else if (rarityLevel == ITEM_RARE)
			return 10;
		else if (rarityLevel == ITEM_EPIC)
			return 10;
		else if (rarityLevel == ITEM_LEGENDARY)
			return 10;
	}
	else if (tierLevel == ITEM_TIER_2)
	{
		if (rarityLevel == ITEM_UNCOMMON)
			return 15;
		else if (rarityLevel == ITEM_RARE)
			return 20;
		else if (rarityLevel == ITEM_EPIC)
			return 25;
		else if (rarityLevel == ITEM_LEGENDARY)
			return 30;
	}
	else if (tierLevel == ITEM_TIER_3)
	{
		{
			if (rarityLevel == ITEM_UNCOMMON)
				return 35;
			else if (rarityLevel == ITEM_RARE)
				return 40;
			else if (rarityLevel == ITEM_EPIC)
				return 45;
			else if (rarityLevel == ITEM_LEGENDARY)
				return 50;
		}
	}
	else if (tierLevel == ITEM_TIER_4)
	{
		if (rarityLevel == ITEM_UNCOMMON)
			return 55;
		else if (rarityLevel == ITEM_RARE)
			return 60;
		else if (rarityLevel == ITEM_EPIC)
			return 65;
		else if (rarityLevel == ITEM_LEGENDARY)
			return 70;
	}

	return 0;
}

int64_t Item::getItemRarity()
{
	auto it = getCustomAttribute("ry");
	if (!it) {
		return ITEM_COMMON;
	}

	return boost::get<int64_t>(it->value);
}

int64_t Item::getItemTierRarity()
{
	auto it = getCustomAttribute("tr");
	if (!it) {
		return updateItemAttributes(nullptr);
	}

	return boost::get<int64_t>(it->value);
}

int32_t Item::getRarityAttributeRequiredLevel()
{
	return getRarityAttributeRequiredLevelFunction((ItemRarityLevel)getItemRarity(), (ItemTierLevel)getItemTierRarity());
}

std::string Item::getDescription(const ItemType& it, int32_t lookDistance,
                                 const Item* item /*= nullptr*/, int32_t subType /*= -1*/, bool addArticle /*= true*/)
{
	std::ostringstream s;
	s << getNameDescription(it, item, subType, addArticle);

	if (item) {
		subType = item->getSubType();
	}

	int32_t armor = it.armor;
	if (item) {
		int32_t statBonus = const_cast<Item*>(item)->getIntCustomAttribute(nullptr, std::to_string(ITEM_RND_ARMOR));
		if (statBonus > 0)
			armor += statBonus;
		else
			armor = item->getArmor();
	}

	if (it.isRune()) {
		uint32_t charges = std::max(static_cast<uint32_t>(1), static_cast<uint32_t>(item == nullptr ? it.charges : item->getCharges()));

		if (it.runeLevel > 0) {
			s << " for level " << it.runeLevel;
		}

		if (it.runeLevel > 0) {
			s << " and";
		}

		s << " for level " << it.runeMagLevel;
		s << ". It's an \"" << it.runeSpellName << "\"-spell (" << charges << "x). ";
	} else if (it.isDoor() && item) {
		if (item->hasAttribute(ITEM_ATTRIBUTE_DOORLEVEL)) {
			s << " for level " << item->getIntAttr(ITEM_ATTRIBUTE_DOORLEVEL);
		}
		s << ".";
	} else if (it.weaponType != WEAPON_NONE) {
		int32_t attack = it.attack;
		if (item) {
			int32_t statBonus = const_cast<Item*>(item)->getIntCustomAttribute(nullptr, std::to_string(ITEM_RND_ATTACK));
			if (statBonus > 0)
				attack += statBonus;
			else
				attack = item->getAttack();
		}

		int32_t defense = it.defense;
		if (item) {
			int32_t statBonus = const_cast<Item*>(item)->getIntCustomAttribute(nullptr, std::to_string(ITEM_RND_DEF));
			if (statBonus > 0)
				defense += statBonus;
			else
				defense = item->getDefense();
		}

		if (it.weaponType == WEAPON_DISTANCE && it.ammoType != AMMO_NONE) {
			if (it.attack != 0) {
				s << "\n(Atk: " << attack;
				if (it.hitChance != 0) {
					s << ", hit chance: +" << static_cast<int>(it.hitChance) << "%";
				}
				s << ")";
			} else {
				if (it.hitChance != 0) {
					s << "\n(hit chance: +" << static_cast<int>(it.hitChance) << "%)";
				}
			}
		} else if (it.weaponType != WEAPON_AMMO && it.weaponType != WEAPON_WAND && (it.attack != 0 || it.defense != 0)) {
			s << " (";
			s << "Atk:" << static_cast<int>(attack);

			if (it.defense != 0) {
				s << " ";
				s << "Def:" << static_cast<int>(defense);
			}

			s << ")";
		}
		s << ".";
	} else if (armor != 0) {
		if (it.charges > 0) {
			if (subType > 1) {
				s << " that has " << static_cast<int32_t>(subType) << " charges left";
			} else {
				s << " that has " << it.charges << " charge left";
			}
		}

		s << " (Arm:" << armor << ").";
	} else if (it.isFluidContainer()) {
		if (item && item->getFluidType() != 0) {
			s << " of " << items[item->getFluidType()].name << ".";
		} else {
			s << ". It is empty.";
		}
	} else if (it.isSplash()) {
		s << " of ";
		if (item && item->getFluidType() != 0) {
			s << items[item->getFluidType()].name;
		} else {
			s << items[1].name;
		}
		s << ".";
	} else if (it.isContainer()) {
		s << " (Vol:" << static_cast<int>(it.maxItems) << ").";
	} else if (it.isKey()) {
		if (item) {
			s << " (Key:" << static_cast<int>(item->getIntAttr(ITEM_ATTRIBUTE_KEYNUMBER)) << ").";
		} else {
			s << " (Key:0).";
		}
	} else if (it.allowDistRead) {
		s << "." << std::endl;

		if (item && item->getText() != "") {
			if (lookDistance <= 4) {
				const std::string& writer = item->getWriter();
				if (!writer.empty()) {
					s << writer << " wrote";
					time_t date = item->getDate();
					if (date != 0) {
						s << " on " << formatDateShort(date);
					}
					s << ": ";
				} else {
					s << "You read: ";
				}
				s << item->getText();
			} else {
				s << "You are too far away to read it.";
			}
		} else {
			s << "Nothing is written on it.";
		}
	} else if (it.charges > 0) {
		uint32_t charges = (item == nullptr ? it.charges : item->getCharges());
		if (charges > 1) {
			s << " that has " << static_cast<int>(charges) << " charges left.";
		} else {
			s << " that has 1 charge left.";
		}
	} else if (it.showDuration) {
		if (item && item->hasAttribute(ITEM_ATTRIBUTE_DURATION)) {
			int32_t durationSeconds = item->getDuration() / 1000;
			int32_t hours = durationSeconds / 3600;
			int32_t minutes = (durationSeconds % 3600) / 60;

			if (hours > 0) {
				if (minutes > 0) {
					s << " that has energy for " << hours << " hour" << (hours != 1 ? "s" : "")
					  << " and " << minutes << " minute" << (minutes != 1 ? "s" : "") << " left.";
				} else {
					s << " that has energy for " << hours << " hour" << (hours != 1 ? "s" : "") << " left.";
				}
			} else {
				s << " that has energy for " << minutes << " minute" << (minutes != 1 ? "s" : "") << " left.";
			}
		} else {
			s << " that is brand-new.";
		}
	} else {
		s << ".";
	}

	if (it.wieldInfo != 0) {
		s << std::endl << "It can only be wielded properly by ";

		if (it.wieldInfo & WIELDINFO_PREMIUM) {
			s << "premium ";
		}

		if (it.wieldInfo & WIELDINFO_VOCREQ) {
			s << it.vocationString;
		} else {
			s << "players";
		}

		if (it.wieldInfo & WIELDINFO_LEVEL) {
			s << " of level " << static_cast<int>(it.minReqLevel) << " or higher";
		}

		if (it.wieldInfo & WIELDINFO_MAGLV) {
			if (it.wieldInfo & WIELDINFO_LEVEL) {
				s << " and";
			} else {
				s << " of";
			}

			s << " magic level " << static_cast<int>(it.minReqMagicLevel) << " or higher";
		}

		s << ".";
	}

	if (lookDistance <= 1 && it.pickupable) {
		double weight = (item == nullptr ? it.weight : item->getWeight());
		if (weight > 0) {
			s << std::endl << getWeightDescription(it, weight);
		}
	}

	if (item && item->getBed() && !item->getText().empty()) {
		s << ' ' << item->getText() << " is sleeping there.";
	} else if (item && item->getSpecialDescription() != "") {
		s << ' ' << item->getSpecialDescription().c_str();
	} else if (it.description.length() && lookDistance <= 1) {
		s << std::endl << it.description;
	}

	if (item) {
		const_cast<Item*>(item)->updateItemAttributes(nullptr);

		int64_t itemRarity = const_cast<Item*>(item)->getItemRarity();
		int64_t itemTier = const_cast<Item*>(item)->getItemTierRarity();

		int32_t requiredLevel = getRarityAttributeRequiredLevelFunction((ItemRarityLevel)itemRarity, (ItemTierLevel)itemTier);
		if (requiredLevel > 1) {
			s << "\nRequired player level " << requiredLevel << " to activate attributes from rarity. " << std::endl;
		}
	}

	return s.str();
}

std::string Item::getDescription(int32_t lookDistance) const
{
	const ItemType& it = items[id];
	return getDescription(it, lookDistance, this);
}

std::string Item::getNameDescription(const ItemType& it, const Item* item /*= nullptr*/, int32_t subType /*= -1*/, bool addArticle /*= true*/)
{
	if (item) {
		subType = item->getSubType();
	}

	std::ostringstream s;

	const std::string& name = (item ? item->getName() : it.name);
	if (!name.empty()) {
		if (it.stackable && subType > 1) {
			if (it.showCount) {
				s << subType << ' ';
			}

			s << (item ? item->getPluralName() : it.getPluralName());
		} else {
			if (addArticle) {
				const std::string& article = (item ? item->getArticle() : it.article);
				if (!article.empty()) {
					s << article << ' ';
				}
			}

			s << name;
		}
	} else {
		if (addArticle) {
			s << "an ";
		}
		s << "item of type " << it.id;
	}
	return s.str();
}

std::string Item::getNameDescription() const
{
	const ItemType& it = items[id];
	return getNameDescription(it, this);
}

std::string Item::getWeightDescription(const ItemType& it, uint32_t weight, uint32_t count /*= 1*/)
{
	std::ostringstream ss;
	if (it.stackable && count > 1 && it.showCount != 0) {
		ss << "They weigh ";
	} else {
		ss << "It weighs ";
	}

	if (weight < 100) {
		ss << "0." << weight;
	} else {
		std::string weightString = std::to_string(weight / 10);
		weightString.insert(weightString.end() - 1, '.');
		ss << weightString;
	}

	ss << " oz.";
	return ss.str();
}

bool Item::canPlayerUseRarityAttribute(const Player* player, int tier)
{
	return player->getLevel() >= (size_t)getRarityAttributeRequiredLevelFunction((ItemRarityLevel)getItemRarity(), (ItemTierLevel)tier);
}

int64_t Item::getIntCustomAttribute(const Player* player, std::string key) {
	auto it = getCustomAttribute(key);
	if (!it) {
		return 0;
	}
	int value = boost::get<int64_t>(it->value);
	
	if (player && value > 0 && !canPlayerUseRarityAttribute(player, value))
		return 0;

	return value;
}

ItemTierLevel Item::updateItemAttributes(const Player* player)
{

	//auto it = getCustomAttribute("tr");
	//if (it)
	//	return (ItemTierLevel)boost::get<int64_t>(it->value);

	ItemTierLevel tierLevel = ITEM_TIER_1;

	if (isStackable()) {
		return tierLevel;
	}


	const ItemType& itemType = Item::items[id];

	int level = player ? player->getLevel() : 0;

	for (int stat = ITEM_RND_FIRST; stat <= ITEM_RND_LAST; stat++) {
		auto it = getCustomAttribute(std::to_string(stat));

		if (!it)
			continue;

		int value = boost::get<int64_t>(it->value);

		if (value > 0)
		{
			tierLevel = (ItemTierLevel)value;

			if (player)
			{
				if (stat == ITEM_RND_ATTACK)
				{

					if (level >= getRarityAttributeRequiredLevel())
						setIntAttr(ITEM_ATTRIBUTE_ATTACK, itemType.attack + tierLevel);
					else
						setIntAttr(ITEM_ATTRIBUTE_ATTACK, itemType.attack);
				}
				else if (stat == ITEM_RND_ARMOR)
				{

					if (level >= getRarityAttributeRequiredLevel())
						setIntAttr(ITEM_ATTRIBUTE_ARMOR, itemType.armor + tierLevel);
					else
						setIntAttr(ITEM_ATTRIBUTE_ARMOR, itemType.armor);
				}
				else if (stat == ITEM_RND_DEF)
				{

					if (level >= getRarityAttributeRequiredLevel())
						setIntAttr(ITEM_ATTRIBUTE_DEFENSE, itemType.defense + tierLevel);
					else
						setIntAttr(ITEM_ATTRIBUTE_DEFENSE, itemType.defense);
				}
				else if (stat == ITEM_RND_ATTACK_SPEED)
				{

					if (level >= getRarityAttributeRequiredLevel())
					{
						if (itemType.attackSpeed == 0)
							setIntAttr(ITEM_ATTRIBUTE_ATTACK_SPEED, 2000 - (tierLevel * 50));
						else
							setIntAttr(ITEM_ATTRIBUTE_ATTACK_SPEED, itemType.attackSpeed - (tierLevel * 50));
					}
					else
					{
						if (itemType.attackSpeed == 0)
							setIntAttr(ITEM_ATTRIBUTE_ATTACK_SPEED, 2000);
						else
							setIntAttr(ITEM_ATTRIBUTE_ATTACK_SPEED, itemType.attackSpeed);
					}
				}
			}
		}
	}

	if (itemType.slotPosition & SLOTP_HEAD) {
		if (itemType.armor <= 6)
			tierLevel = ITEM_TIER_1;
		else if (itemType.armor <= 8)
			tierLevel = ITEM_TIER_2;
		else if (itemType.armor <= 9)
			tierLevel = ITEM_TIER_3;
		else
			tierLevel = ITEM_TIER_4;
	} else if (itemType.slotPosition & SLOTP_ARMOR) {
		if (itemType.armor <= 10)
			tierLevel = ITEM_TIER_1;
		else if (itemType.armor <= 13)
			tierLevel = ITEM_TIER_2;
		else if (itemType.armor <= 15)
			tierLevel = ITEM_TIER_3;
		else
			tierLevel = ITEM_TIER_4;
	} else if (itemType.slotPosition & SLOTP_LEGS) {
		if (itemType.armor <= 7)
			tierLevel = ITEM_TIER_1;
		else if (itemType.armor <= 8)
			tierLevel = ITEM_TIER_2;
		else if (itemType.armor <= 9)
			tierLevel = ITEM_TIER_3;
		else
			tierLevel = ITEM_TIER_4;
	} else if (itemType.slotPosition & SLOTP_FEET) {
		if (itemType.armor <= 1)
			tierLevel = ITEM_TIER_1;
		else if (itemType.armor <= 2)
			tierLevel = ITEM_TIER_2;
		else if (itemType.armor <= 3)
			tierLevel = ITEM_TIER_3;
		else
			tierLevel = ITEM_TIER_4;
	} else if (itemType.weaponType == WEAPON_SHIELD) {
		if (itemType.defense <= 28)
			tierLevel = ITEM_TIER_1;
		else if (itemType.defense <= 34)
			tierLevel = ITEM_TIER_2;
		else if (itemType.defense <= 36)
			tierLevel = ITEM_TIER_3;
		else
			tierLevel = ITEM_TIER_4;
	} else if (itemType.attack > 0 && (itemType.weaponType == WEAPON_CLUB || itemType.weaponType == WEAPON_AXE || itemType.weaponType == WEAPON_SWORD || itemType.weaponType == WEAPON_DISTANCE)) {
		if (itemType.slotPosition & SLOTP_TWO_HAND) {
			// two handed weapons
			if (itemType.attack <= 35)
				tierLevel = ITEM_TIER_1;
			else if (itemType.attack <= 39)
				tierLevel = ITEM_TIER_2;
			else if (itemType.attack <= 47)
				tierLevel = ITEM_TIER_3;
			else
				tierLevel = ITEM_TIER_4; // a partir de 48
		} else {
			// one handed weapons
			if (itemType.attack <= 31)
				tierLevel = ITEM_TIER_1;
			else if (itemType.attack <= 35)
				tierLevel = ITEM_TIER_2;
			else if (itemType.attack <= 39)
				tierLevel = ITEM_TIER_3;
			else
				tierLevel = ITEM_TIER_4; // a partir de 40
		}
	}

	if (itemType.rarityTier == 1)
		tierLevel = ITEM_TIER_1;
	else if (itemType.rarityTier == 2)
		tierLevel = ITEM_TIER_2;
	else if (itemType.rarityTier == 3)
		tierLevel = ITEM_TIER_3;
	else if (itemType.rarityTier == 4)
		tierLevel = ITEM_TIER_4;

	ItemAttributes::CustomAttribute val;
	val.set<int64_t>(tierLevel);
	std::string key = "tr";
	setCustomAttribute(key, val);
	return tierLevel;
}

void Item::applyRarity(ItemTierLevel tierLevel, ItemRarityLevel rarityLevel, std::vector<ItemRandomAttributeType> attrList) 
{
    if (isStackable()) {
        return;
    }

    static const std::unordered_map<ItemRarityLevel, std::pair<uint32_t, std::string>> rarityData = {
        {ITEM_COMMON,    {0, "common"}},
        {ITEM_UNCOMMON,  {UNCOMMON_ITEM_SLOTS, "uncommon"}},
        {ITEM_RARE,      {RARE_ITEM_SLOTS, "rare"}},
        {ITEM_EPIC,      {EPIC_ITEM_SLOTS, "epic"}},
        {ITEM_LEGENDARY, {LEGENDARY_ITEM_SLOTS, "legendary"}}
    };

    auto it = rarityData.find(rarityLevel);
    if (it == rarityData.end()) {
        return; 
    }

    uint32_t totalAttributeSlots = it->second.first;
    const std::string& rarityStr = it->second.second;

    ItemAttributes::CustomAttribute rarityLevelValue;
    rarityLevelValue.set<int64_t>(static_cast<int64_t>(rarityLevel));

    std::string rarityLevelKey = ItemAttributes::customAttributeMapKeyToString(ITEM_RARITY_ATTRIBUTE);
    setStrAttr(ITEM_ATTRIBUTE_NAME, fmt::format("{} {}", rarityStr, getName()));
    setCustomAttribute(rarityLevelKey, rarityLevelValue);
 
    attrList.reserve(totalAttributeSlots);
    rollAttributes(tierLevel, attrList.size(), attrList);
}

void Item::rollRarityLevel(bool forceRarity, uint8_t rarity)
{
	if (isStackable()) {
		return;
	}

	const ItemType& itemType = Item::items[id];
	ItemTierLevel tierLevel = ITEM_TIER_1;
	bool freeze_chance = false;
	if (itemType.slotPosition & SLOTP_HEAD) {
		if (itemType.armor <= 6)
			tierLevel = ITEM_TIER_1;
		else if (itemType.armor <= 8)
			tierLevel = ITEM_TIER_2;
		else if (itemType.armor <= 9)
			tierLevel = ITEM_TIER_3;
		else
			tierLevel = ITEM_TIER_4;
	}
	else if (itemType.slotPosition & SLOTP_ARMOR) {
		if (itemType.armor <= 10)
			tierLevel = ITEM_TIER_1;
		else if (itemType.armor <= 13)
			tierLevel = ITEM_TIER_2;
		else if (itemType.armor <= 15)
			tierLevel = ITEM_TIER_3;
		else
			tierLevel = ITEM_TIER_4;

	}
	else if (itemType.slotPosition & SLOTP_LEGS) {
		if (itemType.armor <= 7)
			tierLevel = ITEM_TIER_1;
		else if (itemType.armor <= 8)
			tierLevel = ITEM_TIER_2;
		else if (itemType.armor <= 9)
			tierLevel = ITEM_TIER_3;
		else
			tierLevel = ITEM_TIER_4;
	}
	else if (itemType.slotPosition & SLOTP_FEET) {
		if (itemType.armor <= 1)
			tierLevel = ITEM_TIER_1;
		else if (itemType.armor <= 2)
			tierLevel = ITEM_TIER_2;
		else if (itemType.armor <= 3)
			tierLevel = ITEM_TIER_3;
		else
			tierLevel = ITEM_TIER_4;
	}
	else if (itemType.weaponType == WEAPON_SHIELD) {
		if (itemType.defense <= 28)
			tierLevel = ITEM_TIER_1;
		else if (itemType.defense <= 34)
			tierLevel = ITEM_TIER_2;
		else if (itemType.defense <= 36)
			tierLevel = ITEM_TIER_3;
		else
			tierLevel = ITEM_TIER_4;
	}
	else if (itemType.weaponType == WEAPON_QUIVER && itemType.ammoType == AMMO_ARROW)
	{			
		freeze_chance = true;
	}
	else if (itemType.weaponType == WEAPON_QUIVER && itemType.ammoType == AMMO_BOLT)
	{				
		freeze_chance = true;
	}
	else if (itemType.weaponType == WEAPON_DISTANCE && itemType.slotPosition & SLOTP_TWO_HAND)
	{		
		freeze_chance = true;
	}
	else if (itemType.weaponType == WEAPON_WAND)
	{
			freeze_chance = true;
	}	
	else if (itemType.attack > 0 && (itemType.weaponType == WEAPON_CLUB || itemType.weaponType == WEAPON_AXE || itemType.weaponType == WEAPON_SWORD || itemType.weaponType == WEAPON_DISTANCE))
	{
		if (itemType.slotPosition & SLOTP_TWO_HAND)
		{
			// two handed weapons
			if (itemType.attack <= 35)
				tierLevel = ITEM_TIER_1;
			else if (itemType.attack <= 39)
				tierLevel = ITEM_TIER_2;
			else if (itemType.attack <= 47)
				tierLevel = ITEM_TIER_3;
			else
				tierLevel = ITEM_TIER_4; // a partir de 48
		}
		else {
			// one handed weapons
			if (itemType.attack <= 31)
				tierLevel = ITEM_TIER_1;
			else if (itemType.attack <= 35)
				tierLevel = ITEM_TIER_2;
			else if (itemType.attack <= 39)
				tierLevel = ITEM_TIER_3;
			else
				tierLevel = ITEM_TIER_4; // a partir de 40
		}
	}
	else
	{
		// Não é item com raridade!
		return;
	}

	if (itemType.rarityTier == 1)
		tierLevel = ITEM_TIER_1;
	else if (itemType.rarityTier == 2)
		tierLevel = ITEM_TIER_2;
	else if (itemType.rarityTier == 3)
		tierLevel = ITEM_TIER_3;
	else if (itemType.rarityTier == 4)
		tierLevel = ITEM_TIER_4;

	/* else if (itemType.attack > 0 && (itemType.weaponType == WEAPON_CLUB || itemType.weaponType == WEAPON_AXE || itemType.weaponType == WEAPON_SWORD) || itemType.weaponType == WEAPON_DISTANCE) {
		uint32_t tierChance = uniform_random(0, 100000);
		if (tierChance > 60000 && tierChance <= 100000) //40% de chance de vir tier 1
		{
			tierLevel = ITEM_TIER_1;
		}
		else if (tierChance > 30000 && tierChance <= 60000) { // 30% de chance de vir tier 2
			tierLevel = ITEM_TIER_2;
		}
		else if (tierChance > 10000 && tierChance <= 30000) { // 20% de chance de vir tier 3
			tierLevel = ITEM_TIER_3;
		}
		else if (tierChance >= 0 && tierChance <= 10000) { // 10% de chance de vir tier 4
			tierLevel = ITEM_TIER_4;
		}
		freeze_chance = true;
	} */
	/*else if (itemType.slotPosition & SLOTP_NECKLACE) {
		rollAttributes(static_cast<ItemTierLevel>(uniform_random(ITEM_TIER_FIRST, ITEM_TIER_LAST)), totalAttributeSlots);

		if (!rarityStr.empty()) {
			setStrAttr(ITEM_ATTRIBUTE_NAME, fmt::format("{:s} {:s}", rarityStr, getName()));
			setCustomAttribute(rarityLevelKey, rarityLevelValue);
		}
	}
	else if (itemType.slotPosition & SLOTP_RING) {
		rollAttributes(static_cast<ItemTierLevel>(uniform_random(ITEM_TIER_FIRST, ITEM_TIER_LAST)), totalAttributeSlots);

		if (!rarityStr.empty()) {
			setStrAttr(ITEM_ATTRIBUTE_NAME, fmt::format("{:s} {:s}", rarityStr, getName()));
			setCustomAttribute(rarityLevelKey, rarityLevelValue);
		}
	}*/

	uint32_t rarityLevel = ITEM_COMMON;
	uint32_t commonChance;

	if (tierLevel == ITEM_TIER_1 || freeze_chance)
		commonChance = 400;
	else if (tierLevel == ITEM_TIER_2)
		commonChance = 200;
	else if (tierLevel == ITEM_TIER_3)
		commonChance = 150;
	else if (tierLevel == ITEM_TIER_4)
		commonChance = 80;
	
	// No momento, é a chance de vir comum

	uint32_t uncommonChance = commonChance + g_config.getNumber(ConfigManager::ITEM_UNCOMMON_CHANCE);
	uint32_t rareChance = uncommonChance + g_config.getNumber(ConfigManager::ITEM_RARE_CHANCE);
	uint32_t epicChance = rareChance + g_config.getNumber(ConfigManager::ITEM_EPIC_CHANCE);
	uint32_t legendaryChance = epicChance + g_config.getNumber(ConfigManager::ITEM_LEGENDARY_CHANCE); // total chance

	uint32_t chance = uniform_random(1, legendaryChance);

	if (!forceRarity) {
		if (chance <= commonChance) // de 1 a 100 (para tier 4)
		{
			rarityLevel = ITEM_COMMON;
		}
		else if (chance <= uncommonChance) // de 101 a 104 (para tier 4)
		{
			rarityLevel = ITEM_UNCOMMON;
		}
		else if (chance <= rareChance) // de 105 a 107 (para tier 4)
		{
			rarityLevel = ITEM_RARE;
		}
		else if (chance <= epicChance) // de 108 a 109
		{
			rarityLevel = ITEM_EPIC;
		}
		else if (chance <= legendaryChance) // se igual a 110
		{
			rarityLevel = ITEM_LEGENDARY;
		}
	} else {
		rarityLevel = rarity;
	}

	if (rarityLevel == ITEM_COMMON) {
		return;
	}

	uint32_t totalAttributeSlots = 0;
	std::string rarityStr = "";

	std::string rarityLevelKey = ItemAttributes::customAttributeMapKeyToString(ITEM_RARITY_ATTRIBUTE);
	ItemAttributes::CustomAttribute rarityLevelValue;
	rarityLevelValue.set<int64_t>(ITEM_COMMON);

	switch (rarityLevel) {
	case ITEM_UNCOMMON:
		totalAttributeSlots = UNCOMMON_ITEM_SLOTS;
		rarityStr = "uncommon";
		rarityLevelValue.set<int64_t>(ITEM_UNCOMMON);
		break;
	case ITEM_RARE:
		totalAttributeSlots = RARE_ITEM_SLOTS;
		rarityStr = "rare";
		rarityLevelValue.set<int64_t>(ITEM_RARE);
		break;
	case ITEM_EPIC:
		totalAttributeSlots = EPIC_ITEM_SLOTS;
		rarityStr = "epic";
		rarityLevelValue.set<int64_t>(ITEM_EPIC);
		break;
	case ITEM_LEGENDARY:
		totalAttributeSlots = LEGENDARY_ITEM_SLOTS;
		rarityStr = "legendary";
		rarityLevelValue.set<int64_t>(ITEM_LEGENDARY);
		break;
	default: break;
	}

	rollAttributes(tierLevel, (!forceRarity ? totalAttributeSlots : 1));

	if (!rarityStr.empty()) {
		setStrAttr(ITEM_ATTRIBUTE_NAME, fmt::format("{:s} {:s}", rarityStr, itemType.name));
		setCustomAttribute(rarityLevelKey, rarityLevelValue);
	}

	Cylinder* cylinder = this->getParent();
	if (cylinder) {
		cylinder->updateThing(this, this->getID(), this->getItemCount());
	}
}

void Item::rollAttributes(ItemTierLevel tierLevel, uint8_t totalAttributes)
{
	const ItemType& itemType = Item::items[id];

	std::vector<ItemRandomAttributeType> attrList{};

	for (int i = ITEM_RND_FIRST; i <= ITEM_RND_LAST; i++) {
		attrList.push_back(static_cast<ItemRandomAttributeType>(i));
	}

	//attrList.push_back(ITEM_RND_RESISTANCE);
	//attrList.push_back(ITEM_RND_RESISTANCE);
	//attrList.push_back(ITEM_RND_RESISTANCE);
	attrList.push_back(ITEM_RND_RESISTANCE);


	if (itemType.weaponType == WEAPON_WAND) {
		attrList.push_back(ITEM_RND_RESISTANCE);
	}

	rollAttributes(tierLevel, totalAttributes, attrList);
}

void Item::applyFixedBonuses() {
	uint16_t itemId = this->getID();
	std::string newDesc;

	if (itemId == 5549) {

		std::string manaKey = std::to_string(ITEM_RND_MANA_REGEN);
		if (!this->getCustomAttribute(manaKey)) {
			ItemAttributes::CustomAttribute val;
			val.set<int64_t>(10); // 10 * 0.2 = 2.0
			this->setCustomAttribute(manaKey, val);
			newDesc += fmt::format("\nIncreased Mana Regeneration +{:.1f}", 2.0);
		}

		std::string healthKey = std::to_string(ITEM_RND_HEALTH_REGEN);
		if (!this->getCustomAttribute(healthKey)) {
			ItemAttributes::CustomAttribute val;
			val.set<int64_t>(5); // 5 * 0.2 = 1.0
			this->setCustomAttribute(healthKey, val);
			newDesc += fmt::format("\nIncreased Health Regeneration +{:.1f}", 1.0);
		}
	}

	else if (itemId == 5548) {

		std::string manaKey = std::to_string(ITEM_RND_MANA_REGEN);
		if (!this->getCustomAttribute(manaKey)) {
			ItemAttributes::CustomAttribute val;
			val.set<int64_t>(5);
			this->setCustomAttribute(manaKey, val);
			newDesc += fmt::format("\nIncreased Mana Regeneration +{:.1f}", 1.0);
		}

		std::string healthKey = std::to_string(ITEM_RND_HEALTH_REGEN);
		if (!this->getCustomAttribute(healthKey)) {
			ItemAttributes::CustomAttribute val;
			val.set<int64_t>(10);
			this->setCustomAttribute(healthKey, val);
			newDesc += fmt::format("\nIncreased Health Regeneration +{:.1f}", 2.0);
		}
	}
	if (!newDesc.empty()) {
		std::string currentDesc = getStrAttr(ITEM_ATTRIBUTE_DESCRIPTION);
		if (!currentDesc.empty()) {
			newDesc = currentDesc + "\n" + newDesc;
		}
		setStrAttr(ITEM_ATTRIBUTE_DESCRIPTION, newDesc);
	}
}

void Item::rollAttributes(ItemTierLevel tierLevel, uint8_t totalAttributes, std::vector<ItemRandomAttributeType> attrList)
{
	std::string dataDescription = getSpecialDescription();

	const ItemType& itemType = Item::items[id];

	std::ranges::shuffle(attrList, getRandomGenerator());

	for (uint8_t i = 0; i < totalAttributes;) {
		if (attrList.empty()) {
			break;
		}

		bool inc = true;

		ItemRandomAttributeType attr = attrList.back();
		attrList.pop_back();

		switch (attr) {
			case ITEM_RND_ATTACK: {
				if (itemType.weaponType == WEAPON_CLUB || itemType.weaponType == WEAPON_AXE || itemType.weaponType == WEAPON_SWORD ||
				itemType.weaponType == WEAPON_QUIVER) {
					std::string str = std::to_string(attr);
					if (totalAttributes == 1) {
						if (getCustomAttribute(str)) {
							rollAttributes(tierLevel, 1);
							return;
						}
					}
					setIntAttr(ITEM_ATTRIBUTE_ATTACK, itemType.attack + tierLevel);
					ItemAttributes::CustomAttribute val;
					val.set<int64_t>(tierLevel);
					setCustomAttribute(str, val);
					dataDescription = fmt::format("{:s}\nIncreased Attack +{:d}", dataDescription, static_cast<uint16_t>(tierLevel));
				}
				else {
					inc = false;
				}
				break;
			}
			case ITEM_RND_SPEED: {
				if (itemType.slotPosition & SLOTP_FEET) {
					std::string str = std::to_string(attr);
					if (totalAttributes == 1) {
						if (getCustomAttribute(str)) {
							rollAttributes(tierLevel, 1);
							return;
						}
					}
					ItemAttributes::CustomAttribute val;
					val.set<int64_t>(tierLevel);
					setCustomAttribute(str, val);
					dataDescription = fmt::format("{:s}\nIncreased Movement Speed +{:d}", dataDescription, static_cast<uint16_t>(tierLevel * 5));
				}
				else {
					inc = false;
				}
				break;
			}
			case ITEM_RND_ARMOR: {
				bool hasArmor = itemType.armor != 0 || itemType.slotPosition & SLOTP_FEET;
				if (hasArmor) {
					std::string str = std::to_string(attr);
					if (totalAttributes == 1) {
						if (getCustomAttribute(str)) {
							rollAttributes(tierLevel, 1);
							return;
						}
					}
					setIntAttr(ITEM_ATTRIBUTE_ARMOR, itemType.armor + tierLevel);
					ItemAttributes::CustomAttribute val;
					val.set<int64_t>(tierLevel);
					setCustomAttribute(str, val);
					dataDescription = fmt::format("{:s}\nIncreased Armor +{:d}", dataDescription, static_cast<uint16_t>(tierLevel));
				}
				else {
					inc = false;
				}
				break;
			}
			case ITEM_RND_DEF: {
				if (itemType.defense != 0 && itemType.weaponType == WEAPON_SHIELD) {
					std::string str = std::to_string(attr);
					if (totalAttributes == 1) {
						if (getCustomAttribute(str)) {
							rollAttributes(tierLevel, 1);
							return;
						}
					}
					setIntAttr(ITEM_ATTRIBUTE_DEFENSE, itemType.defense + tierLevel);
					ItemAttributes::CustomAttribute val;
					val.set<int64_t>(tierLevel);
					setCustomAttribute(str, val);
					dataDescription = fmt::format("{:s}\nIncreased Defense +{:d}", dataDescription, static_cast<uint16_t>(tierLevel));
				}
				else {
					inc = false;
				}
				break;
			}
			case ITEM_RND_SKILL: {
				if (itemType.weaponType == WEAPON_CLUB || itemType.weaponType == WEAPON_AXE || itemType.weaponType == WEAPON_SWORD || itemType.weaponType == WEAPON_DISTANCE || itemType.weaponType == WEAPON_QUIVER || itemType.weaponType == WEAPON_SHIELD) {
					std::string str = std::to_string(attr);
					if (totalAttributes == 1) {
						if (getCustomAttribute(str)) {
							rollAttributes(tierLevel, 1);
							return;
						}
					}
					ItemAttributes::CustomAttribute val;
					val.set<int64_t>(tierLevel);
					setCustomAttribute(str, val);
					dataDescription = fmt::format("{:s}\nIncreased Skills +{:d}", dataDescription, static_cast<uint16_t>(tierLevel));
				}
				else {
					inc = false;
				}
				break;
			}
			case ITEM_RND_MAGIC: {
				if (itemType.weaponType == WEAPON_NONE || itemType.weaponType == WEAPON_WAND) {
					std::string str = std::to_string(attr);
					ItemAttributes::CustomAttribute val;
					val.set<int64_t>(tierLevel);
					setCustomAttribute(str, val);
					dataDescription = fmt::format("{:s}\nIncreased Magic +{:d}", dataDescription, static_cast<uint16_t>(tierLevel));
				}
				else {
					inc = false;
				}
				break;
			}
			case ITEM_RND_WEIGHT_REDUCTION: {
				if (itemType.weaponType == WEAPON_NONE || itemType.weaponType == WEAPON_SHIELD) {
					std::string str = std::to_string(attr);
					if (totalAttributes == 1) {
						if (getCustomAttribute(str)) {
							rollAttributes(tierLevel, 1);
							return;
						}
					}
					uint32_t baseWeight = getWeight();
					double weightReduction = 1.0 - ((size_t)tierLevel * 0.2); // -20% por Tier
					uint32_t newWeight = std::max(static_cast<uint32_t>(baseWeight * weightReduction), 1U);

					setIntAttr(ITEM_ATTRIBUTE_WEIGHT, newWeight);
					ItemAttributes::CustomAttribute val;
					val.set<int64_t>(tierLevel);
					setCustomAttribute(str, val);

					dataDescription = fmt::format("{:s}\nWeight Reduction -{:d}%", dataDescription, tierLevel * 20);
				} else {
					inc = false;
				}
				break;
			}			
			case ITEM_RND_MAX_HEALTH:
			case ITEM_RND_MAX_MANA: {
				if (itemType.weaponType == WEAPON_NONE || itemType.weaponType == WEAPON_SHIELD) {
					std::string str = std::to_string(attr);

					// Determinar o range conforme tierLevel
					int minValue = 0;
					int maxValue = 0;

					switch (tierLevel) {
						case 1:
							minValue = 10;
							maxValue = 30;
							break;
						case 2:
							minValue = 35;
							maxValue = 60;
							break;
						case 3:
							minValue = 65;
							maxValue = 100;
							break;
						case 4:
						default:
							minValue = 100;
							maxValue = 200;
							break;
					}

					int newTierLevel = uniform_random(minValue, maxValue);

					if (totalAttributes == 1) {
						if (getCustomAttribute(str)) {
							rollAttributes(static_cast<ItemTierLevel>(newTierLevel), 1);
							return;
						}
					}

					ItemAttributes::CustomAttribute val;
					val.set<int64_t>(newTierLevel);
					setCustomAttribute(str, val);

					if (attr == ITEM_RND_MAX_HEALTH) {
						dataDescription = fmt::format(
							"{:s}\nIncreased Max Health +{:d}",
							dataDescription,
							static_cast<uint16_t>(newTierLevel)
						);
					}
					else if (attr == ITEM_RND_MAX_MANA) {
						dataDescription = fmt::format(
							"{:s}\nIncreased Max Mana +{:d}",
							dataDescription,
							static_cast<uint16_t>(newTierLevel)
						);
					}
				}
				else {
					inc = false;
				}
				break;
			}


			case ITEM_RND_HEALTH_REGEN: 
			case ITEM_RND_MANA_REGEN: {
				if (itemType.weaponType == WEAPON_NONE || itemType.weaponType == WEAPON_SHIELD) {
					std::string str = std::to_string(attr);
					if (totalAttributes == 1) {
						if (getCustomAttribute(str)) {
							// Cria multiplicador aleatório x1~x2
							int multiplier = uniform_random(1, 1.5);
							int newTierLevel = tierLevel * multiplier;
							rollAttributes(static_cast<ItemTierLevel>(newTierLevel), 1);
							return;
						}
					}

					// Cria multiplicador aleatório também aqui
					int multiplier = uniform_random(1, 1.5);
					int newTierLevel = tierLevel * multiplier;

					ItemAttributes::CustomAttribute val;
					val.set<int64_t>(newTierLevel);
					setCustomAttribute(str, val);

					if (attr == ITEM_RND_HEALTH_REGEN) {
						dataDescription = fmt::format(
							"{:s}\nIncreased Health Regeneration +{:.1f}",
							dataDescription,
							static_cast<double>(newTierLevel) * 0.20
						);
					}
					else if (attr == ITEM_RND_MANA_REGEN) {
						dataDescription = fmt::format(
							"{:s}\nIncreased Mana Regeneration +{:.1f}",
							dataDescription,
							static_cast<double>(newTierLevel) * 0.20
						);
					}
				}
				else {
					inc = false;
				}
				break;
			}

			case ITEM_RND_RESISTANCE: {
				if (itemType.weaponType == WEAPON_NONE || itemType.weaponType == WEAPON_SHIELD || itemType.weaponType == WEAPON_WAND) {
					
					std::vector<ItemAttributeDefenseType> resistances;					
					if (itemType.weaponType == WEAPON_WAND)
						resistances = { ITEM_RND_RESIST_FIRE, ITEM_RND_RESIST_POISON, ITEM_RND_RESIST_ENERGY, ITEM_RND_RESIST_ICE };
					else
						resistances = { ITEM_RND_RESIST_PHYSICAL, ITEM_RND_RESIST_FIRE, ITEM_RND_RESIST_POISON, ITEM_RND_RESIST_ENERGY, ITEM_RND_RESIST_ICE };					
					
					std::ranges::shuffle(resistances, getRandomGenerator());

					bool added = false;
					for (auto resistance : resistances) {
						if (getCustomAttribute(std::to_string(resistance))) {
							continue;
						}

						std::string str = std::to_string(resistance);
						if (totalAttributes == 1) {
							if (getCustomAttribute(str)) {
								// Cria multiplicador aleatório x1 ~ x5
								int multiplier = uniform_random(1, 5);
								int newTierLevel = tierLevel * multiplier;
								rollAttributes(static_cast<ItemTierLevel>(newTierLevel), 1);
								return;
							}
						}

						// Também aplica multiplicador aqui na criação do atributo
						int multiplier = uniform_random(1, 5);
						int newTierLevel = tierLevel * multiplier;

						ItemAttributes::CustomAttribute val;
						val.set<int64_t>(newTierLevel);
						setCustomAttribute(str, val);

						std::string str2 = std::to_string(attr);
						ItemAttributes::CustomAttribute val2;
						val2.set<int64_t>(newTierLevel);
						setCustomAttribute(str2, val2);

						switch (resistance) {
							case ITEM_RND_RESIST_ENERGY:
								dataDescription = fmt::format("{:s}\nIncreased Energy Resistance {:d}%", dataDescription, static_cast<uint16_t>(newTierLevel));
								break;
							case ITEM_RND_RESIST_FIRE:
								dataDescription = fmt::format("{:s}\nIncreased Fire Resistance {:d}%", dataDescription, static_cast<uint16_t>(newTierLevel));
								break;
							case ITEM_RND_RESIST_POISON:
								dataDescription = fmt::format("{:s}\nIncreased Poison Resistance {:d}%", dataDescription, static_cast<uint16_t>(newTierLevel));
								break;
							case ITEM_RND_RESIST_PHYSICAL:
								dataDescription = fmt::format("{:s}\nIncreased Physical Resistance {:d}%", dataDescription, static_cast<uint16_t>(newTierLevel));
								break;
							case ITEM_RND_RESIST_ICE:
								dataDescription = fmt::format("{:s}\nIncreased Ice Resistance {:d}%", dataDescription, static_cast<uint16_t>(newTierLevel));
								break;
							default:
								break;
						}

						added = true;
						break;
					}

					if (!added)
						inc = false;
				}
				else {
					inc = false;
				}
				break;
			}

			case ITEM_RND_CRITICAL: {
				if (itemType.weaponType != WEAPON_NONE && itemType.weaponType != WEAPON_SHIELD) {
					std::string str = std::to_string(attr);
					if (totalAttributes == 1) {
						if (getCustomAttribute(str)) {
							// Cria multiplicador aleatório x1~x5
							int multiplier = uniform_random(1, 3);
							int newTierLevel = tierLevel * multiplier;
							rollAttributes(static_cast<ItemTierLevel>(newTierLevel), 1);
							return;
						}
					}

					// Cria multiplicador aleatório também aqui
					int multiplier = uniform_random(1, 3);
					int newTierLevel = tierLevel * multiplier;

					ItemAttributes::CustomAttribute val;
					val.set<int64_t>(newTierLevel);
					setCustomAttribute(str, val);

					dataDescription = fmt::format(
						"{:s}\nCritical Hit +{:d}%",
						dataDescription,
						static_cast<uint16_t>(newTierLevel)
					);
				}
				else {
					inc = false;
				}
				break;
			}

			
			case ITEM_RND_ATTACK_SPEED: {
				if (itemType.weaponType != WEAPON_NONE && itemType.weaponType != WEAPON_SHIELD && itemType.weaponType != WEAPON_WAND) {
					std::string str = std::to_string(attr);
					if (totalAttributes == 1) {
						if (getCustomAttribute(str)) {
							rollAttributes(tierLevel, 1);
							return;
						}
					}
					ItemAttributes::CustomAttribute val;
					val.set<int64_t>(tierLevel);
					setCustomAttribute(str, val);

					if (itemType.attackSpeed == 0)
						setIntAttr(ITEM_ATTRIBUTE_ATTACK_SPEED, 2000 - (tierLevel * 80));
					else
						setIntAttr(ITEM_ATTRIBUTE_ATTACK_SPEED, itemType.attackSpeed - (tierLevel * 80));
						float decreaseValue = static_cast<float>(tierLevel) * 0.05f;
						dataDescription = fmt::format("{:s}\nDecrease Attack Interval -{:.2f}", dataDescription, decreaseValue);				
				}
				else {
					inc = false;
				}
				break;
			}

			case ITEM_RND_PARRY: {
				if (itemType.weaponType == WEAPON_AXE || itemType.weaponType == WEAPON_CLUB || itemType.weaponType == WEAPON_SWORD || itemType.weaponType == WEAPON_WAND) {
					std::string str = std::to_string(attr);
					if (totalAttributes == 1) {
						if (getCustomAttribute(str)) {
							int multiplier = uniform_random(1, 2);
							int newTierLevel = tierLevel * multiplier;
							rollAttributes(static_cast<ItemTierLevel>(newTierLevel), 1);
							return;
						}
					}
					int multiplier = uniform_random(1, 2);
					int newTierLevel = tierLevel * multiplier;

					ItemAttributes::CustomAttribute val;
					val.set<int64_t>(newTierLevel);
					setCustomAttribute(str, val);

					dataDescription = fmt::format("{:s}\nParry {:d}%", dataDescription, static_cast<uint16_t>(newTierLevel));
				} else {
					inc = false;
				}
				break;
			}

			case ITEM_RND_PERSEVERANCE: {
				if ((itemType.weaponType == WEAPON_AXE || itemType.weaponType == WEAPON_CLUB || itemType.weaponType == WEAPON_SWORD) &&
					!(itemType.slotPosition & SLOTP_TWO_HAND)) {
					std::string str = std::to_string(attr);
					if (totalAttributes == 1) {
						if (getCustomAttribute(str)) {
							int multiplier = uniform_random(1, 2);
							int newTierLevel = tierLevel * multiplier;
							rollAttributes(static_cast<ItemTierLevel>(newTierLevel), 1);
							return;
						}
					}
					int multiplier = uniform_random(1, 2);
					int newTierLevel = tierLevel * multiplier;

					ItemAttributes::CustomAttribute val;
					val.set<int64_t>(newTierLevel);
					setCustomAttribute(str, val);

					dataDescription = fmt::format("{:s}\nPerseverance {:d}%", dataDescription, static_cast<uint16_t>(newTierLevel));
				} else {
					inc = false;
				}
				break;
			}

			case ITEM_RND_BERSERK: {
				if ((itemType.weaponType == WEAPON_AXE || itemType.weaponType == WEAPON_CLUB || itemType.weaponType == WEAPON_SWORD) &&
					(itemType.slotPosition & SLOTP_TWO_HAND)) {
					std::string str = std::to_string(attr);
					if (totalAttributes == 1) {
						if (getCustomAttribute(str)) {
							int multiplier = uniform_random(1, 2);
							int newTierLevel = tierLevel * multiplier;
							rollAttributes(static_cast<ItemTierLevel>(newTierLevel), 1);
							return;
						}
					}
					int multiplier = uniform_random(1, 2);
					int newTierLevel = tierLevel * multiplier;

					ItemAttributes::CustomAttribute val;
					val.set<int64_t>(newTierLevel);
					setCustomAttribute(str, val);

					dataDescription = fmt::format("{:s}\nBerserk {:d}%", dataDescription, static_cast<uint16_t>(newTierLevel));
				} else {
					inc = false;
				}
				break;
			}

			case ITEM_RND_CRUSHING_BLOW: {
				if (itemType.isCrossbow || (itemType.weaponType == WEAPON_QUIVER && itemType.ammoType == AMMO_BOLT)) {
					std::string str = std::to_string(attr);
					if (totalAttributes == 1) {
						if (getCustomAttribute(str)) {
							int multiplier = uniform_random(1, 5);
							int newTierLevel = tierLevel * multiplier;
							rollAttributes(static_cast<ItemTierLevel>(newTierLevel), 1);
							return;
						}
					}
					int multiplier = uniform_random(1, 2);
					int newTierLevel = tierLevel * multiplier;

					ItemAttributes::CustomAttribute val;
					val.set<int64_t>(newTierLevel);
					setCustomAttribute(str, val);

					dataDescription = fmt::format("{:s}\nCrushing Blow {:d}%", dataDescription, static_cast<uint16_t>(newTierLevel));
				} else {
					inc = false;
				}
				break;
			}

			case ITEM_RND_FAST_HAND: {
				if (itemType.isBow || (itemType.weaponType == WEAPON_QUIVER && itemType.ammoType == AMMO_ARROW)) {
					std::string str = std::to_string(attr);
					if (totalAttributes == 1) {
						if (getCustomAttribute(str)) {
							int multiplier = uniform_random(1, 5);
							int newTierLevel = tierLevel * multiplier;
							rollAttributes(static_cast<ItemTierLevel>(newTierLevel), 1);
							return;
						}
					}
					int multiplier = uniform_random(1, 2);
					int newTierLevel = tierLevel * multiplier;

					ItemAttributes::CustomAttribute val;
					val.set<int64_t>(newTierLevel);
					setCustomAttribute(str, val);

					dataDescription = fmt::format("{:s}\nFast Hand {:d}%", dataDescription, static_cast<uint16_t>(newTierLevel));
				} else {
					inc = false;
				}
				break;
			}

			case ITEM_RND_SHARPSHOOTER: {
				if ((itemType.weaponType == WEAPON_DISTANCE && itemType.slotPosition & SLOTP_TWO_HAND) || itemType.weaponType == WEAPON_QUIVER) {
					std::string str = std::to_string(attr);
					if (totalAttributes == 1) {
						if (getCustomAttribute(str)) {
							int multiplier = uniform_random(1, 2);
							int newTierLevel = tierLevel * multiplier;
							rollAttributes(static_cast<ItemTierLevel>(newTierLevel), 1);
							return;
						}
					}
					int multiplier = uniform_random(1, 2);
					int newTierLevel = tierLevel * multiplier;

					ItemAttributes::CustomAttribute val;
					val.set<int64_t>(newTierLevel);
					setCustomAttribute(str, val);

					dataDescription = fmt::format("{:s}\nSharpshooter {:d}%", dataDescription, static_cast<uint16_t>(newTierLevel));
				} else {
					inc = false;
				}
				break;
			}


			case ITEM_RND_BLEEDING:
			case ITEM_RND_ELETRICFYING:
			case ITEM_RND_BURNING:
			case ITEM_RND_POISONING: {
				// Verifica se o item já possui um dos atributos de "bleeding"
				if (getCustomAttribute(std::to_string(ITEM_RND_BLEEDING)) ||
					getCustomAttribute(std::to_string(ITEM_RND_ELETRICFYING)) ||
					getCustomAttribute(std::to_string(ITEM_RND_BURNING)) ||
					getCustomAttribute(std::to_string(ITEM_RND_POISONING))) {
					inc = false; // Bloqueia a adição de outro atributo de "bleeding"
					break;
				}
				if (itemType.weaponType != WEAPON_NONE && itemType.weaponType != WEAPON_SHIELD) {
					std::string str = std::to_string(attr);
					if (totalAttributes == 1) {
						if (getCustomAttribute(str)) {
							// Cria multiplicador aleatório x1 ~ x5
							int multiplier = uniform_random(1, 5);
							int newTierLevel = tierLevel * multiplier;
							rollAttributes(static_cast<ItemTierLevel>(newTierLevel), 1);
							return;
						}
					}

					// Também aplica multiplicador aqui na criação do atributo
					int multiplier = uniform_random(1, 5);
					int newTierLevel = tierLevel * multiplier;

					ItemAttributes::CustomAttribute val;
					val.set<int64_t>(newTierLevel);
					setCustomAttribute(str, val);

					std::unordered_map<int, std::string> attributeNames = {
						{ ITEM_RND_BLEEDING, "\nBleeding" },
						{ ITEM_RND_ELETRICFYING, "\nEletricfying" },
						{ ITEM_RND_BURNING, "\nBurning" },
						{ ITEM_RND_POISONING, "\nPoisoning" }
					};

					std::string effectName = attributeNames[attr];

					dataDescription = fmt::format("{:s}{} {:d}%", dataDescription, effectName, static_cast<uint16_t>(newTierLevel));
				} else {
					inc = false;
				}
				break;
			}

			default: 
				std::cout << fmt::format("Item::rollAttributes: Unhandled RNG {:d} for Tier {:d}", static_cast<uint16_t>(attr), static_cast<uint16_t>(tierLevel)) << std::endl;
				break;
		}

		if (inc) {
			i++;
		}
	}

	setSpecialDescription(dataDescription);
}

std::string Item::getWeightDescription(uint32_t weight) const
{
	const ItemType& it = Item::items[id];
	return getWeightDescription(it, weight, getItemCount());
}

std::string Item::getWeightDescription() const
{
	uint32_t weight = getWeight();
	if (weight == 0) {
		return std::string();
	}
	return getWeightDescription(weight);
}

void Item::setUniqueId(uint32_t n)
{
	if (hasAttribute(ITEM_ATTRIBUTE_UNIQUEID)) {
		return;
	}

	if (g_game.addUniqueItem(n, this)) {
		getAttributes()->setUniqueId(n);
	}
}

bool Item::canDecay() const
{
	if (isRemoved()) {
		return false;
	}

	const ItemType& it = Item::items[id];
	if (getDecayTo() < 0 || it.decayTime == 0) {
		return false;
	}

	if (hasAttribute(ITEM_ATTRIBUTE_UNIQUEID)) {
		return false;
	}

	if (getActionId() >= 1000 && getActionId() <= 2000) {
		return false;
	}

	return true;
}

uint32_t Item::getWorth() const
{
	switch (id) {
		case ITEM_GOLD_COIN:
			return count;

		case ITEM_PLATINUM_COIN:
			return count * 100;

		case ITEM_CRYSTAL_COIN:
			return count * 10000;

		default:
			return 0;
	}
}

LightInfo Item::getLightInfo() const
{
	const ItemType& it = items[id];
	return {it.lightLevel, it.lightColor};
}

std::string ItemAttributes::emptyString;
int64_t ItemAttributes::emptyInt;
double ItemAttributes::emptyDouble;
bool ItemAttributes::emptyBool;

const std::string& ItemAttributes::getStrAttr(itemAttrTypes type) const
{
	if (!isStrAttrType(type)) {
		return emptyString;
	}

	const Attribute* attr = getExistingAttr(type);
	if (!attr) {
		return emptyString;
	}
	return *attr->value.string;
}

void ItemAttributes::setStrAttr(itemAttrTypes type, const std::string& value)
{
	if (!isStrAttrType(type)) {
		return;
	}

	if (value.empty()) {
		return;
	}

	Attribute& attr = getAttr(type);
	delete attr.value.string;
	attr.value.string = new std::string(value);
}

void ItemAttributes::removeAttribute(itemAttrTypes type)
{
	if (!hasAttribute(type)) {
		return;
	}

	auto prev_it = attributes.rbegin();
	if ((*prev_it).type == type) {
		attributes.pop_back();
	} else {
		auto it = prev_it, end = attributes.rend();
		while (++it != end) {
			if ((*it).type == type) {
				(*it) = attributes.back();
				attributes.pop_back();
				break;
			}
		}
	}
	attributeBits &= ~type;
}

int64_t ItemAttributes::getIntAttr(itemAttrTypes type) const
{
	if (!isIntAttrType(type)) {
		return 0;
	}

	const Attribute* attr = getExistingAttr(type);
	if (!attr) {
		return 0;
	}
	return attr->value.integer;
}

void ItemAttributes::setIntAttr(itemAttrTypes type, int64_t value)
{
	if (!isIntAttrType(type)) {
		return;
	}

	if (type == ITEM_ATTRIBUTE_ATTACK_SPEED && value < 100) {
		value = 100;
	}

	getAttr(type).value.integer = value;
}

void ItemAttributes::increaseIntAttr(itemAttrTypes type, int64_t value)
{
	setIntAttr(type, getIntAttr(type) + value);
}

const ItemAttributes::Attribute* ItemAttributes::getExistingAttr(itemAttrTypes type) const
{
	if (hasAttribute(type)) {
		for (const Attribute& attribute : attributes) {
			if (attribute.type == type) {
				return &attribute;
			}
		}
	}
	return nullptr;
}

ItemAttributes::Attribute& ItemAttributes::getAttr(itemAttrTypes type)
{
	if (hasAttribute(type)) {
		for (Attribute& attribute : attributes) {
			if (attribute.type == type) {
				return attribute;
			}
		}
	}

	attributeBits |= type;
	attributes.emplace_back(type);
	return attributes.back();
}

void Item::startDecaying()
{
	if (getActionId() >= 1000 && getActionId() <= 2000) {
		// Quest items should never decay
		return;
	}

	g_game.startDecay(this);
}

bool Item::isHouseItem() const 
{
	const ItemType& type = Item::items.getItemType(getID());
	return type.isDoor() || type.moveable || type.forceSerialize || type.isBed() || type.canWriteText || type.isContainer();
}

template<>
const std::string& ItemAttributes::CustomAttribute::get<std::string>() {
	if (value.type() == typeid(std::string)) {
		return boost::get<std::string>(value);
	}

	return emptyString;
}

template<>
const int64_t& ItemAttributes::CustomAttribute::get<int64_t>() {
	if (value.type() == typeid(int64_t)) {
		return boost::get<int64_t>(value);
	}

	return emptyInt;
}

template<>
const double& ItemAttributes::CustomAttribute::get<double>() {
	if (value.type() == typeid(double)) {
		return boost::get<double>(value);
	}

	return emptyDouble;
}

template<>
const bool& ItemAttributes::CustomAttribute::get<bool>() {
	if (value.type() == typeid(bool)) {
		return boost::get<bool>(value);
	}

	return emptyBool;
}
