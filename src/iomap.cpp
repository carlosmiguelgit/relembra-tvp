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

#include "iomap.h"

#include "bed.h"
#include "game.h"

#include <fmt/format.h>

/*
	OTBM_ROOTV1
	|
	|--- OTBM_MAP_DATA
	|	|
	|	|--- OTBM_TILE_AREA
	|	|	|--- OTBM_TILE
	|	|	|--- OTBM_TILE_SQUARE (not implemented)
	|	|	|--- OTBM_TILE_REF (not implemented)
	|	|	|--- OTBM_HOUSETILE
	|	|
	|	|--- OTBM_SPAWNS (not implemented)
	|	|	|--- OTBM_SPAWN_AREA (not implemented)
	|	|	|--- OTBM_MONSTER (not implemented)
	|	|
	|	|--- OTBM_TOWNS
	|	|	|--- OTBM_TOWN
	|	|
	|	|--- OTBM_WAYPOINTS
	|		|--- OTBM_WAYPOINT
	|
	|--- OTBM_ITEM_DEF (not implemented)
*/

Tile* IOMap::createTile(Item*& ground, Item* item, uint16_t x, uint16_t y, uint8_t z)
{
	if (!ground) {
		return new StaticTile(x, y, z);
	}

	Tile* tile;
	if ((item && item->isBlocking()) || ground->isBlocking()) {
		tile = new StaticTile(x, y, z);
	} else {
		tile = new DynamicTile(x, y, z);
	}

	tile->internalAddThing(ground);
	ground->startDecaying();
	ground = nullptr;
	return tile;
}

bool IOMap::loadMap(Map* map, const std::string& fileName, bool replaceExistingTiles)
{
	std::cout << "> Loading " << fileName << std::endl;

	int64_t start = OTSYS_TIME();
	try {
		OTB::Loader loader{fileName, OTB::Identifier{{'O', 'T', 'B', 'M'}}};
		auto& root = loader.parseTree();

		PropStream propStream;
		if (!loader.getProps(root, propStream)) {
			setLastErrorString("Could not read root property.");
			return false;
		}

		OTBM_root_header root_header;
		if (!propStream.read(root_header)) {
			setLastErrorString("Could not read header.");
			return false;
		}

		uint32_t headerVersion = root_header.version;
		if (headerVersion == 0) {
			headerVersion = 1;
		}

		if (headerVersion > 2) {
			setLastErrorString("Unknown OTBM version detected.");
			return false;
		}

		std::cout << "> Map size: " << root_header.width << "x" << root_header.height << '.' << std::endl;

		map->width = root_header.width;
		map->height = root_header.height;

		if (root.children.size() != 1 || root.children[0].type != OTBM_MAP_DATA) {
			setLastErrorString("Could not read data node.");
			return false;
		}

		auto& mapNode = root.children[0];
		if (!parseMapDataAttributes(loader, mapNode, *map, fileName)) {
			return false;
		}

		for (auto& mapDataNode : mapNode.children) {
			if (mapDataNode.type == OTBM_TILE_AREA) {
				if (!parseTileArea(loader, mapDataNode, *map, replaceExistingTiles)) {
					return false;
				}
			} else if (mapDataNode.type == OTBM_TOWNS) {
				if (!parseTowns(loader, mapDataNode, *map)) {
					return false;
				}
			} else if (mapDataNode.type == OTBM_WAYPOINTS && headerVersion > 1) {
				if (!parseWaypoints(loader, mapDataNode, *map)) {
					return false;
				}
			} else {
				setLastErrorString("Unknown map node.");
				return false;
			}
		}
	} catch (const OTB::InvalidOTBFormat& err) {
		setLastErrorString(err.what());
		return false;
	}

	std::cout << "> Map loading time: " << (OTSYS_TIME() - start) / (1000.) << " seconds." << std::endl;
	return true;
}

