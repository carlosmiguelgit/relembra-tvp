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

#define FS_OTPCH_H_F00C737DA6CA4C8D90F57430C614367F

// Definitions should be global.
#include "definitions.h"

// System headers required in headers should be included here.
#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>
#include <boost/iostreams/device/mapped_file.hpp>
#include <boost/lockfree/stack.hpp>
#include <boost/variant.hpp>
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <fmt/format.h>
#include <fmt/color.h>
#include <forward_list>
#include <functional>
#include <iostream>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <mysql/mysql.h>
#include <pugixml.hpp>
#include <spdlog/spdlog.h>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <valarray>
#include <variant>
#include <vector>

#if __has_include("luajit/lua.hpp")
#include <luajit/lua.hpp>
#else
#include <lua.hpp>
#endif

#include <pugixml.hpp>
