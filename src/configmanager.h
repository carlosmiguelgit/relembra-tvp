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

#ifndef FS_CONFIGMANAGER_H_6BDD23BD0B8344F4B7C40E8BE6AF6F39
#define FS_CONFIGMANAGER_H_6BDD23BD0B8344F4B7C40E8BE6AF6F39

#include <utility>
#include <vector>
#include "enums.h"

using ExperienceStages = std::vector<std::tuple<uint32_t, uint32_t, float>>;

class ConfigManager
{
	public:
		ConfigManager();

		enum boolean_config_t {
			ALLOW_CHANGEOUTFIT,
			ONE_PLAYER_ON_ACCOUNT,
			REMOVE_RUNE_CHARGES,
			REMOVE_WEAPON_AMMO,
			REMOVE_WEAPON_CHARGES,
			REMOVE_POTION_CHARGES,
			EXPERIENCE_FROM_PLAYERS,
			FREE_PREMIUM,
			REPLACE_KICK_ON_LOGIN,
			ALLOW_CLONES,
			BIND_ONLY_GLOBAL_ADDRESS,
			OPTIMIZE_DATABASE,
			EMOTE_SPELLS,
			STAMINA_SYSTEM,
			WARN_UNSAFE_SCRIPTS,
			CONVERT_UNSAFE_SCRIPTS,
			SCRIPTS_CONSOLE_LOGS,
			YELL_ALLOW_PREMIUM,
			FORCE_MONSTERTYPE_LOAD,
			DEFAULT_WORLD_LIGHT,
			HOUSE_OWNED_BY_ACCOUNT,
			LUA_ITEM_DESC,
			HOUSE_DOOR_SHOW_PRICE,
			ONLY_INVITED_CAN_ADD_HOUSE_ITEMS,
			ONLY_INVITED_CAN_MOVE_HOUSE_ITEMS,
			REMOVE_ON_DESPAWN,
			PLAYER_CONSOLE_LOGS,
			USE_CLASSIC_COMBAT_FORMULAS,
			ALLOW_PLAYER_ROOKING,
			ALLOW_UNFAIRFIGHT_DEATH_REDUCTION,
			SHOW_MONSTER_LOOT_MESSAGE,
			CLASSIC_PLAYER_LOOTDROP,
			MONSTERS_SPAWN_WITH_LOOT,
			PLAYER_INVENTORY_AUTOSTACK,
			ONLY_ONE_FRAG_PER_KILL,
			HOUSES_BANKSYSTEM,
			ENABLE_MAP_REFRESH,
			ENABLE_MAP_DATA_FILES,
			ENABLE_PLAYER_DATA_FILES,
			ENABLE_PLAYER_INVENTORY_DATABASE,
			TRASHABLE_MAILBOX,
			CLASSIC_INVENTORY_SWAP,
			ALLOW_MONSTER_OVERSPAWN,
			NEED_LEARN_SPELLS,
			NO_SPELL_REQUIREMENTS,
			UNLIMITED_PLAYER_HP,
			UNLIMITED_PLAYER_MP,
			DISABLE_MONSTER_SPAWNS,
			HOUSE_DOORS_DISPLAY_HOUSEINFO,
			DEEP_PLAYER_CONTAINER_SEARCH,
			GUILHALLS_ONLYFOR_LEADERS,
			HOUSES_ONLY_PREMIUM,
			UPON_MAP_UPDATE_SENDPLAYERS_TO_TEMPLE,
			GAMEMASTER_DAMAGEPROTECTONZONEEFFECTS,

			LAST_BOOLEAN_CONFIG /* this must be the last one */
		};