bool IOMap::parseMapDataAttributes(OTB::Loader& loader, const OTB::Node& mapNode, Map& map, const std::string& fileName)
{
	PropStream propStream;
	if (!loader.getProps(mapNode, propStream)) {
		setLastErrorString("Could not read map data attributes.");
		return false;
	}

	std::string mapDescription;
	std::string tmp;

	uint8_t attribute;
	while (propStream.read<uint8_t>(attribute)) {
		switch (attribute) {
			case OTBM_ATTR_DESCRIPTION:
				if (!propStream.readString(mapDescription)) {
					setLastErrorString("Invalid description tag.");
					return false;
				}
				break;

			case OTBM_ATTR_EXT_SPAWN_FILE:
				if (!propStream.readString(tmp)) {
					setLastErrorString("Invalid spawn tag.");
					return false;
				}

				map.spawnfile = fileName.substr(0, fileName.rfind('/') + 1);
				map.spawnfile += tmp;
				break;

			case OTBM_ATTR_EXT_HOUSE_FILE:
				if (!propStream.readString(tmp)) {
					setLastErrorString("Invalid house tag.");
					return false;
				}

				map.housefile = fileName.substr(0, fileName.rfind('/') + 1);
				map.housefile += tmp;
				break;

			default:
				setLastErrorString("Unknown header node.");
				return false;
		}
	}
	return true;
}

bool IOMap::parseTileArea(OTB::Loader& loader, const OTB::Node& tileAreaNode, Map& map, bool replaceExistingTiles)
{
	PropStream propStream;
	if (!loader.getProps(tileAreaNode, propStream)) {
		setLastErrorString("Invalid map node.");
		return false;
	}

	OTBM_Destination_coords area_coord;
	if (!propStream.read(area_coord)) {
		setLastErrorString("Invalid map node.");
		return false;
	}

	const uint16_t base_x = area_coord.x;
	const uint16_t base_y = area_coord.y;
	const uint16_t z = area_coord.z;

	for (const auto& tileNode : tileAreaNode.children) {
		if (tileNode.type != OTBM_TILE && tileNode.type != OTBM_HOUSETILE) {
			setLastErrorString("Unknown tile node.");
			return false;
		}

		PropStream tilePropStream;
		if (!loader.getProps(tileNode, tilePropStream)) {
			setLastErrorString("Could not read node data.");
			return false;
		}

		OTBM_Tile_coords tile_coord;
		if (!tilePropStream.read(tile_coord)) {
			setLastErrorString("Could not read tile position.");
			return false;
		}

		const uint16_t x = base_x + tile_coord.x;
		const uint16_t y = base_y + tile_coord.y;

		bool allowDecay = (map.getTile(x, y, z) == nullptr) || replaceExistingTiles;
		bool isHouseTile = false;
		House* house = nullptr;
		Tile* tile = nullptr;
		Item* ground_item = nullptr;
		uint32_t tileflags = TILESTATE_NONE;

		if (tileNode.type == OTBM_HOUSETILE) {
			uint32_t houseId;
			if (!tilePropStream.read<uint32_t>(houseId)) {
				setLastErrorString(fmt::format("[x:{}, y:{}, z:{}] Could not read house id.", x, y, z));
				return false;
			}

			house = map.houses.addHouse(houseId);
			if (!house) {
				setLastErrorString(fmt::format("[x:{}, y:{}, z:{}] Could not create house id: {}", x, y, z, houseId));
				return false;
			}

			tile = new HouseTile(x, y, z, house);
			house->addTile(static_cast<HouseTile*>(tile));
			isHouseTile = true;
		}

		uint8_t attribute;
		while (tilePropStream.read<uint8_t>(attribute)) {
			switch (attribute) {
				case OTBM_ATTR_TILE_FLAGS: {
					uint32_t flags;
					if (!tilePropStream.read<uint32_t>(flags)) {
						setLastErrorString(fmt::format("[x:{}, y:{}, z:{}] Failed to read tile flags.", x, y, z));
						return false;
					}

					if (flags & OTBM_TILEFLAG_PROTECTIONZONE) tileflags |= TILESTATE_PROTECTIONZONE;
					if (flags & OTBM_TILEFLAG_NOPVPZONE)      tileflags |= TILESTATE_NOPVPZONE;
					else if (flags & OTBM_TILEFLAG_PVPZONE)   tileflags |= TILESTATE_PVPZONE;
					if (flags & OTBM_TILEFLAG_REFRESH)        tileflags |= TILESTATE_REFRESH;
					if (flags & OTBM_TILEFLAG_NOLOGOUT)       tileflags |= TILESTATE_NOLOGOUT;

					break;
				}

				case OTBM_ATTR_ITEM: {
					Item* item = Item::CreateItem(tilePropStream);
					if (!item) {
						// Força continuar se não conseguir criar o item
						std::cerr << "[Warning] Failed to create inline item at [x:" << x << ", y:" << y << ", z:" << z << "]. Skipping.\n";
						continue;
					}

					if (item->getItemCount() == 0) {
						item->setItemCount(1);
					}

					if (tile) {
						tile->internalAddThing(item);
						if (allowDecay) {
							item->startDecaying();
						}
					} else if (item->isGroundTile()) {
						delete ground_item;
						ground_item = item;
					} else {
						tile = createTile(ground_item, item, x, y, z);
						tile->internalAddThing(item);
						item->startDecaying();
					}
					break;
				}

				default:
					std::cerr << "[Warning] Unknown tile attribute " << static_cast<int>(attribute)
							  << " at [x:" << x << ", y:" << y << ", z:" << z << "]. Skipping.\n";
					// Instead of failing, just skip unknown attribute
					break;
			}
		}

		for (const auto& itemNode : tileNode.children) {
			if (itemNode.type != OTBM_ITEM) {
				std::cerr << "[Warning] Unknown node type in tile at [x:" << x << ", y:" << y << ", z:" << z << "]. Skipping.\n";
				continue;
			}

			PropStream stream;
			if (!loader.getProps(itemNode, stream)) {
				std::cerr << "[Warning] Invalid item node at [x:" << x << ", y:" << y << ", z:" << z << "]. Skipping.\n";
				continue;
			}

			Item* item = Item::CreateItem(stream);
			if (!item) {
				std::cerr << "[Warning] Failed to create item in node at [x:" << x << ", y:" << y << ", z:" << z << "]. Skipping.\n";
				continue;
			}

			if (!item->unserializeItemNode(loader, itemNode, stream)) {
				delete item;
				continue;
			}

			if (item->getItemCount() == 0) {
				item->setItemCount(1);
			}

			if (tile) {
				tile->internalAddThing(item);
				if (allowDecay) {
					item->startDecaying();
				}
			} else if (item->isGroundTile()) {
				delete ground_item;
				ground_item = item;
			} else {
				tile = createTile(ground_item, item, x, y, z);
				tile->internalAddThing(item);
				item->startDecaying();
			}
		}

		if (!tile) {
			tile = createTile(ground_item, nullptr, x, y, z);
		}

		tile->setFlag(static_cast<tileflags_t>(tileflags));
		tile->makeRefreshItemList();

		map.setTile(x, y, z, tile, replaceExistingTiles);
	}

	return true;
}

