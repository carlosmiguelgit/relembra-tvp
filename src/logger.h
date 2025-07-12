#pragma once

#include <spdlog/async.h>
#include <spdlog/sinks/basic_file_sink.h>

class Logger
{
	public:
		explicit Logger() = default;
		~Logger() {}

		// non-copyable
		Logger(const Logger&) = delete;
		Logger& operator=(const Logger&) = delete;

		void init();
		void flush();
		void shutdown();

		void gameLog(spdlog::level::level_enum level, const std::string& str, bool fileLogOnly = false);
		void npcLog(spdlog::level::level_enum level, const std::string& str);
		void houseLog(spdlog::level::level_enum level, const std::string& str);
		void chatLog(spdlog::level::level_enum level, const std::string& str);
		void sqlLog(const std::string& str);
	private:
		std::shared_ptr<spdlog::logger> houseLogger;
		std::shared_ptr<spdlog::logger> npcLogger;
		std::shared_ptr<spdlog::logger> gameLogger;
		std::shared_ptr<spdlog::logger> chatLogger;
		std::shared_ptr<spdlog::logger> sqlLogger;
};

extern Logger g_logger;