		enum string_config_t {
			MAP_NAME,
			HOUSE_RENT_PERIOD,
			SERVER_NAME,
			OWNER_NAME,
			OWNER_EMAIL,
			URL,
			LOCATION,
			IP,
			STATUS_IP,
			MOTD,
			WORLD_TYPE,
			MYSQL_HOST,
			MYSQL_USER,
			MYSQL_PASS,
			MYSQL_DB,
			MYSQL_SOCK,
			DEFAULT_PRIORITY,
			MAP_AUTHOR,
			CONFIG_FILE,
			DISABLED_MAILBOXES,
			ROOK_TOWN_NAME,
			LOGGER_FLAGS,
			ACCOUNT_LOCK_MESSAGE,
			IP_LOCK_MESSAGE,
			LOG_PATH,
			BATTLEPASS_END_DATE,

			LAST_STRING_CONFIG /* this must be the last one */
		};

		enum integer_config_t {
			SQL_PORT,
			MAX_PLAYERS,
			CLIENT_VERSION,
			PZ_LOCKED,
			RATE_EXPERIENCE,
			RATE_SKILL,
			RATE_LOOT,
			RATE_MAGIC,
			RATE_SPAWN,
			HOUSE_PRICE,
			MAX_MESSAGEBUFFER,
			ACTIONS_DELAY_INTERVAL,
			EX_ACTIONS_DELAY_INTERVAL,
			KICK_AFTER_MINUTES,
			PREMIUM_KICK_AFTER_MINUTES,
			PROTECTION_LEVEL,
			DEATH_LOSE_PERCENT,
			STATUSQUERY_TIMEOUT,
			RED_SKULL_DURATION,
			WHITE_SKULL_TIME,
			BAN_DAYS_LENGTH,
			GAME_PORT,
			LOGIN_PORT,
			STATUS_PORT,
			PVP_EXP_FORMULA,
			MAX_PACKETS_PER_SECOND,
			YELL_MINIMUM_LEVEL,
			VIP_FREE_LIMIT,
			VIP_PREMIUM_LIMIT,
			DEPOT_FREE_LIMIT,
			DEPOT_PREMIUM_LIMIT,
			ROOKING_LEVEL,
			KILLS_DAY_RED_SKULL,
			KILLS_WEEK_RED_SKULL,
			KILLS_MONTH_RED_SKULL,
			KILLS_DAY_BANISHMENT,
			KILLS_WEEK_BANISHMENT,
			KILLS_MONTH_BANISHMENT,
			MAP_REFRESH_TILES_PER_CYCLE,
			MAP_REFRESH_INTERVAL,
			MAP_REFRESH_VISIBILITY_INTERVAL,
			TILE_ITEM_LIMIT,
			HOUSE_TILE_ITEM_LIMIT,
			FAILED_LOGINATTEMPTS_ACCOUNT_LOCK,
			FAILED_LOGINATTEMPTS_IP_BAN,
			ACCOUNT_LOCK_DURATION,
			IP_LOCK_DURATION,
			
			ITEM_UNCOMMON_CHANCE,
			ITEM_RARE_CHANCE,
			ITEM_EPIC_CHANCE,
			ITEM_LEGENDARY_CHANCE,

			STATS_DUMP_INTERVAL,
			STATS_SLOW_LOG_TIME,
			STATS_VERY_SLOW_LOG_TIME,

			BESTIARY_KILL_COUNT,
			LAST_INTEGER_CONFIG /* this must be the last one */
		};

		bool load();
		bool reload();

		const std::string& getString(string_config_t what) const;
		int32_t getNumber(integer_config_t what) const;
		bool getBoolean(boolean_config_t what) const;
		float getExperienceStage(uint32_t level) const;

		bool setString(string_config_t what, const std::string& value);
		bool setNumber(integer_config_t what, int32_t value);
		bool setBoolean(boolean_config_t what, bool value);

	private:
		std::string string[LAST_STRING_CONFIG] = {};
		int32_t integer[LAST_INTEGER_CONFIG] = {};
		bool boolean[LAST_BOOLEAN_CONFIG] = {};

		ExperienceStages expStages = {};

		bool loaded = false;
};

#endif