bool IOMap::parseTowns(OTB::Loader& loader, const OTB::Node& townsNode, Map& map)
{
	for (auto& townNode : townsNode.children) {
		PropStream propStream;
		if (townNode.type != OTBM_TOWN) {
			setLastErrorString("Unknown town node.");
			return false;
		}

		if (!loader.getProps(townNode, propStream)) {
			setLastErrorString("Could not read town data.");
			return false;
		}

		uint32_t townId;
		if (!propStream.read<uint32_t>(townId)) {
			setLastErrorString("Could not read town id.");
			return false;
		}

		Town* town = map.towns.getTown(townId);
		if (!town) {
			town = new Town(townId);
			map.towns.addTown(townId, town);
		}

		std::string townName;
		if (!propStream.readString(townName)) {
			setLastErrorString("Could not read town name.");
			return false;
		}

		town->setName(townName);

		OTBM_Destination_coords town_coords;
		if (!propStream.read(town_coords)) {
			setLastErrorString("Could not read town coordinates.");
			return false;
		}

		town->setTemplePos(Position(town_coords.x, town_coords.y, town_coords.z));
	}
	return true;
}

bool IOMap::parseWaypoints(OTB::Loader& loader, const OTB::Node& waypointsNode, Map& map)
{
	PropStream propStream;
	for (auto& node : waypointsNode.children) {
		if (node.type != OTBM_WAYPOINT) {
			setLastErrorString("Unknown waypoint node.");
			return false;
		}

		if (!loader.getProps(node, propStream)) {
			setLastErrorString("Could not read waypoint data.");
			return false;
		}

		std::string name;
		if (!propStream.readString(name)) {
			setLastErrorString("Could not read waypoint name.");
			return false;
		}

		OTBM_Destination_coords waypoint_coords;
		if (!propStream.read(waypoint_coords)) {
			setLastErrorString("Could not read waypoint coordinates.");
			return false;
		}

		map.waypoints[name] = Position(waypoint_coords.x, waypoint_coords.y, waypoint_coords.z);
	}
	return true;
}

