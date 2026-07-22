/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file sct_bridge.cpp JS ⇄ C++ bridge for Supplychain Tycoon external UI (Emscripten). */

#ifdef __EMSCRIPTEN__

#include "../../stdafx.h"
#include "../../console_func.h"
#include "../../openttd.h"
#include "../../gfx_func.h"
#include "../../company_base.h"
#include "../../company_func.h"
#include "../../map_func.h"
#include "../../timer/timer_game_calendar.h"
#include "../../3rdparty/nlohmann/json.hpp"

#include <emscripten.h>
#include <string>

extern "C" {

/**
 * Execute a console command string (same path as dedicated server / admin).
 * @param cmd Null-terminated console command.
 */
void EMSCRIPTEN_KEEPALIVE sct_console_exec(const char *cmd)
{
	if (cmd == nullptr) return;
	IConsoleCmdExec(cmd);
}

/**
 * Return a JSON snapshot of high-level game state for the external UI.
 * @return Pointer to a static string buffer (valid until the next call).
 */
const char *EMSCRIPTEN_KEEPALIVE sct_get_state()
{
	static std::string buffer;

	nlohmann::json j;

	switch (_game_mode) {
		case GM_NORMAL: j["gameMode"] = "normal"; break;
		case GM_EDITOR: j["gameMode"] = "editor"; break;
		case GM_MENU:
		case GM_BOOTSTRAP:
		default:        j["gameMode"] = "menu"; break;
	}

	j["paused"] = _pause_mode.Any();
	j["fastForward"] = (_game_speed != 100);

	{
		TimerGameCalendar::YearMonthDay ymd = TimerGameCalendar::ConvertDateToYMD(TimerGameCalendar::date);
		j["date"] = {
			{"year", ymd.year.base()},
			{"month", ymd.month},
			{"day", ymd.day},
		};
	}

	if (_game_mode == GM_MENU || _game_mode == GM_BOOTSTRAP) {
		j["company"] = nullptr;
	} else {
		const Company *c = Company::GetIfValid(_local_company);
		if (c == nullptr) {
			j["company"] = nullptr;
		} else {
			std::string name;
			if (!c->name.empty()) {
				name = c->name;
			} else {
				name = fmt::format("Company {}", c->index.base() + 1);
			}
			j["company"] = {
				{"id", c->index.base()},
				{"name", name},
				{"money", static_cast<int64_t>(c->money)},
				{"loan", static_cast<int64_t>(c->current_loan)},
			};
		}
	}

	if (_game_mode == GM_MENU || _game_mode == GM_BOOTSTRAP || !Map::IsInitialized()) {
		j["mapX"] = 0;
		j["mapY"] = 0;
	} else {
		j["mapX"] = Map::SizeX();
		j["mapY"] = Map::SizeY();
	}

	buffer = j.dump();
	return buffer.c_str();
}

/**
 * Enable or disable fast-forward (mirrors the toolbar / hotkey path).
 * @param enabled Non-zero to enable fast-forward, zero to restore 1x.
 */
void EMSCRIPTEN_KEEPALIVE sct_set_fast_forward(int enabled)
{
	ChangeGameSpeed(enabled != 0);
}

/**
 * Request a return to the intro menu (same as console 'part').
 */
void EMSCRIPTEN_KEEPALIVE sct_return_to_menu()
{
	_switch_mode = SM_MENU;
}

} /* extern "C" */

#endif /* __EMSCRIPTEN__ */
