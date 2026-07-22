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
#include "../../town.h"
#include "../../industry.h"
#include "../../industrytype.h"
#include "../../station_base.h"
#include "../../vehicle_base.h"
#include "../../group.h"
#include "../../engine_base.h"
#include "../../engine_func.h"
#include "../../news_gui.h"
#include "../../news_type.h"
#include "../../strings_func.h"
#include "../../economy_type.h"
#include "../../core/math_func.hpp"
#include "../../3rdparty/nlohmann/json.hpp"

#include "table/strings.h"

#include <emscripten.h>
#include <cstring>
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

namespace {

/** Expense category keys for financeDetail (current year, yearly_expenses[0]). */
static constexpr const char *const _sct_expense_keys[EXPENSES_END] = {
	"construction",
	"new_vehicles",
	"train_run",
	"roadveh_run",
	"aircraft_run",
	"ship_run",
	"property",
	"train_revenue",
	"roadveh_revenue",
	"aircraft_revenue",
	"ship_revenue",
	"loan_interest",
	"other",
};

static bool SctInGame()
{
	return _game_mode == GM_NORMAL || _game_mode == GM_EDITOR;
}

static nlohmann::json SctQueryTowns()
{
	nlohmann::json arr = nlohmann::json::array();
	if (!SctInGame() || !Map::IsInitialized()) return arr;

	for (const Town *t : Town::Iterate()) {
		arr.push_back({
			{"id", t->index.base()},
			{"name", t->GetCachedName()},
			{"population", t->cache.population},
			{"houses", t->cache.num_houses},
			{"x", TileX(t->xy)},
			{"y", TileY(t->xy)},
		});
	}
	return arr;
}

static nlohmann::json SctQueryIndustries()
{
	nlohmann::json arr = nlohmann::json::array();
	if (!SctInGame() || !Map::IsInitialized()) return arr;

	for (const Industry *i : Industry::Iterate()) {
		nlohmann::json entry = {
			{"id", i->index.base()},
			{"type", i->type},
			{"x", TileX(i->location.tile)},
			{"y", TileY(i->location.tile)},
			{"productionLevel", i->prod_level},
		};

		const IndustrySpec *spec = GetIndustrySpec(i->type);
		if (spec != nullptr && spec->name != STR_NULL && spec->name != INVALID_STRING_ID) {
			entry["typeName"] = GetString(spec->name);
		}

		arr.push_back(std::move(entry));
	}
	return arr;
}

static nlohmann::json SctQueryStations()
{
	nlohmann::json arr = nlohmann::json::array();
	if (!SctInGame() || !Map::IsInitialized()) return arr;

	for (const Station *st : Station::Iterate()) {
		arr.push_back({
			{"id", st->index.base()},
			{"name", st->GetCachedName()},
			{"owner", st->owner.base()},
			{"facilities", st->facilities.base()},
			{"x", TileX(st->xy)},
			{"y", TileY(st->xy)},
		});
	}
	return arr;
}

static nlohmann::json SctQueryVehicles()
{
	nlohmann::json arr = nlohmann::json::array();
	if (!SctInGame()) return arr;

	for (const Vehicle *v : Vehicle::Iterate()) {
		if (!v->IsPrimaryVehicle()) continue;
		if (v->owner != _local_company) continue;

		arr.push_back({
			{"id", v->index.base()},
			{"name", v->name},
			{"type", static_cast<int>(v->type)},
			{"ownerIsLocal", true},
			{"speed", v->GetDisplaySpeed()},
			{"profitThisYear", static_cast<int64_t>(v->GetDisplayProfitThisYear())},
			{"profitLastYear", static_cast<int64_t>(v->GetDisplayProfitLastYear())},
			{"age", v->age.base()},
			{"groupId", v->group_id.base()},
		});
	}
	return arr;
}

static nlohmann::json SctQueryGroups()
{
	nlohmann::json arr = nlohmann::json::array();
	if (!SctInGame()) return arr;

	for (const Group *g : Group::Iterate()) {
		if (g->owner != _local_company) continue;

		arr.push_back({
			{"id", g->index.base()},
			{"name", g->name},
			{"vehicleType", static_cast<int>(g->vehicle_type)},
			{"numVehicles", g->statistics.num_vehicle},
		});
	}
	return arr;
}

static nlohmann::json SctQueryFinanceDetail()
{
	if (!SctInGame()) return nullptr;

	const Company *c = Company::GetIfValid(_local_company);
	if (c == nullptr) return nullptr;

	nlohmann::json expenses = nlohmann::json::object();
	for (ExpensesType et = EXPENSES_CONSTRUCTION; et < EXPENSES_END; et = static_cast<ExpensesType>(et + 1)) {
		expenses[_sct_expense_keys[et]] = static_cast<int64_t>(c->yearly_expenses[0][et]);
	}

	return {
		{"money", static_cast<int64_t>(c->money)},
		{"loan", static_cast<int64_t>(c->current_loan)},
		{"maxLoan", static_cast<int64_t>(c->GetMaxLoan())},
		{"yearlyExpenses", std::move(expenses)},
	};
}

static nlohmann::json SctQueryEngines()
{
	nlohmann::json arr = nlohmann::json::array();
	if (!SctInGame()) return arr;

	for (const Engine *e : Engine::Iterate()) {
		if (!IsEngineBuildable(e->index, e->type, _local_company)) continue;

		TimerGameCalendar::YearMonthDay ymd = TimerGameCalendar::ConvertDateToYMD(e->intro_date);

		std::string name;
		if (!e->name.empty()) {
			name = e->name;
		} else {
			name = GetString(STR_ENGINE_NAME, e->index);
		}

		arr.push_back({
			{"id", e->index.base()},
			{"name", name},
			{"type", static_cast<int>(e->type)},
			{"introYear", ymd.year.base()},
			{"reliability", static_cast<int>(ToPercent16(e->reliability))},
			{"cost", static_cast<int64_t>(e->GetCost())},
		});
	}
	return arr;
}

static nlohmann::json SctQueryNews()
{
	nlohmann::json arr = nlohmann::json::array();
	if (!SctInGame()) return arr;

	int count = 0;
	for (const NewsItem &news : GetNews()) {
		TimerGameCalendar::YearMonthDay ymd = TimerGameCalendar::ConvertDateToYMD(news.date);
		arr.push_back({
			{"date", {
				{"y", ymd.year.base()},
				{"m", ymd.month},
				{"d", ymd.day},
			}},
			{"typeId", static_cast<int>(news.type)},
			{"headline", news.GetStatusText()},
		});
		if (++count >= 20) break;
	}
	return arr;
}

} // namespace

