#include "otpch.h"

#include "logger.h"
#include "game.h"
#include "configmanager.h"

#include <fmt/format.h>
#include <spdlog/cfg/env.h>

extern Game g_game;
extern ConfigManager g_config;

void Logger::init()
{
	spdlog::flush_every(std::chrono::seconds(1));

	const std::string logPath = g_config.getString(ConfigManager::LOG_PATH);

	gameLogger = spdlog::basic_logger_mt<spdlog::async_factory>("Game", fmt::format("{:s}/game.log", logPath));
	npcLogger = spdlog::basic_logger_mt<spdlog::async_factory>("NPC", fmt::format("{:s}/npc.log", logPath));
	houseLogger = spdlog::basic_logger_mt<spdlog::async_factory>("House", fmt::format("{:s}/house.log", logPath));
	chatLogger = spdlog::basic_logger_mt<spdlog::async_factory>("Chat", fmt::format("{:s}/chat.log", logPath));
	sqlLogger = spdlog::basic_logger_mt<spdlog::async_factory>("SQL", fmt::format("{:s}/sql.log", logPath));

	spdlog::set_level(spdlog::level::trace);
	spdlog::set_default_logger(gameLogger);
	spdlog::flush_on(spdlog::level::critical);

	gameLogger->info("=========================> GAME LOG <=========================");
	npcLogger->info("=========================> NPC LOG <=========================");
	houseLogger->info("=========================> HOUSE LOG <=========================");
	chatLogger->info("=========================> CHAT LOG <=========================");
	sqlLogger->info("=========================> SQL LOG <=========================");
}

void Logger::flush()
{
	gameLogger->flush();
	npcLogger->flush();
	houseLogger->flush();
	chatLogger->flush();
	sqlLogger->flush();
}

void Logger::shutdown()
{
	spdlog::shutdown();
}

void Logger::gameLog(spdlog::level::level_enum level, const std::string& str, bool fileLogOnly)
{
	if (!fileLogOnly) {
		std::cout << str << std::endl;
	}
	gameLogger->log(level, str);
}

void Logger::npcLog(spdlog::level::level_enum level, const std::string& str)
{
	npcLogger->log(level, str);
}

void Logger::houseLog(spdlog::level::level_enum level, const std::string& str)
{
	std::cout << str << std::endl;
	houseLogger->log(level, str);
}

void Logger::chatLog(spdlog::level::level_enum level, const std::string& str)
{
	chatLogger->log(level, str);
}

void Logger::sqlLog(const std::string& str)
{
	sqlLogger->info(str);
}