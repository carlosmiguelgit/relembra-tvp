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

#ifndef FS_BED_H_84DE19758D424C6C9789189231946BFF
#define FS_BED_H_84DE19758D424C6C9789189231946BFF

#include "item.h"

class House;
class Player;

class BedItem final : public Item
{
	public:
		explicit BedItem(uint16_t id) : Item(id) {}

		BedItem* getBed() override {
			return this;
		}
		const BedItem* getBed() const override {
			return this;
		}

		Attr_ReadValue readAttr(AttrTypes_t attr, PropStream& propStream) override;
		void serializeAttr(PropWriteStream& propWriteStream) const override;

		uint32_t getSleeper() const {
			return sleeperGUID;
		}

		House* getHouse() const {
			return house;
		}
		void setHouse(House* h) {
			house = h;
		}

		bool canUse(Player* player);

		bool trySleep(Player* player);
		bool sleep(Player* player);
		void wakeUp(Player* player);

		BedItem* getNextBedItem() const;

	protected:
		void updateAppearance(const Player* player);
		void regeneratePlayer(Player* player) const;
		void internalSetSleeper(const Player* player);
		void internalRemoveSleeper();

		House* house = nullptr;
		uint64_t sleepStart = 0;
		uint32_t sleeperGUID = 0;

		friend class Item;
};

#endif