/**
 * Query entity data for the external UI.
 * @param kind Null-terminated query kind (towns, industries, stations, vehicles, groups,
 *             financeDetail, engines, news).
 * @return Pointer to a static JSON string buffer (valid until the next call to this or sct_get_state).
 */
const char *EMSCRIPTEN_KEEPALIVE sct_query(const char *kind)
{
	static std::string buffer;

	nlohmann::json j;
	if (kind == nullptr) {
		j = {{"error", "unknown kind"}};
	} else if (std::strcmp(kind, "towns") == 0) {
		j = SctQueryTowns();
	} else if (std::strcmp(kind, "industries") == 0) {
		j = SctQueryIndustries();
	} else if (std::strcmp(kind, "stations") == 0) {
		j = SctQueryStations();
	} else if (std::strcmp(kind, "vehicles") == 0) {
		j = SctQueryVehicles();
	} else if (std::strcmp(kind, "groups") == 0) {
		j = SctQueryGroups();
	} else if (std::strcmp(kind, "financeDetail") == 0) {
		j = SctQueryFinanceDetail();
	} else if (std::strcmp(kind, "engines") == 0) {
		j = SctQueryEngines();
	} else if (std::strcmp(kind, "news") == 0) {
		j = SctQueryNews();
	} else {
		j = {{"error", "unknown kind"}};
	}

	buffer = j.dump();
	return buffer.c_str();
}

} /* extern "C" */

#endif /* __EMSCRIPTEN__ */
