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

#ifndef FS_IOMAPSERIALIZE_H_7E903658F34E44F9BE03A713B55A3D6D
#define FS_IOMAPSERIALIZE_H_7E903658F34E44F9BE03A713B55A3D6D

#include "map.h"
#include "house.h"

enum MapDataLoadResult_t : uint8_t
{
	MAP_DATA_LOAD_NONE,
	MAP_DATA_LOAD_FOUND,
	MAP_DATA_LOAD_ERROR,
};

enum MapDataTileType_t : uint8_t
{
	MAP_TILE_DYNAMIC,
	MAP_TILE_STATIC,
	MAP_TILE_HOUSE,
};

class IOMapSerialize
{
	public:
		static MapDataLoadResult_t loadMapData();
		static bool loadHouseItems(Map* map);

		static bool saveHouseItems();
		static bool saveMapData();

		static bool loadHouseInfo();
		static bool saveHouseInfo();

		static bool saveHouse(House* house);
		static bool saveHouseTVPFormat(const House* house);

	private:
		static void saveItem(PropWriteStream& stream, const Item* item);
		static void saveTile(PropWriteStream& stream, const Tile* tile);

		static bool loadContainer(PropStream& propStream, Container* container);
		static bool loadItem(PropStream& propStream, Cylinder* parent);
};

#endif
