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

#ifndef FS_SPAWN_H_1A86089E080846A9AE53ED12E7AE863B
#define FS_SPAWN_H_1A86089E080846A9AE53ED12E7AE863B

#include "tile.h"
#include "position.h"

#include <utility>
#include <vector>

class Monster;
class MonsterType;
class Npc;

static constexpr uint32_t SPAWN_CHECK_INTERVAL = 5000;

struct spawnBlock_t {
	Position pos;
	MonsterType* mType;
	uint32_t interval = 0;
	uint32_t amount = 0;
	int64_t nextSpawnTime = 0;
	Direction direction = DIRECTION_SOUTH;
};

class BaseSpawn
{
	public:
		BaseSpawn() = default;
		~BaseSpawn() = default;

		virtual void startup() = 0;

		void startSpawnCheck(uint32_t interval);
		void stopSpawnCheck();

		void increaseMonsterCount() {
			activeMonsters++;
		}
		void decreaseMonsterCount() {
			activeMonsters--;
		}
		void removeMonster(const std::string& name);

		const Position& getCenterPos() const {
			return centerPos;
		}
		void setCenterPos(const Position& pos) {
			centerPos = pos;
		}
		uint8_t getRadius() const {
			return radius;
		}
		void setRadius(uint8_t newRadius) {
			radius = newRadius;
		}

		virtual void addMonster(const std::string& name, const Position& pos, Direction& dir, uint32_t interval, uint32_t amount) = 0;
	protected:
		static bool isPlayerAround(const Position& pos);

		virtual void checkSpawn() = 0;

		bool spawnMonster(spawnBlock_t& sb, bool startup = false);
		bool spawnMonster(MonsterType* mType, const Position& pos, Direction dir, uint32_t interval, bool forceSpawn = false);

		Position centerPos;

		uint8_t radius = 0;
		uint32_t activeMonsters = 0;
		uint32_t checkSpawnEvent = 0;
};

class Spawn : public BaseSpawn
{
	public:
		Spawn(const Position& pos, int32_t radius) {
			this->centerPos = pos;
			this->radius = static_cast<uint8_t>(radius);
		}
		~Spawn() = default;

		// non-copyable
		Spawn(const Spawn&) = delete;
		Spawn& operator=(const Spawn&) = delete;

		void startup() final;
		void addMonster(const std::string& name, const Position& pos, Direction& dir, uint32_t interval, uint32_t amount) final;
	private:
		void checkSpawn() final;

		std::vector<spawnBlock_t> spawnMap;
};

class TvpSpawn : public BaseSpawn
{
public:
	TvpSpawn(const Position& pos, int32_t radius) {
		this->centerPos = pos;
		this->radius = static_cast<uint8_t>(radius);
	}
	~TvpSpawn() = default;

	// non-copyable
	TvpSpawn(const TvpSpawn&) = delete;
	TvpSpawn& operator=(const TvpSpawn&) = delete;

	void startup() final;
	void addMonster(const std::string& name, const Position& pos, Direction& dir, uint32_t interval, uint32_t amount) final;
private:
	void checkSpawn() final;

	spawnBlock_t monsterSpawn;
};

class Spawns
{
	public:
		~Spawns() {
			for (auto* spawn : spawnList) {
				delete spawn;
			}

			for (auto* spawn : tvpSpawnList) {
				delete spawn;
			}
		}
		static bool isInZone(const Position& centerPos, int32_t radius, const Position& pos);

		bool loadFromXml(const std::string& filename);
		void startup();
		void clear();

		bool isStarted() const {
			return started;
		}

		static int32_t calculateSpawnDelay(int32_t delay);
	private:
		std::forward_list<Npc*> npcList;
		std::forward_list<Spawn*> spawnList;
		std::forward_list<TvpSpawn*> tvpSpawnList;
		std::string filename;
		bool loaded = false;
		bool started = false;
};

#endif
