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

#include "networkmessage.h"

#include "container.h"
#include "creature.h"

std::string NetworkMessage::getString(uint16_t stringLen/* = 0*/)
{
	if (stringLen == 0) {
		stringLen = get<uint16_t>();
	}

	if (!canRead(stringLen)) {
		return std::string();
	}

	char* v = reinterpret_cast<char*>(buffer) + info.position; //does not break strict aliasing
	info.position += stringLen;
	return std::string(v, stringLen);
}

Position NetworkMessage::getPosition()
{
	Position pos;
	pos.x = get<uint16_t>();
	pos.y = get<uint16_t>();
	pos.z = getByte();
	return pos;
}

void NetworkMessage::addString(const std::string& value)
{
	size_t stringLen = value.length();
	if (!canAdd(stringLen + 2) || stringLen > 8192) {
		return;
	}

	add<uint16_t>(stringLen);
	memcpy(buffer + info.position, value.c_str(), stringLen);
	info.position += stringLen;
	info.length += stringLen;
}

void NetworkMessage::addDouble(double value, uint8_t precision/* = 2*/)
{
	addByte(precision);
	add<uint32_t>(static_cast<uint32_t>((value * std::pow(static_cast<float>(10), precision)) + std::numeric_limits<int32_t>::max()));
}

void NetworkMessage::addBytes(const char* bytes, size_t size)
{
	if (!canAdd(size) || size > 8192) {
		return;
	}

	memcpy(buffer + info.position, bytes, size);
	info.position += size;
	info.length += size;
}

void NetworkMessage::addPaddingBytes(size_t n)
{
	if (!canAdd(n)) {
		return;
	}

	memset(buffer + info.position, 0x33, n);
	info.length += n;
}

void NetworkMessage::addPosition(const Position& pos)
{
	add<uint16_t>(pos.x);
	add<uint16_t>(pos.y);
	addByte(pos.z);
}

void NetworkMessage::addItem(uint16_t id, uint8_t count)
{
	const ItemType& it = Item::items[id];

	add<uint16_t>(it.clientId);

	if (it.charges > 0) {
		addByte(2);
		addByte(std::min<uint16_t>(0xFF, it.charges));
	} else if (it.stackable) {
		addByte(1);
		addByte(std::min<uint16_t>(0xFF, count));
	} else if (it.isSplash() || it.isFluidContainer()) {
		std::cout << "enviou splash" << std::endl;
		addByte(4);
		addByte(getLiquidColor(count));
	} else {
		addByte(0);
	}
}

void NetworkMessage::addItem(const Item* item)
{
	const ItemType& it = Item::items[item->getID()];

	add<uint16_t>(it.clientId);

	//if (item->getDuration() > 0 && !it.isSplash() || item->getDuration() > 0 && !it.isFluidContainer()) {
	//	addByte(3);
	//	uint32_t duration = item->getDuration() / 1000;
	//	add<uint32_t>(duration);
	//} 
	
	if (item->getCharges() > 0) {
		addByte(2);
		addByte(std::min<uint16_t>(0xFF, item->getCharges()));
	} else if (it.stackable) {
		addByte(1);
		addByte(std::min<uint16_t>(0xFF, item->getItemCount()));
	} else if (it.isSplash() || it.isFluidContainer()) {
		addByte(4);
		addByte(getLiquidColor(item->getFluidType()));
	} else {
		addByte(0);
	}

	addItemCustomAttributes(item);
}

void NetworkMessage::addItemId(const Item* item)
{
	const ItemType& it = Item::items[item->getID()];
	add<uint16_t>(it.clientId);
}

void NetworkMessage::addItemId(uint16_t itemId)
{
	add<uint16_t>(Item::items[itemId].clientId);
}

void NetworkMessage::addItemCustomAttributes(const Item* item)
{
	auto intCustomAttributeMap = item->getIntCustomAttributeMap();
	if (!intCustomAttributeMap.empty()) {
		add<uint16_t>(static_cast<uint16_t>(intCustomAttributeMap.size()));

		for (const auto& intCustomAttribute : intCustomAttributeMap) {
			uint16_t key = ItemAttributes::customAttributeMapKeyToUint16(intCustomAttribute.first);
			ItemAttributes::CustomAttribute customAttribute = intCustomAttribute.second;

			add<uint16_t>(key);
			add<uint64_t>(static_cast<uint64_t>(customAttribute.get<int64_t>()));
		}
	}
	else {
		add<uint16_t>(0);
	}
}