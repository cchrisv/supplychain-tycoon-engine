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
#include "../../map_type.h"
#include "../../timer/timer_game_calendar.h"
#include "../../town.h"
#include "../../industry.h"
#include "../../industrytype.h"
#include "../../station_base.h"
#include "../../vehicle_base.h"
#include "../../group.h"
#include "../../group_cmd.h"
#include "../../engine_base.h"
#include "../../engine_func.h"
#include "../../news_gui.h"
#include "../../news_type.h"
#include "../../strings_func.h"
#include "../../economy_type.h"
#include "../../core/math_func.hpp"
#include "../../core/backup_type.hpp"
#include "../../command_func.h"
#include "../../rail_cmd.h"
#include "../../road_cmd.h"
#include "../../station_cmd.h"
#include "../../vehicle_cmd.h"
#include "../../train_cmd.h"
#include "../../order_cmd.h"
#include "../../order_type.h"
#include "../../order_base.h"
#include "../../timetable_cmd.h"
#include "../../timer/timer_game_tick.h"
#include "../../misc_cmd.h"
#include "../../landscape_cmd.h"
#include "../../terraform_cmd.h"
#include "../../tunnelbridge_cmd.h"
#include "../../tunnelbridge.h"
#include "../../bridge.h"
#include "../../transport_type.h"
#include "../../waypoint_cmd.h"
#include "../../autoreplace_cmd.h"
#include "../../engine_cmd.h"
#include "../../town_cmd.h"
#include "../../signs_cmd.h"
#include "../../industry_cmd.h"
#include "../../group_type.h"
#include "../../engine_type.h"
#include "../../townname_func.h"
#include "../../settings_type.h"
#include "../../core/random_func.hpp"
#include "../../newgrf_station.h"
#include "../../newgrf_config.h"
#include "../../string_func.h"
#include "../../rail.h"
#include "../../road_func.h"
#include "../../track_type.h"
#include "../../direction_type.h"
#include "../../slope_type.h"
#include "../../cargo_type.h"
#include "../../cargotype.h"
#include "../../network/network_type.h"
#include "../../network/network_content.h"
#include "../../network/network.h"
#include "../../network/network_func.h"
#include "../../network/network_base.h"
#include "../../linkgraph/linkgraph.h"
#include "../../heightmap.h"
#include "../../fios.h"
#include "../../genworld.h"
#include "../../viewport_func.h"
#include "../../window_func.h"
#include "../../error.h"
#include "../../window_gui.h"
#include "../../toolbar_gui.h"
#include "../../statusbar_gui.h"
#include "../../tile_map.h"
#include "../../rail_map.h"
#include "../../road_map.h"
#include "../../station_map.h"
#include "../../water_map.h"
#include "../../depot_map.h"
#include "../../industry_map.h"
#include "../../town_map.h"
#include "../../landscape.h"
#include "../../zoom_func.h"
#include "../../viewport_type.h"
#include "../../viewport_kdtree.h"
#include "../../signs_base.h"
#include "../../object_cmd.h"
#include "../../object_type.h"
#include "../../cheat_type.h"
#include "../../transparency.h"
#include "../../water.h"
#include "../../station_func.h"
#include "../../rail_gui.h"
#include "../../economy_func.h"
#include "../../linkgraph/linkgraphschedule.h"
#include "../../timer/timer_game_economy.h"
#include "../../saveload/saveload.h"
#include "../../fileio_type.h"
#include "../../fileio_func.h"
#include "../../town_type.h"
#include "../../train.h"
#include "../../water_cmd.h"
#include "../../company_cmd.h"
#include "../../company_type.h"
#include "../../vehiclelist.h"
#include "../../subsidy_base.h"
#include "../../source_type.h"
#include "../../ai/ai.hpp"
#include "../../ai/ai_config.hpp"
#include "../../game/game.hpp"
#include "../../game/game_config.hpp"
#include "../../script/script_info.hpp"
#include "../../script/script_scanner.hpp"
#include "../../3rdparty/nlohmann/json.hpp"

#include "table/strings.h"

#include <emscripten.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <strings.h>
#include <tuple>

/* When false (the fork default), idle viewport clicks never open the engine's
 * own windows (station/town/industry/vehicle views) — the React layer routes
 * clicks via sct_tile_info instead. Checked in HandleViewportClicked
 * (viewport.cpp); toggleable at runtime through sct_set_native_click below
 * for debugging against stock behaviour. */
bool _sct_native_viewport_windows = false;
bool _sct_native_chrome_enabled = false;

/* Defined in engine.cpp; refreshes engine availability caches after a date jump.
 * Declared extern here exactly as cheat_gui.cpp does (no public header). */
extern void CalendarEnginesMonthlyLoop();

/* Defined in industry_gui.cpp; when true the scenario editor's industry-build path
 * bypasses the normal placement restrictions (industry_cmd.cpp:1327). Declared extern
 * here exactly as industry_cmd.cpp does (no public header). Wave 22 uses it to mirror
 * the SE "build industry" tool. */
extern bool _ignore_industry_restrictions;

/* Defined in openttd.cpp; copies _settings_newgame → _settings_game so a subsequent
 * new-game / heightmap generation reads the freshly chosen map size. No public header,
 * so declared extern here exactly as misc.cpp / genworld_gui.cpp do. Wave 29 uses it
 * from sct_start_heightmap to mirror genworld_gui.cpp's StartGeneratingLandscape. */
extern void MakeNewgameSettingsLive();

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
 * Let the React HUD replace the stock main toolbar without changing the
 * viewport canvas geometry. Re-enabling restores the native toolbar.
 */
void EMSCRIPTEN_KEEPALIVE sct_set_native_toolbar(int enabled)
{
	_sct_native_chrome_enabled = enabled != 0;
	if (enabled != 0) {
		if (FindWindowById(WC_MAIN_TOOLBAR, 0) == nullptr) AllocateToolbar();
		if (_game_mode == GM_NORMAL && FindWindowById(WC_STATUS_BAR, 0) == nullptr) ShowStatusBar();
	} else {
		CloseWindowById(WC_MAIN_TOOLBAR, 0);
		CloseWindowById(WC_STATUS_BAR, 0);
	}
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
			/* vehstatus VehState::Stopped == parked/not running (vehicle_base.h:309). */
			{"stopped", v->vehstatus.Test(VehState::Stopped)},
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

static nlohmann::json SctQueryCargos()
{
	nlohmann::json arr = nlohmann::json::array();
	if (!SctInGame()) return arr;

	for (const CargoSpec *cs : CargoSpec::Iterate()) {
		arr.push_back({
			{"id", static_cast<int>(cs->Index())},
			{"name", GetString(cs->name)},
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

		/* Wave 26 — consist-editor fields. `wagon` marks a non-motorised rail wagon
		 * (RailVehInfo(e)->railveh_type == RAILVEH_WAGON), always false for non-train
		 * engines. `capacity` is the engine's default-cargo display capacity;
		 * `cargo` its default CargoType (-1 when it carries nothing). */
		const bool wagon = (e->type == VEH_TRAIN) &&
				RailVehInfo(e->index)->railveh_type == RAILVEH_WAGON;
		const CargoType default_cargo = e->GetDefaultCargoType();

		arr.push_back({
			{"id", e->index.base()},
			{"name", name},
			{"type", static_cast<int>(e->type)},
			{"introYear", ymd.year.base()},
			{"reliability", static_cast<int>(ToPercent16(e->reliability))},
			{"cost", static_cast<int64_t>(e->GetCost())},
			{"wagon", wagon},
			{"capacity", static_cast<int>(e->GetDisplayDefaultCapacity())},
			{"cargo", IsValidCargoType(default_cargo) ? static_cast<int>(default_cargo) : -1},
		});
	}
	return arr;
}

static nlohmann::json SctQueryIndustryTypes()
{
	/* Wave 14 — buildable industry types. Iterate every IndustrySpec slot
	 * (industry_type.h NUM_INDUSTRYTYPES) and emit only enabled specs (a NewGRF
	 * can disable slots). cost is spec->GetConstructionCost(); prospectable is
	 * cheaply read from spec->prospecting_chance (industrytype.h:107, non-zero
	 * means the deity-prospect path can place it). */
	nlohmann::json arr = nlohmann::json::array();
	if (!SctInGame()) return arr;

	for (IndustryType it = 0; it < NUM_INDUSTRYTYPES; it++) {
		const IndustrySpec *spec = GetIndustrySpec(it);
		if (spec == nullptr || !spec->enabled) continue;

		nlohmann::json entry = {
			{"id", static_cast<int>(it)},
			{"cost", static_cast<int64_t>(spec->GetConstructionCost())},
			{"prospectable", spec->prospecting_chance != 0},
		};
		if (spec->name != STR_NULL && spec->name != INVALID_STRING_ID) {
			entry["name"] = GetString(spec->name);
		}

		/* Wave 21 — cargo the type accepts / produces (industrytype.h: fixed-size
		 * accepts_cargo / produced_cargo arrays padded with INVALID_CARGO). */
		nlohmann::json accepts = nlohmann::json::array();
		for (CargoType c : spec->accepts_cargo) {
			if (IsValidCargoType(c)) accepts.push_back(static_cast<int>(c));
		}
		nlohmann::json produces = nlohmann::json::array();
		for (CargoType c : spec->produced_cargo) {
			if (IsValidCargoType(c)) produces.push_back(static_cast<int>(c));
		}
		entry["accepts"] = std::move(accepts);
		entry["produces"] = std::move(produces);

		arr.push_back(std::move(entry));
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

static nlohmann::json SctQueryCompanyEconomy()
{
	nlohmann::json arr = nlohmann::json::array();
	if (!SctInGame()) return arr;

	const Company *c = Company::GetIfValid(_local_company);
	if (c == nullptr) return arr;

	/* company_base.h: old_economy[0] is the most recent quarter; num_valid_stat_ent
	 * counts the filled entries. Emit oldest -> newest for a natural history chart. */
	const int n = std::min<int>(c->num_valid_stat_ent, static_cast<int>(c->old_economy.size()));
	for (int i = n - 1; i >= 0; i--) {
		const CompanyEconomyEntry &e = c->old_economy[i];
		arr.push_back({
			{"income", static_cast<int64_t>(e.income)},
			{"expenses", static_cast<int64_t>(e.expenses)},
			{"companyValue", static_cast<int64_t>(e.company_value)},
			{"deliveredCargo", static_cast<int64_t>(e.delivered_cargo.GetSum<uint64_t>())},
			{"performance", e.performance_history},
		});
	}
	return arr;
}

static nlohmann::json SctQueryInfrastructure()
{
	/* Wave 16 — local company infrastructure counts (company_base.h
	 * CompanyInfrastructure, a NOSAVE cache the engine keeps up to date). rail/road
	 * are the per-type track-bit totals summed across all rail/road types
	 * (GetRailTotal / GetRoadTotal); water/station/airport/signal are direct counts.
	 * `maintenance` mirrors the infra-maintenance accumulation in economy.cpp:657-669
	 * (the raw sum the engine subtracts while `economy.infrastructure_maintenance` is
	 * on); omitted when that setting is off (then nothing is charged). */
	if (!SctInGame()) return nullptr;

	const Company *c = Company::GetIfValid(_local_company);
	if (c == nullptr) return nullptr;

	const CompanyInfrastructure &inf = c->infrastructure;
	nlohmann::json j = {
		{"rail", inf.GetRailTotal()},
		{"road", inf.GetRoadTotal()},
		{"tram", inf.GetTramTotal()},
		{"water", inf.water},
		{"station", inf.station},
		{"airport", inf.airport},
		{"signal", inf.signal},
	};

	if (_settings_game.economy.infrastructure_maintenance) {
		Money cost = 0;
		const uint32_t rail_total = inf.GetRailTotal();
		for (RailType rt = RAILTYPE_BEGIN; rt < RAILTYPE_END; rt++) {
			if (inf.rail[rt] != 0) cost += RailMaintenanceCost(rt, inf.rail[rt], rail_total);
		}
		cost += SignalMaintenanceCost(inf.signal);
		const uint32_t road_total = inf.GetRoadTotal();
		const uint32_t tram_total = inf.GetTramTotal();
		for (RoadType rt = ROADTYPE_BEGIN; rt < ROADTYPE_END; rt++) {
			if (inf.road[rt] != 0) cost += RoadMaintenanceCost(rt, inf.road[rt], RoadTypeIsRoad(rt) ? road_total : tram_total);
		}
		cost += CanalMaintenanceCost(inf.water);
		cost += StationMaintenanceCost(inf.station);
		cost += AirportMaintenanceCost(c->index);
		j["maintenance"] = static_cast<int64_t>(cost);
	}

	return j;
}

/* ---- Write-bridge helpers (build / place / vehicle commands) ---- */

/** Default railtype for build commands when p1 == 0 (0 = auto-pick first available). */
static int g_sct_railtype = 0;
/** Default roadtype for build commands when p1 == 0 (0 = auto-pick first available). */
static int g_sct_roadtype = 0;
/**
 * Wave 21 — per-placement station-join stash consumed by the rail_station build.
 * -1 (default) = force a fresh station (NEW_STATION, adjacent=false); 0 = allow
 * adjacent placement (station_to_join = Invalid, adjacent=true); >0 = distant-join
 * that concrete StationID (adjacent=false). Reset to -1 after every station build.
 */
static int g_sct_station_join = -1;

static bool SctCanBuild()
{
	return SctInGame() && Map::IsInitialized() && Company::IsValidID(_local_company);
}

/**
 * Wave 22 — gate for landscape / town / industry edits, which are legal both in a
 * normal game (with a valid local company) AND in the scenario editor (GM_EDITOR,
 * where _local_company is OWNER_NONE so no real company exists). This is distinct
 * from SctCanBuild, which gates company-only construction (rail/road/stations/
 * vehicles) and still demands a valid company — those actions stay refused in the
 * editor exactly as before.
 */
static bool SctCanEditLandscape()
{
	if (!Map::IsInitialized()) return false;
	if (_game_mode == GM_EDITOR) return true;
	return _game_mode == GM_NORMAL && Company::IsValidID(_local_company);
}

/**
 * Wave 22 — acting company for a scenario-editor landscape / town / industry edit.
 *
 * The scenario-editor GUI dispatches every one of these tools as OWNER_NONE — its
 * local company in the editor. industry_gui.cpp:716 backs _current_company to
 * OWNER_NONE explicitly before CMD_BUILD_INDUSTRY; the terraform toolbar
 * (terraform_gui.cpp) and the found-town tool (town_gui.cpp) run under the same
 * OWNER_NONE local company that openttd.cpp sets with SetLocalCompany(OWNER_NONE)
 * when the editor starts.
 *
 * We deliberately use OWNER_NONE and NOT OWNER_DEITY: OWNER_DEITY is the
 * game-script / deity owner and takes different code paths (e.g. industry
 * random/prospect-creation instead of user-creation, and different town-rating and
 * cleared-land ownership handling). Matching the SE GUI means OWNER_NONE.
 *
 * In a normal game this simply returns _local_company, so every existing write path
 * keeps identical behaviour. (Note _local_company already equals OWNER_NONE in the
 * editor, so this is also what the old AutoRestoreBackup(_current_company,
 * _local_company) produced — but SctCanBuild refused to reach it there; the real fix
 * is the gate above.)
 */
static CompanyID SctActingCompany()
{
	return (_game_mode == GM_EDITOR) ? OWNER_NONE : _local_company;
}

static std::string SctErrorFromCost(const CommandCost &cost)
{
	if (cost.Succeeded()) return {};
	const StringID msg = cost.GetErrorMessage();
	if (msg != INVALID_STRING_ID) return GetString(msg);
	return "build failed";
}

static nlohmann::json SctCostResult(const CommandCost &cost)
{
	return {
		{"ok", cost.Succeeded()},
		{"error", SctErrorFromCost(cost)},
		{"cost", static_cast<int64_t>(cost.GetCost())},
	};
}

static nlohmann::json SctOkResult(const CommandCost &cost)
{
	return {
		{"ok", cost.Succeeded()},
		{"error", SctErrorFromCost(cost)},
	};
}

static RailType SctResolveRailType(int p1)
{
	int rt = (p1 != 0) ? p1 : g_sct_railtype;
	if (rt > 0 && rt < RAILTYPE_END) return static_cast<RailType>(rt);

	const Company *c = Company::GetIfValid(_local_company);
	if (c != nullptr) {
		for (RailType candidate = RAILTYPE_BEGIN; candidate < RAILTYPE_END; candidate++) {
			if (c->avail_railtypes.Test(candidate)) return candidate;
		}
	}
	return RAILTYPE_RAIL;
}

static RoadType SctResolveRoadType(int p1)
{
	int rt = (p1 != 0) ? p1 : g_sct_roadtype;
	if (rt > 0 && rt < ROADTYPE_END) return static_cast<RoadType>(rt);

	const Company *c = Company::GetIfValid(_local_company);
	if (c != nullptr) {
		for (RoadType candidate = ROADTYPE_BEGIN; candidate < ROADTYPE_END; candidate++) {
			if (c->avail_roadtypes.Test(candidate)) return candidate;
		}
	}
	return ROADTYPE_ROAD;
}

/**
 * Shared autorail drag resolution for preview, build, and removal.
 *
 * This is the tile-centre equivalent of viewport.cpp CalcRaildirsDrawstyle:
 * measure each delta as a span including one tile, keep shallow gestures on a
 * map axis, then trim a mixed gesture onto the nearest half-track line.
 * The React guide mirrors this calculation, so guide and command agree.
 */
struct SctRailDrag {
	TileIndex end;
	Track track;
	Track alternate;
};

static SctRailDrag SctResolveRailDrag(TileIndex start, TileIndex requested_end, int explicit_track)
{
	if (explicit_track >= TRACK_BEGIN && explicit_track < TRACK_END) {
		return {requested_end, static_cast<Track>(explicit_track), INVALID_TRACK};
	}

	const int ax = static_cast<int>(TileX(start));
	const int ay = static_cast<int>(TileY(start));
	const int bx = static_cast<int>(TileX(requested_end));
	const int by = static_cast<int>(TileY(requested_end));
	const int dx = bx - ax;
	const int dy = by - ay;
	const int adx = std::abs(dx);
	const int ady = std::abs(dy);
	const int max_x = static_cast<int>(Map::MaxX());
	const int max_y = static_cast<int>(Map::MaxY());

	if (dy == 0 || adx + 1 > 2 * (ady + 1)) {
		return {
			TileXY(static_cast<uint>(std::clamp(bx, 0, max_x)), static_cast<uint>(std::clamp(ay, 0, max_y))),
			TRACK_X,
			INVALID_TRACK,
		};
	}
	if (dx == 0 || ady + 1 > 2 * (adx + 1)) {
		return {
			TileXY(static_cast<uint>(std::clamp(ax, 0, max_x)), static_cast<uint>(std::clamp(by, 0, max_y))),
			TRACK_Y,
			INVALID_TRACK,
		};
	}

	const int sx = dx >= 0 ? 1 : -1;
	const int sy = dy >= 0 ? 1 : -1;
	const int end_x = adx > ady ? ax + (ady + 1) * sx : bx;
	const int end_y = ady > adx ? ay + (adx + 1) * sy : by;
	const TileIndex end = TileXY(
			static_cast<uint>(std::clamp(end_x, 0, max_x)),
			static_cast<uint>(std::clamp(end_y, 0, max_y)));

	/* Balanced drags use sub-tile cursor position natively. The web gesture
	 * starts from tile centres, so choose the centre result and retry sibling. */
	if (adx == ady) {
		if (sx > 0 && sy > 0) return {end, TRACK_RIGHT, TRACK_LEFT};
		if (sx > 0 && sy < 0) return {end, TRACK_LOWER, TRACK_UPPER};
		if (sx < 0 && sy > 0) return {end, TRACK_LOWER, TRACK_UPPER};
		return {end, TRACK_RIGHT, TRACK_LEFT};
	}

	if (sx > 0 && sy > 0) return {end, adx > ady ? TRACK_LEFT : TRACK_RIGHT, INVALID_TRACK};
	if (sx > 0 && sy < 0) return {end, adx > ady ? TRACK_LOWER : TRACK_UPPER, INVALID_TRACK};
	if (sx < 0 && sy > 0) return {end, adx > ady ? TRACK_UPPER : TRACK_LOWER, INVALID_TRACK};
	return {end, adx > ady ? TRACK_RIGHT : TRACK_LEFT, INVALID_TRACK};
}

static DiagDirection SctResolveDiagDir(int p2)
{
	if (p2 >= DIAGDIR_BEGIN && p2 < DIAGDIR_END) return static_cast<DiagDirection>(p2);
	return DIAGDIR_NE;
}

/**
 * Auto-pick rail depot exit direction toward adjacent track (Wave 5, dir=255).
 * Scans DiagDirections NE/SE/SW/NW; faces the first plain-rail or rail-station neighbour.
 * Falls back to DIAGDIR_NE when none found. Skips invalid/map-edge neighbours.
 */
static DiagDirection SctAutoOrientRailDepot(TileIndex tile)
{
	for (DiagDirection d = DIAGDIR_BEGIN; d < DIAGDIR_END; d++) {
		const TileIndex n = TileAddByDiagDir(tile, d);
		if (!IsValidTile(n)) continue;
		if (IsPlainRailTile(n) || IsRailStationTile(n)) return d;
	}
	return DIAGDIR_NE;
}

/**
 * Auto-pick road depot exit direction toward adjacent road (Wave 5, dir=255).
 * Scans DiagDirections NE/SE/SW/NW; faces the first normal road neighbour.
 * Falls back to DIAGDIR_NE when none found. Skips invalid/map-edge neighbours.
 */
static DiagDirection SctAutoOrientRoadDepot(TileIndex tile)
{
	for (DiagDirection d = DIAGDIR_BEGIN; d < DIAGDIR_END; d++) {
		const TileIndex n = TileAddByDiagDir(tile, d);
		if (!IsValidTile(n)) continue;
		if (IsNormalRoadTile(n)) return d;
	}
	return DIAGDIR_NE;
}

static Axis SctResolveAxis(int p1)
{
	return (p1 == 1) ? AXIS_Y : AXIS_X;
}

static uint8_t SctClampStationDim(int value, uint8_t fallback)
{
	if (value < 1 || value > 7) return fallback;
	return static_cast<uint8_t>(value);
}

/**
 * Wave 6 — auto-flatten a width×height footprint at origin before build.
 * Levels the axis-aligned rectangle origin..end via CMD_LEVEL_LAND (same
 * pattern as terrain_level). Result is ignored (already flat / no funds).
 * End tile is clamped onto the map so the level never walks off-map.
 */
static void SctLevelFootprint(TileIndex origin, int width, int height)
{
	if (!IsValidTile(origin) || width < 1 || height < 1) return;

	const int ox = static_cast<int>(TileX(origin));
	const int oy = static_cast<int>(TileY(origin));
	const int max_x = static_cast<int>(Map::MaxX());
	const int max_y = static_cast<int>(Map::MaxY());
	const int end_x = std::min(ox + width - 1, max_x);
	const int end_y = std::min(oy + height - 1, max_y);
	const TileIndex end_tile = TileXY(static_cast<uint>(end_x), static_cast<uint>(end_y));

	/* terraform_cmd.h:18 CmdLevelLand(end, start, diagonal, LM_LEVEL) — ignore result. */
	(void)Command<CMD_LEVEL_LAND>::Do(DoCommandFlag::Execute, end_tile, origin, false, LM_LEVEL);
}

/* ---- Wave 18: network content service (BaNaNaS) ---- */

/** Map a ContentType (tcp_content_type.h) to a short lower-snake string for the UI. */
static const char *SctContentTypeStr(ContentType t)
{
	switch (t) {
		case CONTENT_TYPE_BASE_GRAPHICS: return "base_graphics";
		case CONTENT_TYPE_NEWGRF:        return "newgrf";
		case CONTENT_TYPE_AI:            return "ai";
		case CONTENT_TYPE_AI_LIBRARY:    return "library";
		case CONTENT_TYPE_SCENARIO:      return "scenario";
		case CONTENT_TYPE_HEIGHTMAP:     return "heightmap";
		case CONTENT_TYPE_BASE_SOUNDS:   return "base_sounds";
		case CONTENT_TYPE_BASE_MUSIC:    return "base_music";
		case CONTENT_TYPE_GAME:          return "game_script";
		case CONTENT_TYPE_GAME_LIBRARY:  return "library";
		default:                         return "unknown";
	}
}

/** Map a ContentInfo::State (tcp_content_type.h) to a short lower-snake string. */
static const char *SctContentStateStr(ContentInfo::State s)
{
	switch (s) {
		case ContentInfo::State::Unselected:   return "unselected";
		case ContentInfo::State::Selected:     return "selected";
		case ContentInfo::State::Autoselected: return "autoselected";
		case ContentInfo::State::AlreadyHere:  return "already_here";
		case ContentInfo::State::DoesNotExist: return "does_not_exist";
		case ContentInfo::State::Invalid:      return "invalid";
		default:                               return "invalid";
	}
}

/**
 * Wave 18 — the network content client's known items (BaNaNaS). Iterates
 * `_network_content_client.Info()` (network_content.h — a read-only view of the
 * received ContentInfo list). Returns [] before `content update` has fetched a
 * list (the infos vector is empty then). Independent of game mode — content can
 * be browsed from the menu.
 */
static nlohmann::json SctQueryContentList()
{
	nlohmann::json arr = nlohmann::json::array();
	for (const ContentInfo &ci : _network_content_client.Info()) {
		nlohmann::json entry = {
			{"id", static_cast<int64_t>(ci.id)},
			{"type", SctContentTypeStr(ci.type)},
			{"name", ci.name},
			{"filesize", static_cast<int64_t>(ci.filesize)},
			{"state", SctContentStateStr(ci.state)},
		};
		if (!ci.version.empty()) entry["version"] = ci.version;
		if (!ci.url.empty()) entry["url"] = ci.url;
		if (!ci.description.empty()) entry["description"] = ci.description;
		arr.push_back(std::move(entry));
	}
	return arr;
}

/**
 * Wave 18 — network reachability snapshot for the UI. `contentConnected` is
 * whether the content client currently holds an open socket
 * (NetworkTCPSocketHandler::IsConnected, tcp.h:45). The game coordinator / server
 * listing is not surfaced: multiplayer server browsing is not built in this fork
 * and the coordinator client's game list is not cheaply/reliably reachable over
 * the emscripten WebSocket proxy, so `serverList` is always [] with an explanatory
 * `note` (see the Wave 18 contract section). No server browsing is implemented.
 */
static nlohmann::json SctQueryNetworkStatus()
{
	return {
		{"contentConnected", _network_content_client.IsConnected()},
		{"serverList", nlohmann::json::array()},
		{"note", "coordinator unavailable: server browsing not built in this emscripten fork"},
	};
}

/**
 * Wave 21 — every company in the pool (real + AI). Drives the league table and
 * buy-company windows. Fields are the cheap cached ones plus a live company value.
 * `performance` is the same score the league window sorts on
 * (old_economy[0].performance_history, 0-1000). `vehicles` sums the ALL_GROUP
 * per-type caches (company_base.h group_all[], NOSAVE). `value` is a live
 * CalculateCompanyValue(c) (company_base.h:198) — O(assets) per company, fine for
 * the handful of companies that ever exist. money/loan are doubles so JS keeps
 * full magnitude without BigInt.
 */
static nlohmann::json SctQueryCompanies()
{
	nlohmann::json arr = nlohmann::json::array();
	if (!SctInGame()) return arr;

	for (const Company *c : Company::Iterate()) {
		std::string name = c->name.empty()
				? fmt::format("Company {}", c->index.base() + 1)
				: c->name;

		uint32_t vehicles = 0;
		for (VehicleType vt = VEH_BEGIN; vt < VEH_COMPANY_END; vt++) {
			vehicles += c->group_all[vt].num_vehicle;
		}

		arr.push_back({
			{"id", c->index.base()},
			{"name", std::move(name)},
			{"money", static_cast<double>(c->money)},
			{"loan", static_cast<double>(c->current_loan)},
			{"value", static_cast<double>(CalculateCompanyValue(c))},
			{"isAI", c->is_ai},
			{"vehicles", vehicles},
			{"performance", c->old_economy[0].performance_history},
		});
	}
	return arr;
}

/** True if `list` already holds a GRFConfig matching grfid (and md5 when non-null). */
static bool SctGrfInList(const GRFConfigList &list, uint32_t grfid, const MD5Hash *md5)
{
	for (const auto &c : list) {
		if (c->ident.HasGrfIdentifier(grfid, md5)) return true;
	}
	return false;
}

/**
 * Wave 21 — every NewGRF the engine has scanned (newgrf_config.h _all_grfs,
 * populated by ScanNewGRFFiles). grfid is the canonical 8-hex-char display form
 * (std::byteswap of the stored little-endian grfid, exactly as console_cmds.cpp
 * prints it); md5 is the uppercase 32-char hex of the file checksum. `selected`
 * flags whether a matching entry is currently in _grfconfig_newgame (so new games
 * would start with it). If no scan has run yet this session (_all_grfs empty),
 * request one the way the GUI/console does (RequestNewGRFScan runs on the next
 * game-tick, openttd.cpp:1327) and return [] for the caller to poll.
 */
static nlohmann::json SctQueryNewgrfAvailable()
{
	nlohmann::json arr = nlohmann::json::array();

	if (_all_grfs.empty()) {
		RequestNewGRFScan();
		return arr;
	}

	for (const auto &c : _all_grfs) {
		arr.push_back({
			{"grfid", fmt::format("{:08X}", std::byteswap(c->ident.grfid))},
			{"md5", FormatArrayAsHex(c->ident.md5sum)},
			{"filename", c->filename},
			{"name", c->GetName()},
			{"status", static_cast<int>(c->status)},
			{"selected", SctGrfInList(_grfconfig_newgame, c->ident.grfid, &c->ident.md5sum)},
		});
	}
	return arr;
}

/**
 * Wave 26 — resolve a subsidy Source (industry / town) to a display name. Company
 * headquarters sources never appear in the subsidy list, so they are labelled
 * generically. Mirrors subsidy.cpp Source::GetFormat (Industry/Town names).
 */
static std::string SctSourceName(const Source &src)
{
	switch (src.type) {
		case SourceType::Industry: {
			const Industry *i = Industry::GetIfValid(src.ToIndustryID());
			return (i != nullptr) ? i->GetCachedName() : std::string{};
		}
		case SourceType::Town: {
			const Town *t = Town::GetIfValid(src.ToTownID());
			return (t != nullptr) ? t->GetCachedName() : std::string{};
		}
		default:
			return {};
	}
}

/**
 * Wave 26 — offered / awarded subsidies (subsidy_base.h Subsidy pool). srcType /
 * dstType map SourceType (0 industry, 1 town, 2 headquarters); awardedTo is the
 * company index the subsidy is awarded to, or -1 when still merely offered.
 * monthsLeft is the `remaining` month counter (offer window while unawarded, the
 * subsidised duration once awarded).
 */
static nlohmann::json SctQuerySubsidies()
{
	nlohmann::json arr = nlohmann::json::array();
	if (!SctInGame()) return arr;

	for (const Subsidy *s : Subsidy::Iterate()) {
		arr.push_back({
			{"id", s->index.base()},
			{"cargo", IsValidCargoType(s->cargo_type) ? static_cast<int>(s->cargo_type) : -1},
			{"srcType", static_cast<int>(s->src.type)},
			{"srcId", static_cast<int>(s->src.id)},
			{"srcName", SctSourceName(s->src)},
			{"dstType", static_cast<int>(s->dst.type)},
			{"dstId", static_cast<int>(s->dst.id)},
			{"dstName", SctSourceName(s->dst)},
			{"awardedTo", s->IsAwarded() ? static_cast<int>(s->awarded.base()) : -1},
			{"monthsLeft", s->remaining},
		});
	}
	return arr;
}

/**
 * Wave 26 — cargo payment-rate curves for the payments graph. Replicates the exact
 * call graph_gui.cpp PaymentRatesGraphWindow::UpdatePaymentRates makes:
 * GetTransportedGoodsIncome(10, 20, j*4+4, cargo) for j in 0..GRAPH_PAYMENT_RATE_STEPS-1
 * (20 steps). `days` is the transit-periods x value (j*4+4); `income` the money for
 * that leg. `color` is the cargo's legend palette index (cargotype.h legend_colour).
 */
static nlohmann::json SctQueryCargoPaymentRates()
{
	nlohmann::json arr = nlohmann::json::array();
	if (!SctInGame()) return arr;

	static const int GRAPH_PAYMENT_RATE_STEPS = 20;

	for (const CargoSpec *cs : CargoSpec::Iterate()) {
		if (!cs->IsValid()) continue;

		nlohmann::json points = nlohmann::json::array();
		for (int j = 0; j < GRAPH_PAYMENT_RATE_STEPS; j++) {
			const uint16_t periods = static_cast<uint16_t>(j * 4 + 4);
			points.push_back({
				{"days", static_cast<int>(periods)},
				{"income", static_cast<int64_t>(GetTransportedGoodsIncome(10, 20, periods, cs->Index()))},
			});
		}

		arr.push_back({
			{"cargo", static_cast<int>(cs->Index())},
			{"label", GetString(cs->name)},
			{"color", static_cast<int>(cs->legend_colour.p)},
			{"points", std::move(points)},
		});
	}
	return arr;
}

/** Wave 26 — flatten a ScriptInfoList (AI / Game) to [{name, version}] rows. */
static nlohmann::json SctScriptInfoList(const ScriptInfoList *list)
{
	nlohmann::json arr = nlohmann::json::array();
	if (list == nullptr) return arr;

	for (const auto &item : *list) {
		const ScriptInfo *info = item.second;
		if (info == nullptr) continue;
		arr.push_back({
			{"name", info->GetName()},
			{"version", info->GetVersion()},
		});
	}
	return arr;
}

/** Wave 26 — installed AIs (best version per unique AI), mirroring `list_ai`. */
static nlohmann::json SctQueryAiList()
{
	return SctScriptInfoList(AI::GetUniqueInfoList());
}

/** Wave 26 — installed Game Scripts (best version per unique GS), like `list_game`. */
static nlohmann::json SctQueryGsList()
{
	return SctScriptInfoList(Game::GetUniqueInfoList());
}

/**
 * Wave 29 — resolve the canonical 8-hex-char display grfid (the form
 * query('newgrfAvailable') emits, std::byteswap of the stored little-endian grfid) to
 * its entry in the new-game config (_grfconfig_newgame). Parses the id exactly the way
 * sct_newgrf_select does. Returns nullptr when the GRF is not currently selected.
 */
static GRFConfig *SctFindNewgameGrf(const char *grfid_hex)
{
	if (grfid_hex == nullptr || grfid_hex[0] == '\0') return nullptr;
	const uint32_t display_grfid = static_cast<uint32_t>(std::strtoul(grfid_hex, nullptr, 16));
	const uint32_t grfid = std::byteswap(display_grfid);
	for (const auto &c : _grfconfig_newgame) {
		if (c->ident.grfid == grfid) return c.get();
	}
	return nullptr;
}

/**
 * Wave 29 — connected clients in a network game (network_base.h NetworkClientInfo pool).
 * `company` is the company the client plays as, or -1 for spectators (COMPANY_SPECTATOR).
 * Empty array when not networking.
 */
static nlohmann::json SctQueryNetworkClients()
{
	nlohmann::json arr = nlohmann::json::array();
	if (!_networking) return arr;

	for (const NetworkClientInfo *ci : NetworkClientInfo::Iterate()) {
		const bool spectator = (ci->client_playas == COMPANY_SPECTATOR);
		arr.push_back({
			{"id", static_cast<int>(ci->client_id)},
			{"name", ci->client_name},
			{"company", spectator ? -1 : static_cast<int>(ci->client_playas.base())},
		});
	}
	return arr;
}

/**
 * Wave 29 — per-cargo link-graph edges for cargo-distribution overlays. Iterates every
 * LinkGraph (linkgraph.h LinkGraph::Iterate()); for each graph its cargo, for each node
 * its outgoing edges. from/to are station ids; fromTile/toTile are the stations' tiles
 * (Station::GetIfValid → xy, falling back to the node's cached xy). capacity/usage come
 * straight off the edge; "planned" flows live in per-station FlowStats (not cheaply
 * available here), so planned = usage as documented. Self-edges and invalid destinations
 * are skipped. Total edges are capped at 600 (simple iteration order, not sorted).
 */
static nlohmann::json SctQueryLinkFlows()
{
	nlohmann::json arr = nlohmann::json::array();
	if (!SctInGame()) return arr;

	constexpr int MAX_EDGES = 600; // cap total emitted edges; simple (unsorted) order
	int emitted = 0;

	for (const LinkGraph *lg : LinkGraph::Iterate()) {
		const CargoType cargo = lg->Cargo();
		const int cargo_json = IsValidCargoType(cargo) ? static_cast<int>(cargo) : -1;

		for (NodeID from = 0; from < lg->Size(); from++) {
			const LinkGraph::BaseNode &fnode = (*lg)[from];
			const Station *fst = Station::GetIfValid(fnode.station);
			const TileIndex ftile = (fst != nullptr) ? fst->xy : fnode.xy;

			for (const LinkGraph::BaseEdge &edge : fnode.edges) {
				if (emitted >= MAX_EDGES) return arr;

				const NodeID to = edge.dest_node;
				if (to == INVALID_NODE || to == from || to >= lg->Size()) continue;

				const LinkGraph::BaseNode &tnode = (*lg)[to];
				const Station *tst = Station::GetIfValid(tnode.station);
				const TileIndex ttile = (tst != nullptr) ? tst->xy : tnode.xy;

				arr.push_back({
					{"cargo", cargo_json},
					{"from", static_cast<int>(fnode.station.base())},
					{"to", static_cast<int>(tnode.station.base())},
					{"fromTile", ftile.base()},
					{"toTile", ttile.base()},
					{"capacity", static_cast<int64_t>(edge.capacity)},
					{"usage", static_cast<int64_t>(edge.usage)},
					{"planned", static_cast<int64_t>(edge.usage)},
				});
				emitted++;
			}
		}
	}
	return arr;
}

} // namespace

/**
 * Query entity data for the external UI.
 * @param kind Null-terminated query kind (towns, industries, stations, vehicles, groups,
 *             financeDetail, engines, news, cargos, companyEconomy, industryTypes,
 *             infrastructure, contentList, networkStatus, companies, newgrfAvailable).
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
	} else if (std::strcmp(kind, "cargos") == 0) {
		j = SctQueryCargos();
	} else if (std::strcmp(kind, "companyEconomy") == 0) {
		j = SctQueryCompanyEconomy();
	} else if (std::strcmp(kind, "industryTypes") == 0) {
		j = SctQueryIndustryTypes();
	} else if (std::strcmp(kind, "infrastructure") == 0) {
		j = SctQueryInfrastructure();
	} else if (std::strcmp(kind, "contentList") == 0) {
		j = SctQueryContentList();
	} else if (std::strcmp(kind, "networkStatus") == 0) {
		j = SctQueryNetworkStatus();
	} else if (std::strcmp(kind, "companies") == 0) {
		j = SctQueryCompanies();
	} else if (std::strcmp(kind, "newgrfAvailable") == 0) {
		j = SctQueryNewgrfAvailable();
	} else if (std::strcmp(kind, "subsidies") == 0) {
		j = SctQuerySubsidies();
	} else if (std::strcmp(kind, "cargo_payment_rates") == 0) {
		j = SctQueryCargoPaymentRates();
	} else if (std::strcmp(kind, "ai_list") == 0) {
		j = SctQueryAiList();
	} else if (std::strcmp(kind, "gs_list") == 0) {
		j = SctQueryGsList();
	} else if (std::strcmp(kind, "network_clients") == 0) {
		j = SctQueryNetworkClients();
	} else if (std::strcmp(kind, "link_flows") == 0) {
		j = SctQueryLinkFlows();
	} else {
		j = {{"error", "unknown kind"}};
	}

	buffer = j.dump();
	return buffer.c_str();
}

/**
 * Resolve a canvas pixel to a world tile via the main viewport.
 * @return JSON `{"tile":idx,"x":TileX,"y":TileY}` or the literal JSON `null`.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_tile_at_screen(int px, int py)
{
	static std::string buffer;

	if (!SctInGame() || !Map::IsInitialized()) {
		buffer = "null";
		return buffer.c_str();
	}

	Window *w = GetMainWindow();
	if (w == nullptr || w->viewport == nullptr) {
		buffer = "null";
		return buffer.c_str();
	}

	Point pt = TranslateXYToTileCoord(*w->viewport, px, py, true);
	if (pt.x == -1 && pt.y == -1) {
		buffer = "null";
		return buffer.c_str();
	}

	TileIndex tile = TileVirtXY(pt.x, pt.y);
	if (!IsValidTile(tile)) {
		buffer = "null";
		return buffer.c_str();
	}

	nlohmann::json j = {
		{"tile", tile.base()},
		{"x", TileX(tile)},
		{"y", TileY(tile)},
	};
	buffer = j.dump();
	return buffer.c_str();
}

/**
 * Map a tile to the 4 screen-pixel corners of its top face (placement highlight).
 * Forward inverse of TranslateXYToTileCoord (viewport.cpp:430); same canvas transform
 * as GetViewportStationMiddle (viewport.cpp:3585–3586).
 * @param tile TileIndex base.
 * @return JSON `[{"x":sx,"y":sy},...]` (4 corners) or the literal JSON `null`.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_tile_poly(int tile)
{
	static std::string buffer;

	if (!SctInGame() || !Map::IsInitialized()) {
		buffer = "null";
		return buffer.c_str();
	}

	if (tile < 0 || static_cast<uint>(tile) >= Map::Size()) {
		buffer = "null";
		return buffer.c_str();
	}

	Window *w = GetMainWindow();
	if (w == nullptr || w->viewport == nullptr) {
		buffer = "null";
		return buffer.c_str();
	}

	const Viewport &vp = *w->viewport;
	const TileIndex t{static_cast<uint32_t>(tile)};
	const int tx = static_cast<int>(TileX(t)) * TILE_SIZE;
	const int ty = static_cast<int>(TileY(t)) * TILE_SIZE;
	/* Flat top-face height for v1 (northern corner); fine for placement highlight. */
	const int z = static_cast<int>(TilePixelHeight(t));

	/* World corners of the tile top face, NW → NE → SE → SW in tile XY. */
	const int ts = static_cast<int>(TILE_SIZE);
	const int corners_x[4] = { tx, tx + ts, tx + ts, tx };
	const int corners_y[4] = { ty, ty, ty + ts, ty + ts };

	nlohmann::json arr = nlohmann::json::array();
	for (int i = 0; i < 4; i++) {
		Point v = RemapCoords(corners_x[i], corners_y[i], z);
		/* Reverse of ScaleByZoom(sx - vp.left, zoom) + vp.virtual_left (viewport.cpp:438). */
		const int sx = UnScaleByZoom(v.x - vp.virtual_left, vp.zoom) + vp.left;
		const int sy = UnScaleByZoom(v.y - vp.virtual_top, vp.zoom) + vp.top;
		arr.push_back({{"x", sx}, {"y", sy}});
	}

	buffer = arr.dump();
	return buffer.c_str();
}

/**
 * Show / hide the engine's own viewport name signs.
 *
 * Every viewport sign is gated by _display_opt (transparency.h) and read in one
 * place, ViewportAddKdtreeSigns (viewport.cpp); clearing the bits stops the
 * engine drawing them without touching the draw path. The React label layer
 * (sct_labels below) renders its own plates instead.
 *
 * @param mask Bit 0 towns, 1 stations, 2 waypoints, 3 signs. 0 = hide all.
 */
void EMSCRIPTEN_KEEPALIVE sct_set_labels(int mask)
{
	auto apply = [&](int bit, uint8_t opt) {
		if (HasBit(static_cast<uint>(mask), bit)) {
			SetBit(_display_opt, opt);
		} else {
			ClrBit(_display_opt, opt);
		}
	};

	apply(0, DO_SHOW_TOWN_NAMES);
	apply(1, DO_SHOW_STATION_NAMES);
	apply(2, DO_SHOW_WAYPOINT_NAMES);
	apply(3, DO_SHOW_SIGNS);

	MarkWholeScreenDirty();
}

/**
 * List the name labels visible in the main viewport, in canvas pixels.
 *
 * Mirrors ViewportAddKdtreeSigns (viewport.cpp) — same kdtree query, same
 * facility / competitor filtering — but ignores the four name bits of
 * _display_opt so the React layer keeps working while the native signs are
 * hidden via sct_set_labels(0). Screen mapping is the transform used by
 * sct_tile_poly above.
 *
 * @return JSON `{"vp":{"left","top","zoom","virtualLeft","virtualTop"},
 *          "labels":[{"type","id","name","x","y","w","owner"}]}`, or `null`.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_labels()
{
	static std::string buffer;

	if (!SctInGame() || !Map::IsInitialized()) {
		buffer = "null";
		return buffer.c_str();
	}

	Window *w = GetMainWindow();
	if (w == nullptr || w->viewport == nullptr) {
		buffer = "null";
		return buffer.c_str();
	}

	const Viewport &vp = *w->viewport;
	const bool small = vp.zoom >= ZoomLevel::Out4x;

	/* Search the sign kdtree over the visible virtual rect, expanded so signs
	 * whose anchor sits just off-screen but whose text overlaps still appear.
	 * (ExpandRectWithViewportSignMargins is static in viewport.cpp; the margins
	 * here are a generous fixed equivalent — the kdtree does the real work.) */
	const int expand_x = ScaleByZoom(192, vp.zoom);
	const int expand_y = ScaleByZoom(32, vp.zoom);
	const int left = vp.virtual_left - expand_x;
	const int top = vp.virtual_top - expand_y;
	const int right = vp.virtual_left + vp.virtual_width + expand_x;
	const int bottom = vp.virtual_top + vp.virtual_height + expand_y;

	const bool show_competitors = HasBit(_display_opt, DO_SHOW_COMPETITOR_SIGNS);

	nlohmann::json labels = nlohmann::json::array();

	auto emit = [&](const char *type, uint32_t id, const std::string &name, const ViewportSign &sign, Owner owner) {
		labels.push_back({
			{"type", type},
			{"id", id},
			{"name", name},
			{"x", UnScaleByZoom(sign.center - vp.virtual_left, vp.zoom) + vp.left},
			{"y", UnScaleByZoom(sign.top - vp.virtual_top, vp.zoom) + vp.top},
			{"w", small ? sign.width_small : sign.width_normal},
			{"owner", owner.base()},
		});
	};

	_viewport_sign_kdtree.FindContained(left, top, right, bottom, [&](const ViewportSignKdtreeItem &item) {
		switch (item.type) {
			case ViewportSignKdtreeItem::VKI_STATION:
			case ViewportSignKdtreeItem::VKI_WAYPOINT: {
				const BaseStation *st = BaseStation::Get(std::get<StationID>(item.id));
				const bool is_waypoint = (item.type == ViewportSignKdtreeItem::VKI_WAYPOINT);

				if (!is_waypoint) {
					/* No facilities at all means a ghost station. */
					StationFacilities facilities = st->facilities;
					if (facilities.None()) facilities = STATION_FACILITY_GHOST;
					if (!facilities.Any(_facility_display_opt)) break;
				}

				/* Competitor-owned names follow the same toggle as the engine;
				 * OWNER_NONE stations are never hidden. */
				if (!show_competitors && _local_company != st->owner && st->owner != OWNER_NONE) break;

				emit(is_waypoint ? "waypoint" : "station", std::get<StationID>(item.id).base(),
						st->GetCachedName(), st->sign, st->owner);
				break;
			}

			case ViewportSignKdtreeItem::VKI_TOWN: {
				const Town *t = Town::Get(std::get<TownID>(item.id));
				emit("town", std::get<TownID>(item.id).base(), t->GetCachedName(), t->cache.sign, OWNER_NONE);
				break;
			}

			case ViewportSignKdtreeItem::VKI_SIGN: {
				const Sign *si = Sign::Get(std::get<SignID>(item.id));
				/* Matches the engine: competitor signs (incl. OWNER_NONE leftovers
				 * from bankrupt companies) hide with the same toggle. */
				if (!show_competitors && _local_company != si->owner && si->owner != OWNER_DEITY) break;
				emit("sign", std::get<SignID>(item.id).base(), si->name, si->sign, si->owner);
				break;
			}

			default:
				break;
		}
	});

	nlohmann::json j = {
		{"vp", {
			{"left", vp.left},
			{"top", vp.top},
			{"width", vp.width},
			{"height", vp.height},
			{"zoom", static_cast<int>(vp.zoom)},
			{"virtualLeft", vp.virtual_left},
			{"virtualTop", vp.virtual_top},
		}},
		{"labels", labels},
	};

	buffer = j.dump();
	return buffer.c_str();
}

/**
 * Dispatch a map build / landscape action for the local company.
 * @param action Action name from the sct command contract.
 * @param a Primary tile (TileIndex base) / start of range.
 * @param b Secondary tile / end of range.
 * @param p1 Action-specific param (railtype, axis, roadtype, airport type, …).
 * @param p2 Action-specific param (track, direction, station packing, layout, …).
 * @return JSON `{"ok":bool,"error":"...","cost":n}`.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_build(const char *action, int a, int b, int p1, int p2)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (action == nullptr) {
		return dump({{"ok", false}, {"error", "unknown action"}, {"cost", 0}});
	}

	/* Wave 22 — terraform / clear are landscape edits the scenario editor legitimately
	 * performs, so they use the editor-aware gate (works in GM_EDITOR too). Every other
	 * action here is company construction and keeps the SctCanBuild (valid-company)
	 * gate, so it still refuses in the editor exactly as before. */
	const bool is_landscape_edit =
			std::strcmp(action, "terrain_up") == 0 ||
			std::strcmp(action, "terrain_down") == 0 ||
			std::strcmp(action, "terrain_level") == 0 ||
			std::strcmp(action, "demolish") == 0;

	if (is_landscape_edit ? !SctCanEditLandscape() : !SctCanBuild()) {
		return dump({{"ok", false}, {"error", "not in game"}, {"cost", 0}});
	}

	/* In a normal game SctActingCompany() == _local_company (identical to the old
	 * behaviour for every company action); in the editor it is OWNER_NONE, matching
	 * the SE terraform / clear tools. */
	AutoRestoreBackup backup(_current_company, SctActingCompany());

	const TileIndex tile_a{static_cast<uint32_t>(a)};
	const TileIndex tile_b{static_cast<uint32_t>(b)};

	if (std::strcmp(action, "rail_track") == 0) {
		/* Autorail build. Terrain stays authoritative: unlike the old bridge,
		 * this never silently levels land before construction. The preview
		 * therefore reports the same slope/obstacle result the commit will use. */
		const RailType rt = SctResolveRailType(p1);
		const SctRailDrag drag = SctResolveRailDrag(tile_a, tile_b, p2);

		/* Off-map guard: start/end must be on the map (tile_map.h IsValidTile). */
		if (!IsValidTile(tile_a) || !IsValidTile(drag.end)) {
			return dump({{"ok", false}, {"error", "invalid tile"}, {"cost", 0}});
		}

		CommandCost cost = Command<CMD_BUILD_RAILROAD_TRACK>::Do(
				DoCommandFlag::Execute, drag.end, tile_a, rt, drag.track, true, false);
		if (cost.Failed() && drag.alternate != INVALID_TRACK) {
			cost = Command<CMD_BUILD_RAILROAD_TRACK>::Do(
					DoCommandFlag::Execute, drag.end, tile_a, rt, drag.alternate, true, false);
		}
		return dump(SctCostResult(cost));
	}

	if (std::strcmp(action, "remove_rail") == 0) {
		const SctRailDrag drag = SctResolveRailDrag(tile_a, tile_b, p2);
		if (!IsValidTile(tile_a) || !IsValidTile(drag.end)) {
			return dump({{"ok", false}, {"error", "invalid tile"}, {"cost", 0}});
		}
		CommandCost cost = Command<CMD_REMOVE_RAILROAD_TRACK>::Do(
				DoCommandFlag::Execute, drag.end, tile_a, drag.track);
		if (cost.Failed() && drag.alternate != INVALID_TRACK) {
			cost = Command<CMD_REMOVE_RAILROAD_TRACK>::Do(
					DoCommandFlag::Execute, drag.end, tile_a, drag.alternate);
		}
		return dump(SctCostResult(cost));
	}

	if (std::strcmp(action, "rail_station") == 0) {
		/* station_cmd.h:28 CmdBuildRailStation */
		const RailType rt = SctResolveRailType(0); /* use stash / first available; p1 is axis */
		const Axis axis = SctResolveAxis(p1);
		const uint8_t numtracks = SctClampStationDim((p2 >> 8) & 0xFF, 1);
		const uint8_t plat_len = SctClampStationDim(p2 & 0xFF, 3);
		/* Wave 6: flatten footprint first. AXIS_X → w=platlen, h=numtracks; AXIS_Y swaps
		 * (matches station_cmd.cpp:1454 and React BuildCaptureLayer footprint). */
		const int w = (axis == AXIS_X) ? static_cast<int>(plat_len) : static_cast<int>(numtracks);
		const int h = (axis == AXIS_X) ? static_cast<int>(numtracks) : static_cast<int>(plat_len);
		SctLevelFootprint(tile_a, w, h);

		/* Wave 21 — per-placement join semantics from the stashed stationJoin param
		 * (station_cmd.cpp:1465 reuse/distant_join logic; rail_gui.cpp:217-223 GUI
		 * path). -1 = fresh station (NEW_STATION, adjacent=false); 0 = adjacent
		 * placement allowed (Invalid + adjacent=true); >0 = distant-join that
		 * StationID (adjacent=false, needs station.distant_join_stations enabled).
		 * The stash is consumed here and reset to -1 so it never leaks to the next
		 * placement. */
		StationID station_to_join;
		bool adjacent;
		if (g_sct_station_join < 0) {
			station_to_join = NEW_STATION;
			adjacent = false;
		} else if (g_sct_station_join == 0) {
			station_to_join = StationID::Invalid();
			adjacent = true;
		} else {
			station_to_join = StationID{static_cast<uint16_t>(g_sct_station_join)};
			adjacent = false;
		}
		g_sct_station_join = -1;

		CommandCost cost = Command<CMD_BUILD_RAIL_STATION>::Do(
				DoCommandFlag::Execute, tile_a, rt, axis, numtracks, plat_len,
				STAT_CLASS_DFLT, 0, station_to_join, adjacent);
		return dump(SctCostResult(cost));
	}

	if (std::strcmp(action, "rail_depot") == 0) {
		/* rail_cmd.h:23 CmdBuildTrainDepot; p2==255 → auto-orient toward adjacent rail */
		const RailType rt = SctResolveRailType(p1);
		const DiagDirection dir = (p2 == 255)
				? SctAutoOrientRailDepot(tile_a)
				: SctResolveDiagDir(p2);
		SctLevelFootprint(tile_a, 1, 1); /* Wave 6: single-tile flatten */
		CommandCost cost = Command<CMD_BUILD_TRAIN_DEPOT>::Do(
				DoCommandFlag::Execute, tile_a, rt, dir);
		return dump(SctCostResult(cost));
	}

	if (std::strcmp(action, "signal") == 0) {
		/* rail_cmd.h:24 CmdBuildSingleSignal(tile, track, sigtype, sigvar, convert, skip, ctrl, cycle_start, cycle_stop, num_dir_cycle, signals_copy) */
		const Track track = (p1 >= TRACK_BEGIN && p1 < TRACK_END) ? static_cast<Track>(p1) : TRACK_X;
		const SignalType sigtype = (p2 >= SIGTYPE_BLOCK && p2 <= SIGTYPE_LAST)
				? static_cast<SignalType>(p2) : SIGTYPE_BLOCK;
		CommandCost cost = Command<CMD_BUILD_SINGLE_SIGNAL>::Do(
				DoCommandFlag::Execute, tile_a, track, sigtype, SIG_ELECTRIC,
				false, false, false,
				static_cast<SignalType>(0), static_cast<SignalType>(0),
				static_cast<uint8_t>(1), static_cast<uint8_t>(0));
		return dump(SctCostResult(cost));
	}

	if (std::strcmp(action, "road") == 0) {
		const RoadType rt = SctResolveRoadType(p1);
		if (a == b || !IsValidTile(tile_b) || tile_a == tile_b) {
			/* Single tile: road_cmd.h:26 CmdBuildRoad */
			CommandCost cost = Command<CMD_BUILD_ROAD>::Do(
					DoCommandFlag::Execute, tile_a, ROAD_X, rt, DRD_NONE, TownID::Invalid());
			return dump(SctCostResult(cost));
		}
		if (TileX(tile_a) != TileX(tile_b) && TileY(tile_a) != TileY(tile_b)) {
			return dump({{"ok", false}, {"error", "road not axis-aligned"}, {"cost", 0}});
		}
		/* Drag: road_cmd.h:24 CmdBuildLongRoad(end, start, rt, axis, drd, start_half, end_half, is_ai) */
		const Axis axis = (TileY(tile_a) != TileY(tile_b)) ? AXIS_Y : AXIS_X;
		CommandCost cost = Command<CMD_BUILD_LONG_ROAD>::Do(
				DoCommandFlag::Execute, tile_b, tile_a, rt, axis, DRD_NONE, false, false, false);
		return dump(SctCostResult(cost));
	}

	if (std::strcmp(action, "remove_road") == 0) {
		const RoadType rt = SctResolveRoadType(p1);
		if (!IsValidTile(tile_a) || !IsValidTile(tile_b)) {
			return dump({{"ok", false}, {"error", "invalid tile"}, {"cost", 0}});
		}
		if (TileX(tile_a) != TileX(tile_b) && TileY(tile_a) != TileY(tile_b)) {
			return dump({{"ok", false}, {"error", "road not axis-aligned"}, {"cost", 0}});
		}
		const Axis axis = (TileY(tile_a) != TileY(tile_b)) ? AXIS_Y : AXIS_X;
		auto result = Command<CMD_REMOVE_LONG_ROAD>::Do(
				DoCommandFlag::Execute, tile_b, tile_a, rt, axis, false, false);
		return dump(SctCostResult(std::get<0>(result)));
	}

	if (std::strcmp(action, "one_way_road") == 0) {
		/* Wave 21 — build road then flag one-way, mirroring the "road" action with a
		 * disallowed-direction set. p1 = roadtype (as "road"); p2 selects the
		 * DisallowedRoadDirections (road_type.h): default/<=0 → DRD_NORTHBOUND (the GUI
		 * one-way default, road_gui.cpp:756), 2 → DRD_SOUTHBOUND, 3 → DRD_BOTH.
		 * Constraint: one-way state only lives on straight full-tile road pieces and
		 * a one-way road may not form a junction (road_cmd.cpp:643-645), so this is a
		 * straight-run tool only. Single tile uses CmdBuildRoad's toggle_drd (XOR): a
		 * fresh straight piece (DRD_NONE) toggles to the requested drd; re-issuing on
		 * the same tile toggles it back off. Drags use CmdBuildLongRoad's drd param. */
		const RoadType rt = SctResolveRoadType(p1);
		DisallowedRoadDirections drd = DRD_NORTHBOUND;
		if (p2 == static_cast<int>(DRD_SOUTHBOUND)) drd = DRD_SOUTHBOUND;
		else if (p2 == static_cast<int>(DRD_BOTH)) drd = DRD_BOTH;

		if (a == b || !IsValidTile(tile_b) || tile_a == tile_b) {
			/* Single straight tile: road_cmd.h:26 CmdBuildRoad(tile, ROAD_X, rt, toggle_drd, town). */
			CommandCost cost = Command<CMD_BUILD_ROAD>::Do(
					DoCommandFlag::Execute, tile_a, ROAD_X, rt, drd, TownID::Invalid());
			return dump(SctCostResult(cost));
		}
		if (TileX(tile_a) != TileX(tile_b) && TileY(tile_a) != TileY(tile_b)) {
			return dump({{"ok", false}, {"error", "road not axis-aligned"}, {"cost", 0}});
		}
		/* Drag: road_cmd.h:24 CmdBuildLongRoad(end, start, rt, axis, drd, start_half, end_half, is_ai). */
		const Axis axis = (TileY(tile_a) != TileY(tile_b)) ? AXIS_Y : AXIS_X;
		CommandCost cost = Command<CMD_BUILD_LONG_ROAD>::Do(
				DoCommandFlag::Execute, tile_b, tile_a, rt, axis, drd, false, false, false);
		return dump(SctCostResult(cost));
	}

	if (std::strcmp(action, "road_depot") == 0) {
		/* road_cmd.h:27 CmdBuildRoadDepot; p2==255 → auto-orient toward adjacent road */
		const RoadType rt = SctResolveRoadType(p1);
		const DiagDirection dir = (p2 == 255)
				? SctAutoOrientRoadDepot(tile_a)
				: SctResolveDiagDir(p2);
		SctLevelFootprint(tile_a, 1, 1); /* Wave 6: single-tile flatten */
		CommandCost cost = Command<CMD_BUILD_ROAD_DEPOT>::Do(
				DoCommandFlag::Execute, tile_a, rt, dir);
		return dump(SctCostResult(cost));
	}

	if (std::strcmp(action, "dock") == 0) {
		/* station_cmd.h:27 CmdBuildDock */
		CommandCost cost = Command<CMD_BUILD_DOCK>::Do(
				DoCommandFlag::Execute, tile_a, StationID::Invalid(), false);
		return dump(SctCostResult(cost));
	}

	if (std::strcmp(action, "canal") == 0) {
		/* Wave 26 — water_cmd.h CmdBuildCanal(end_tile, start_tile, WaterClass, diagonal).
		 * a=end, b=start of the drag rectangle (dock_gui.cpp:267-273 passes end,start).
		 * Canals for a normal game; in the scenario editor with p1==1 place rivers
		 * (WaterClass::River), matching the SE "define rivers" tool. diagonal=false. */
		if (!IsValidTile(tile_a) || !IsValidTile(tile_b)) {
			return dump({{"ok", false}, {"error", "invalid tile"}, {"cost", 0}});
		}
		const WaterClass wc = (_game_mode == GM_EDITOR && p1 == 1)
				? WaterClass::River
				: WaterClass::Canal;
		CommandCost cost = Command<CMD_BUILD_CANAL>::Do(
				DoCommandFlag::Execute, tile_a, tile_b, wc, false);
		return dump(SctCostResult(cost));
	}

	if (std::strcmp(action, "lock") == 0) {
		/* Wave 26 — water_cmd.h CmdBuildLock(tile) (dock_gui.cpp:203). */
		CommandCost cost = Command<CMD_BUILD_LOCK>::Do(DoCommandFlag::Execute, tile_a);
		return dump(SctCostResult(cost));
	}

	if (std::strcmp(action, "buoy") == 0) {
		/* Wave 26 — waypoint_cmd.h CmdBuildBuoy(tile) (dock_gui.cpp:233). */
		CommandCost cost = Command<CMD_BUILD_BUOY>::Do(DoCommandFlag::Execute, tile_a);
		return dump(SctCostResult(cost));
	}

	if (std::strcmp(action, "ship_depot") == 0) {
		/* Wave 26 — water_cmd.h CmdBuildShipDepot(tile, Axis) (dock_gui.cpp:211);
		 * p1 selects the axis (0 X, 1 Y). */
		const Axis axis = SctResolveAxis(p1);
		CommandCost cost = Command<CMD_BUILD_SHIP_DEPOT>::Do(
				DoCommandFlag::Execute, tile_a, axis);
		return dump(SctCostResult(cost));
	}

	if (std::strcmp(action, "aqueduct") == 0) {
		/* Wave 26 — an aqueduct is a TRANSPORT_WATER bridge; dock_gui.cpp:241 builds it
		 * with bridge_type 0 and road_rail_type 0. Mirrors the "bridge" action plumbing
		 * (a=end ramp, b=start ramp), auto-picking the cheapest valid water bridge spec
		 * for the span so it is robust to NewGRF aqueduct sets. */
		if (!IsValidTile(tile_a) || !IsValidTile(tile_b)) {
			return dump({{"ok", false}, {"error", "invalid tile"}, {"cost", 0}});
		}
		const uint bridge_len = GetTunnelBridgeLength(tile_b, tile_a);
		BridgeType bt = 0;
		bool found = false;
		uint16_t best_price = 0;
		for (BridgeType cand = 0; cand < MAX_BRIDGES; cand++) {
			if (CheckBridgeAvailability(cand, bridge_len).Failed()) continue;
			const uint16_t price = GetBridgeSpec(cand)->price;
			if (!found || price < best_price) {
				best_price = price;
				bt = cand;
				found = true;
			}
		}
		if (!found) {
			return dump({{"ok", false}, {"error", "no bridge available"}, {"cost", 0}});
		}
		CommandCost cost = Command<CMD_BUILD_BRIDGE>::Do(
				DoCommandFlag::Execute, tile_a, tile_b, TRANSPORT_WATER,
				bt, static_cast<uint8_t>(0));
		return dump(SctCostResult(cost));
	}

	if (std::strcmp(action, "airport") == 0) {
		/* station_cmd.h:26 CmdBuildAirport */
		SctLevelFootprint(tile_a, 1, 1); /* Wave 6 v1: single-tile flatten then build */
		CommandCost cost = Command<CMD_BUILD_AIRPORT>::Do(
				DoCommandFlag::Execute, tile_a,
				static_cast<uint8_t>(std::clamp(p1, 0, 255)),
				static_cast<uint8_t>(std::clamp(p2, 0, 255)),
				StationID::Invalid(), false);
		return dump(SctCostResult(cost));
	}

	if (std::strcmp(action, "demolish") == 0) {
		if (a != b && IsValidTile(tile_b) && tile_a != tile_b) {
			/* landscape_cmd.h CmdClearArea — returns (CommandCost, Money) */
			auto res = Command<CMD_CLEAR_AREA>::Do(DoCommandFlag::Execute, tile_b, tile_a, false);
			const CommandCost &cost = std::get<0>(res);
			nlohmann::json j = SctCostResult(cost);
			if (cost.Succeeded()) j["cost"] = static_cast<int64_t>(std::get<1>(res));
			return dump(j);
		}
		/* landscape_cmd.h CmdLandscapeClear */
		CommandCost cost = Command<CMD_LANDSCAPE_CLEAR>::Do(DoCommandFlag::Execute, tile_a);
		return dump(SctCostResult(cost));
	}

	if (std::strcmp(action, "terrain_up") == 0) {
		/* terraform_cmd.h CmdTerraformLand(tile, slope, dir_up) — GUI uses SLOPE_N */
		auto res = Command<CMD_TERRAFORM_LAND>::Do(DoCommandFlag::Execute, tile_a, SLOPE_N, true);
		const CommandCost &cost = std::get<0>(res);
		nlohmann::json j = SctCostResult(cost);
		if (cost.Succeeded()) j["cost"] = static_cast<int64_t>(std::get<1>(res));
		return dump(j);
	}

	if (std::strcmp(action, "terrain_down") == 0) {
		auto res = Command<CMD_TERRAFORM_LAND>::Do(DoCommandFlag::Execute, tile_a, SLOPE_N, false);
		const CommandCost &cost = std::get<0>(res);
		nlohmann::json j = SctCostResult(cost);
		if (cost.Succeeded()) j["cost"] = static_cast<int64_t>(std::get<1>(res));
		return dump(j);
	}

	if (std::strcmp(action, "terrain_level") == 0) {
		/* terraform_cmd.h CmdLevelLand(end, start, diagonal, LM_LEVEL) */
		auto res = Command<CMD_LEVEL_LAND>::Do(DoCommandFlag::Execute, tile_b, tile_a, false, LM_LEVEL);
		const CommandCost &cost = std::get<0>(res);
		nlohmann::json j = SctCostResult(cost);
		if (cost.Succeeded()) j["cost"] = static_cast<int64_t>(std::get<1>(res));
		return dump(j);
	}

	if (std::strcmp(action, "bridge") == 0) {
		/* Wave 13 — tunnelbridge_cmd.h CmdBuildBridge(end, start, transport, bridge_type,
		 * road_rail_type). a=end tile, b=start tile; p1 transport (0 rail, 1 road);
		 * p2 explicit bridge type or <=0 = auto-pick the cheapest valid spec for the span
		 * (bridge_gui.cpp:415-431). road_rail_type is the stashed rail/road type. */
		const TransportType tt = (p1 == 1) ? TRANSPORT_ROAD : TRANSPORT_RAIL;
		const uint8_t rr_type = (tt == TRANSPORT_RAIL)
				? static_cast<uint8_t>(SctResolveRailType(0))
				: static_cast<uint8_t>(SctResolveRoadType(0));
		if (!IsValidTile(tile_a) || !IsValidTile(tile_b)) {
			return dump({{"ok", false}, {"error", "invalid tile"}, {"cost", 0}});
		}
		/* bridge.h/tunnelbridge.h: length is the number of tiles spanned between the
		 * two ramps (exclusive), same value CheckBridgeAvailability expects. */
		const uint bridge_len = GetTunnelBridgeLength(tile_b, tile_a);
		BridgeType bt = 0;
		if (p2 > 0 && static_cast<uint>(p2) < MAX_BRIDGES) {
			bt = static_cast<BridgeType>(p2);
		} else {
			bool found = false;
			uint16_t best_price = 0;
			for (BridgeType cand = 0; cand < MAX_BRIDGES; cand++) {
				if (CheckBridgeAvailability(cand, bridge_len).Failed()) continue;
				const uint16_t price = GetBridgeSpec(cand)->price;
				if (!found || price < best_price) {
					best_price = price;
					bt = cand;
					found = true;
				}
			}
			if (!found) {
				return dump({{"ok", false}, {"error", "no bridge available"}, {"cost", 0}});
			}
		}
		CommandCost cost = Command<CMD_BUILD_BRIDGE>::Do(
				DoCommandFlag::Execute, tile_a, tile_b, tt, bt, rr_type);
		return dump(SctCostResult(cost));
	}

	if (std::strcmp(action, "tunnel") == 0) {
		/* Wave 13 — tunnelbridge_cmd.h CmdBuildTunnel(start, transport, road_rail_type).
		 * a=start tile; p1 transport (0 rail, 1 road). The engine bores toward the facing
		 * hillside and picks the exit itself. */
		const TransportType tt = (p1 == 1) ? TRANSPORT_ROAD : TRANSPORT_RAIL;
		const uint8_t rr_type = (tt == TRANSPORT_RAIL)
				? static_cast<uint8_t>(SctResolveRailType(0))
				: static_cast<uint8_t>(SctResolveRoadType(0));
		CommandCost cost = Command<CMD_BUILD_TUNNEL>::Do(
				DoCommandFlag::Execute, tile_a, tt, rr_type);
		return dump(SctCostResult(cost));
	}

	if (std::strcmp(action, "rail_waypoint") == 0) {
		/* Wave 13 — waypoint_cmd.h CmdBuildRailWaypoint(tile, axis, w, h, class, index,
		 * join, adjacent); a=tile, axis from p1, 1x1 default (rail_gui.cpp:183). */
		const Axis axis = SctResolveAxis(p1);
		SctLevelFootprint(tile_a, 1, 1); /* flatten so it never fails on slopes */
		CommandCost cost = Command<CMD_BUILD_RAIL_WAYPOINT>::Do(
				DoCommandFlag::Execute, tile_a, axis,
				static_cast<uint8_t>(1), static_cast<uint8_t>(1),
				STAT_CLASS_WAYP, static_cast<uint16_t>(0), StationID::Invalid(), false);
		return dump(SctCostResult(cost));
	}

	if (std::strcmp(action, "convert_rail") == 0) {
		/* Wave 13 — rail_cmd.h CmdConvertRail(end, area_start, totype, diagonal)
		 * (rail_gui.cpp:759). a..b area, to the stashed / p1 railtype. Single tile when
		 * b is unset (a==b). */
		const RailType rt = SctResolveRailType(p1);
		if (!IsValidTile(tile_a)) {
			return dump({{"ok", false}, {"error", "invalid tile"}, {"cost", 0}});
		}
		const TileIndex end = (a != b && IsValidTile(tile_b)) ? tile_b : tile_a;
		CommandCost cost = Command<CMD_CONVERT_RAIL>::Do(
				DoCommandFlag::Execute, end, tile_a, rt, false);
		return dump(SctCostResult(cost));
	}

	return dump({{"ok", false}, {"error", "unknown action"}, {"cost", 0}});
}

/**
 * Test a rail/road construction gesture without mutating the simulation.
 *
 * The React construction guide calls this after snapping the pointer. Keeping
 * validation in OpenTTD means the guide reports real ownership, slope, vehicle,
 * obstacle, and local-authority failures instead of a client-side guess.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_preview_build(const char *action, int a, int b, int p1, int p2)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (action == nullptr || !SctCanBuild()) {
		return dump({{"ok", false}, {"error", action == nullptr ? "unknown action" : "not in game"}, {"cost", 0}});
	}

	AutoRestoreBackup backup(_current_company, _local_company);
	const TileIndex tile_a{static_cast<uint32_t>(a)};
	const TileIndex tile_b{static_cast<uint32_t>(b)};
	if (!IsValidTile(tile_a) || !IsValidTile(tile_b)) {
		return dump({{"ok", false}, {"error", "invalid tile"}, {"cost", 0}});
	}

	if (std::strcmp(action, "rail_track") == 0 || std::strcmp(action, "remove_rail") == 0) {
		const SctRailDrag drag = SctResolveRailDrag(tile_a, tile_b, p2);
		CommandCost cost = (std::strcmp(action, "remove_rail") == 0)
				? Command<CMD_REMOVE_RAILROAD_TRACK>::Do(DoCommandFlags{}, drag.end, tile_a, drag.track)
				: Command<CMD_BUILD_RAILROAD_TRACK>::Do(
						DoCommandFlags{}, drag.end, tile_a, SctResolveRailType(p1), drag.track, true, false);
		if (cost.Failed() && drag.alternate != INVALID_TRACK) {
			cost = (std::strcmp(action, "remove_rail") == 0)
					? Command<CMD_REMOVE_RAILROAD_TRACK>::Do(DoCommandFlags{}, drag.end, tile_a, drag.alternate)
					: Command<CMD_BUILD_RAILROAD_TRACK>::Do(
							DoCommandFlags{}, drag.end, tile_a, SctResolveRailType(p1), drag.alternate, true, false);
		}
		return dump(SctCostResult(cost));
	}

	if (std::strcmp(action, "road") == 0 || std::strcmp(action, "one_way_road") == 0 ||
			std::strcmp(action, "remove_road") == 0) {
		if (TileX(tile_a) != TileX(tile_b) && TileY(tile_a) != TileY(tile_b)) {
			return dump({{"ok", false}, {"error", "road not axis-aligned"}, {"cost", 0}});
		}

		const RoadType rt = SctResolveRoadType(p1);
		const Axis axis = (TileY(tile_a) != TileY(tile_b)) ? AXIS_Y : AXIS_X;
		if (std::strcmp(action, "remove_road") == 0) {
			auto result = Command<CMD_REMOVE_LONG_ROAD>::Do(
					DoCommandFlags{}, tile_b, tile_a, rt, axis, false, false);
			return dump(SctCostResult(std::get<0>(result)));
		}

		const DisallowedRoadDirections drd = std::strcmp(action, "one_way_road") == 0
				? DRD_NORTHBOUND
				: DRD_NONE;
		if (tile_a == tile_b) {
			CommandCost cost = Command<CMD_BUILD_ROAD>::Do(
					DoCommandFlags{}, tile_a, ROAD_X, rt, drd, TownID::Invalid());
			return dump(SctCostResult(cost));
		}
		CommandCost cost = Command<CMD_BUILD_LONG_ROAD>::Do(
				DoCommandFlags{}, tile_b, tile_a, rt, axis, drd, false, false, false);
		return dump(SctCostResult(cost));
	}

	return dump({{"ok", false}, {"error", "preview unavailable"}, {"cost", 0}});
}

/**
 * Build a vehicle in a depot; returns the new vehicle id on success.
 * vehicle_cmd.h:20 CmdBuildVehicle.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_build_vehicle(int depot_tile, int engine_id)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!SctCanBuild()) {
		return dump({{"ok", false}, {"error", "not in game"}, {"id", -1}, {"cost", 0}});
	}

	AutoRestoreBackup backup(_current_company, _local_company);

	auto result = Command<CMD_BUILD_VEHICLE>::Do(
			DoCommandFlag::Execute,
			TileIndex{static_cast<uint32_t>(depot_tile)},
			EngineID{static_cast<uint16_t>(engine_id)},
			true,
			INVALID_CARGO,
			INVALID_CLIENT_ID);
	const CommandCost &cost = std::get<0>(result);
	const VehicleID veh_id = std::get<1>(result);

	const bool ok = cost.Succeeded() && veh_id != VehicleID::Invalid();
	return dump({
		{"ok", ok},
		{"error", ok ? std::string{} : SctErrorFromCost(cost)},
		{"id", ok ? static_cast<int>(veh_id.base()) : -1},
		{"cost", static_cast<int64_t>(cost.GetCost())},
	});
}

/**
 * Vehicle management: start/stop (toggle), sell, sendToDepot.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_vehicle_cmd(int vehicle_id, const char *action)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (action == nullptr) {
		return dump({{"ok", false}, {"error", "unknown action"}});
	}
	if (!SctCanBuild()) {
		return dump({{"ok", false}, {"error", "not in game"}});
	}

	const VehicleID vid{static_cast<uint32_t>(vehicle_id)};
	if (!Vehicle::IsValidID(vid)) {
		return dump({{"ok", false}, {"error", "invalid vehicle"}});
	}

	AutoRestoreBackup backup(_current_company, _local_company);

	if (std::strcmp(action, "start") == 0 || std::strcmp(action, "stop") == 0) {
		/* vehicle_cmd.h:27 CmdStartStopVehicle — toggles start/stop. The command
		 * is a pure toggle, so make this idempotent: only issue it when the
		 * current state differs from the request (the HUD has mass "Stop all"
		 * buttons — a blind toggle would start already-stopped vehicles).
		 * vehicle_base.h:309 vehstatus; VehState::Stopped set == parked. */
		const Vehicle *v = Vehicle::Get(vid);
		const bool is_stopped = v->vehstatus.Test(VehState::Stopped);
		const bool want_stopped = (std::strcmp(action, "stop") == 0);
		if (is_stopped == want_stopped) {
			return dump({{"ok", true}, {"error", std::string{}}});
		}
		CommandCost cost = Command<CMD_START_STOP_VEHICLE>::Do(
				DoCommandFlag::Execute, vid, false);
		return dump(SctOkResult(cost));
	}

	if (std::strcmp(action, "sell") == 0) {
		/* vehicle_cmd.h:21 CmdSellVehicle */
		CommandCost cost = Command<CMD_SELL_VEHICLE>::Do(
				DoCommandFlag::Execute, vid, true, false, INVALID_CLIENT_ID);
		return dump(SctOkResult(cost));
	}

	if (std::strcmp(action, "sendToDepot") == 0) {
		/* vehicle_cmd.h:23 CmdSendVehicleToDepot */
		CommandCost cost = Command<CMD_SEND_VEHICLE_TO_DEPOT>::Do(
				DoCommandFlag::Execute, vid, DepotCommandFlags{}, VehicleListIdentifier{});
		return dump(SctOkResult(cost));
	}

	if (std::strcmp(action, "skipOrder") == 0) {
		/* order_cmd.h:18 CmdSkipToOrder — skip to the next order. Target index
		 * mirrors the GUI skip button (order_gui.cpp:697):
		 * (cur_implicit_order_index + 1) % GetNumOrders(). No-op for 0/1 orders. */
		const Vehicle *v = Vehicle::Get(vid);
		if (v->GetNumOrders() <= 1) {
			return dump({{"ok", true}, {"error", std::string{}}});
		}
		const VehicleOrderID sel = static_cast<VehicleOrderID>(
				(v->cur_implicit_order_index + 1) % v->GetNumOrders());
		CommandCost cost = Command<CMD_SKIP_TO_ORDER>::Do(DoCommandFlag::Execute, vid, sel);
		return dump(SctOkResult(cost));
	}

	return dump({{"ok", false}, {"error", "unknown action"}});
}

/**
 * Append a goto-station or goto-depot order to a vehicle's order list.
 * order_cmd.h:20 CmdInsertOrder; order_base.h:76 MakeGoToStation, :77 MakeGoToDepot.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_add_order(int vehicle_id, const char *kind, int dest_id)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (kind == nullptr) {
		return dump({{"ok", false}, {"error", "unknown kind"}});
	}
	if (!SctCanBuild()) {
		return dump({{"ok", false}, {"error", "not in game"}});
	}

	AutoRestoreBackup backup(_current_company, _local_company);

	const Vehicle *v = Vehicle::GetIfValid(vehicle_id);
	if (v == nullptr) {
		return dump({{"ok", false}, {"error", "no vehicle"}});
	}

	Order o{};
	if (std::strcmp(kind, "station") == 0) {
		o.MakeGoToStation(StationID{static_cast<uint16_t>(dest_id)});
	} else if (std::strcmp(kind, "depot") == 0) {
		/* order_type.h: OrderDepotTypeFlag::PartOfOrders (replaces legacy ODTFB_PART_OF_ORDERS) */
		o.MakeGoToDepot(DestinationID{static_cast<size_t>(dest_id)}, OrderDepotTypeFlag::PartOfOrders);
	} else {
		return dump({{"ok", false}, {"error", "unknown kind"}});
	}

	/* Append: sel_ord == current order count (vehicle_base.h:705 GetNumOrders).
	 * Do() takes (flags, cmd-args...) with no location tile — that prefix is
	 * only for Post(). CmdInsertOrder args are (veh, sel_ord, order). */
	const VehicleOrderID sel_ord = v->GetNumOrders();
	CommandCost cost = Command<CMD_INSERT_ORDER>::Do(
			DoCommandFlag::Execute, VehicleID{static_cast<uint32_t>(vehicle_id)}, sel_ord, o);
	return dump(SctOkResult(cost));
}

/**
 * Read-back of a vehicle's order list (Wave 4; timetable fields added Wave 14).
 * @return JSON array of orders, or the literal string "null" if vehicle is invalid.
 * Schema: [{"index", "type", "dest", "nonstop", "load", "unload",
 *           "waitTime", "travelTime", "timetableStarted"}, ...]
 * type is "station"|"depot"|"waypoint"|"other"; dest is -1 for "other".
 * waitTime / travelTime are timetabled ticks for the order (order_base.h
 * GetTimetabledWait/GetTimetabledTravel; 0 when that leg is not timetabled).
 * timetableStarted (base_consist.h VehicleFlag::TimetableStarted) is the same
 * vehicle-level bool repeated on every entry so the array shape is preserved.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_vehicle_orders(int vehicle_id)
{
	static std::string buffer;

	const Vehicle *v = Vehicle::GetIfValid(vehicle_id);
	if (v == nullptr) {
		buffer = "null";
		return buffer.c_str();
	}

	const bool timetable_started = v->vehicle_flags.Test(VehicleFlag::TimetableStarted);

	nlohmann::json arr = nlohmann::json::array();
	int index = 0;
	for (const Order &o : v->Orders()) {
		const OrderType ot = o.GetType();
		const char *type_str;
		int dest;
		if (ot == OT_GOTO_STATION) {
			type_str = "station";
			dest = static_cast<int>(o.GetDestination().base());
		} else if (ot == OT_GOTO_DEPOT) {
			type_str = "depot";
			dest = static_cast<int>(o.GetDestination().base());
		} else if (ot == OT_GOTO_WAYPOINT) {
			type_str = "waypoint";
			dest = static_cast<int>(o.GetDestination().base());
		} else {
			type_str = "other";
			dest = -1;
		}

		/* GetNonStopType returns OrderNonStopFlags (EnumBitSet) — use .base() for the raw 0–3 bits.
		 * GetLoadType/GetUnloadType are enum class : uint8_t — static_cast<int> is fine. */
		arr.push_back({
			{"index", index},
			{"type", type_str},
			{"dest", dest},
			{"nonstop", static_cast<int>(o.GetNonStopType().base())},
			{"load", static_cast<int>(o.GetLoadType())},
			{"unload", static_cast<int>(o.GetUnloadType())},
			{"waitTime", static_cast<int>(o.GetTimetabledWait())},
			{"travelTime", static_cast<int>(o.GetTimetabledTravel())},
			{"timetableStarted", timetable_started},
		});
		++index;
	}

	buffer = arr.dump();
	return buffer.c_str();
}

/**
 * Wave 8 — fleet management.
 * Clone a vehicle into a depot. share_orders != 0 shares the source's order
 * list (the OpenTTD idiom for "add another vehicle to this route"); otherwise
 * the orders are copied. vehicle_cmd.h:26 CmdCloneVehicle.
 * @return {ok, error, id, cost}; id is the new vehicle, -1 on failure.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_clone_vehicle(int depot_tile, int vehicle_id, int share_orders)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!SctCanBuild()) {
		return dump({{"ok", false}, {"error", "not in game"}, {"id", -1}, {"cost", 0}});
	}

	const VehicleID vid{static_cast<uint32_t>(vehicle_id)};
	if (!Vehicle::IsValidID(vid)) {
		return dump({{"ok", false}, {"error", "invalid vehicle"}, {"id", -1}, {"cost", 0}});
	}

	AutoRestoreBackup backup(_current_company, _local_company);

	auto result = Command<CMD_CLONE_VEHICLE>::Do(
			DoCommandFlag::Execute,
			TileIndex{static_cast<uint32_t>(depot_tile)},
			vid,
			share_orders != 0);
	const CommandCost &cost = std::get<0>(result);
	const VehicleID new_id = std::get<1>(result);

	const bool ok = cost.Succeeded() && new_id != VehicleID::Invalid();
	return dump({
		{"ok", ok},
		{"error", ok ? std::string{} : SctErrorFromCost(cost)},
		{"id", ok ? static_cast<int>(new_id.base()) : -1},
		{"cost", static_cast<int64_t>(cost.GetCost())},
	});
}

/**
 * Create a vehicle group. vt: 0=train,1=road,2=ship,3=aircraft. A top-level
 * group uses GroupID::Invalid() as parent. group_cmd.h:28 CmdCreateGroup.
 * @return {ok, error, id}; id is the new group, -1 on failure.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_create_group(int vehicle_type, int parent_group)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!SctCanBuild()) {
		return dump({{"ok", false}, {"error", "not in game"}, {"id", -1}});
	}
	if (vehicle_type < VEH_TRAIN || vehicle_type > VEH_AIRCRAFT) {
		return dump({{"ok", false}, {"error", "invalid vehicle type"}, {"id", -1}});
	}

	AutoRestoreBackup backup(_current_company, _local_company);

	const GroupID parent = (parent_group < 0)
			? GroupID::Invalid()
			: GroupID{static_cast<uint16_t>(parent_group)};
	auto result = Command<CMD_CREATE_GROUP>::Do(
			DoCommandFlag::Execute,
			static_cast<VehicleType>(vehicle_type),
			parent);
	const CommandCost &cost = std::get<0>(result);
	const GroupID gid = std::get<1>(result);

	const bool ok = cost.Succeeded() && gid != GroupID::Invalid();
	return dump({
		{"ok", ok},
		{"error", ok ? std::string{} : SctErrorFromCost(cost)},
		{"id", ok ? static_cast<int>(gid.base()) : -1},
	});
}

/**
 * Move a vehicle into a group. add_shared != 0 also moves all vehicles that
 * share the same orders. group_cmd.h:31 CmdAddVehicleGroup.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_add_to_group(int group_id, int vehicle_id, int add_shared)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!SctCanBuild()) {
		return dump({{"ok", false}, {"error", "not in game"}});
	}

	const VehicleID vid{static_cast<uint32_t>(vehicle_id)};
	if (!Vehicle::IsValidID(vid)) {
		return dump({{"ok", false}, {"error", "invalid vehicle"}});
	}

	AutoRestoreBackup backup(_current_company, _local_company);

	auto result = Command<CMD_ADD_VEHICLE_GROUP>::Do(
			DoCommandFlag::Execute,
			GroupID{static_cast<uint16_t>(group_id)},
			vid,
			add_shared != 0,
			VehicleListIdentifier{});
	const CommandCost &cost = std::get<0>(result);
	return dump(SctOkResult(cost));
}

/**
 * Refit a vehicle to carry a different cargo. cargo_type is a CargoType id
 * (0..NUM_CARGO-1). Refits the whole consist. vehicle_cmd.h:22 CmdRefitVehicle.
 * @return {ok, error, cost}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_refit_vehicle(int vehicle_id, int cargo_type)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!SctCanBuild()) {
		return dump({{"ok", false}, {"error", "not in game"}, {"cost", 0}});
	}

	const VehicleID vid{static_cast<uint32_t>(vehicle_id)};
	if (!Vehicle::IsValidID(vid)) {
		return dump({{"ok", false}, {"error", "invalid vehicle"}, {"cost", 0}});
	}

	AutoRestoreBackup backup(_current_company, _local_company);

	auto result = Command<CMD_REFIT_VEHICLE>::Do(
			DoCommandFlag::Execute,
			vid,
			static_cast<CargoType>(cargo_type),
			0,      /* new_subtype */
			false,  /* auto_refit */
			false,  /* only_this */
			0);     /* num_vehicles: 0 = whole consist */
	const CommandCost &cost = std::get<0>(result);
	const bool ok = cost.Succeeded();
	return dump({
		{"ok", ok},
		{"error", ok ? std::string{} : SctErrorFromCost(cost)},
		{"cost", static_cast<int64_t>(cost.GetCost())},
	});
}

/**
 * Rename a vehicle. Empty name resets to the default. vehicle_cmd.h:25
 * CmdRenameVehicle.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_rename_vehicle(int vehicle_id, const char *name)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!SctCanBuild()) {
		return dump({{"ok", false}, {"error", "not in game"}});
	}

	const VehicleID vid{static_cast<uint32_t>(vehicle_id)};
	if (!Vehicle::IsValidID(vid)) {
		return dump({{"ok", false}, {"error", "invalid vehicle"}});
	}

	AutoRestoreBackup backup(_current_company, _local_company);

	CommandCost cost = Command<CMD_RENAME_VEHICLE>::Do(
			DoCommandFlag::Execute,
			vid,
			std::string{name == nullptr ? "" : name});
	return dump(SctOkResult(cost));
}

/**
 * Wave 10 — borrow (delta>0) or repay (delta<0) loan by |delta|.
 * delta>0 → misc_cmd.h:24 CmdIncreaseLoan; delta<0 → CmdDecreaseLoan.
 * Both use the fixed-Amount variant (LoanCommand::Amount, amount=|delta|).
 * @return {ok, error}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_company_loan(int delta)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!SctCanBuild()) {
		return dump({{"ok", false}, {"error", "not in game"}});
	}
	if (delta == 0) {
		return dump({{"ok", true}, {"error", std::string{}}});
	}

	AutoRestoreBackup backup(_current_company, _local_company);

	const Money amount = static_cast<Money>(std::abs(static_cast<int64_t>(delta)));
	CommandCost cost = (delta > 0)
			? Command<CMD_INCREASE_LOAN>::Do(DoCommandFlag::Execute, LoanCommand::Amount, amount)
			: Command<CMD_DECREASE_LOAN>::Do(DoCommandFlag::Execute, LoanCommand::Amount, amount);
	return dump(SctOkResult(cost));
}

/**
 * Wave 10 — delete the order at orderIndex from a vehicle's order list.
 * order_cmd.h:19 CmdDeleteOrder.
 * @return {ok, error}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_delete_order(int vehicle_id, int order_index)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!SctCanBuild()) {
		return dump({{"ok", false}, {"error", "not in game"}});
	}

	const VehicleID vid{static_cast<uint32_t>(vehicle_id)};
	if (!Vehicle::IsValidID(vid)) {
		return dump({{"ok", false}, {"error", "invalid vehicle"}});
	}

	AutoRestoreBackup backup(_current_company, _local_company);

	CommandCost cost = Command<CMD_DELETE_ORDER>::Do(
			DoCommandFlag::Execute, vid, static_cast<VehicleOrderID>(order_index));
	return dump(SctOkResult(cost));
}

/**
 * Wave 10 — modify a field of an existing order. mof is a ModifyOrderFlags int
 * (order_type.h:159): 0=MOF_NON_STOP, 1=MOF_STOP_LOCATION, 2=MOF_UNLOAD,
 * 3=MOF_LOAD, 4=MOF_DEPOT_ACTION. value is the new field value.
 * order_cmd.h:17 CmdModifyOrder.
 * @return {ok, error}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_modify_order(int vehicle_id, int order_index, int mof, int value)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!SctCanBuild()) {
		return dump({{"ok", false}, {"error", "not in game"}});
	}

	const VehicleID vid{static_cast<uint32_t>(vehicle_id)};
	if (!Vehicle::IsValidID(vid)) {
		return dump({{"ok", false}, {"error", "invalid vehicle"}});
	}

	AutoRestoreBackup backup(_current_company, _local_company);

	CommandCost cost = Command<CMD_MODIFY_ORDER>::Do(
			DoCommandFlag::Execute, vid, static_cast<VehicleOrderID>(order_index),
			static_cast<ModifyOrderFlags>(mof), static_cast<uint16_t>(value));
	return dump(SctOkResult(cost));
}

/**
 * Wave 10 — scroll/centre the main viewport on a tile (not a Command).
 * viewport_func.h:79 ScrollMainWindowToTile(tile, instant=true).
 * @return {ok}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_scroll_to_tile(int tile)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!SctInGame() || !Map::IsInitialized()) {
		return dump({{"ok", false}});
	}
	if (tile < 0 || static_cast<uint>(tile) >= Map::Size()) {
		return dump({{"ok", false}});
	}

	bool ok = ScrollMainWindowToTile(TileIndex{static_cast<uint32_t>(tile)}, true);
	return dump({{"ok", ok}});
}

/**
 * Wave 10 — describe what is on a tile so the UI can route map clicks.
 * Tile-map accessors (depot_map.h, station_map.h, industry_map.h, town_map.h)
 * guarded by IsTileType checks; bounds-checked against Map::Size().
 * @return JSON `{kind, id?, vehicleType?, owner?}` or `{kind:"invalid"}`.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_tile_info(int tile)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!SctInGame() || !Map::IsInitialized()) {
		return dump({{"kind", "invalid"}});
	}
	if (tile < 0 || static_cast<uint>(tile) >= Map::Size()) {
		return dump({{"kind", "invalid"}});
	}

	const TileIndex t{static_cast<uint32_t>(tile)};
	nlohmann::json j = nlohmann::json::object();

	switch (GetTileType(t)) {
		case MP_CLEAR:
			j["kind"] = "clear";
			j["owner"] = GetTileOwner(t).base();
			break;

		case MP_TREES:
			j["kind"] = "trees";
			j["owner"] = GetTileOwner(t).base();
			break;

		case MP_RAILWAY:
			if (IsRailDepotTile(t)) {
				j["kind"] = "depot";
				j["vehicleType"] = static_cast<int>(GetDepotVehicleType(t)); /* VEH_TRAIN = 0 */
				j["id"] = static_cast<int>(GetDepotIndex(t).base());
			} else {
				j["kind"] = "rail";
			}
			j["owner"] = GetTileOwner(t).base();
			break;

		case MP_ROAD:
			if (IsRoadDepotTile(t)) {
				j["kind"] = "depot";
				j["vehicleType"] = static_cast<int>(GetDepotVehicleType(t)); /* VEH_ROAD = 1 */
				j["id"] = static_cast<int>(GetDepotIndex(t).base());
			} else {
				j["kind"] = "road";
			}
			j["owner"] = GetTileOwner(t).base();
			break;

		case MP_HOUSE:
			j["kind"] = "house";
			j["id"] = static_cast<int>(GetTownIndex(t).base()); /* no owner: MP_HOUSE */
			break;

		case MP_STATION:
			if (IsHangarTile(t)) {
				j["kind"] = "depot";
				j["vehicleType"] = static_cast<int>(GetDepotVehicleType(t)); /* VEH_AIRCRAFT = 3 */
			} else {
				j["kind"] = "station";
			}
			j["id"] = static_cast<int>(GetStationIndex(t).base());
			j["owner"] = GetTileOwner(t).base();
			break;

		case MP_WATER:
			if (IsShipDepotTile(t)) {
				j["kind"] = "depot";
				j["vehicleType"] = static_cast<int>(GetDepotVehicleType(t)); /* VEH_SHIP = 2 */
				j["id"] = static_cast<int>(GetDepotIndex(t).base());
			} else {
				j["kind"] = "water";
			}
			j["owner"] = GetTileOwner(t).base();
			break;

		case MP_INDUSTRY:
			j["kind"] = "industry";
			j["id"] = static_cast<int>(GetIndustryIndex(t).base()); /* no owner: MP_INDUSTRY */
			break;

		case MP_TUNNELBRIDGE:
			j["kind"] = "tunnelbridge";
			j["owner"] = GetTileOwner(t).base();
			break;

		case MP_OBJECT:
			j["kind"] = "object";
			j["owner"] = GetTileOwner(t).base();
			break;

		case MP_VOID:
		default:
			j["kind"] = "void";
			break;
	}

	return dump(j);
}

/**
 * Wave 10 — rename a station. Empty name resets to the default.
 * station_cmd.h:32 CmdRenameStation.
 * @return {ok, error}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_rename_station(int station_id, const char *name)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!SctCanBuild()) {
		return dump({{"ok", false}, {"error", "not in game"}});
	}

	const StationID sid{static_cast<uint16_t>(station_id)};
	if (!Station::IsValidID(sid)) {
		return dump({{"ok", false}, {"error", "invalid station"}});
	}

	AutoRestoreBackup backup(_current_company, _local_company);

	CommandCost cost = Command<CMD_RENAME_STATION>::Do(
			DoCommandFlag::Execute,
			sid,
			std::string{name == nullptr ? "" : name});
	return dump(SctOkResult(cost));
}

/**
 * Wave 13 — configure engine autoreplace for a group.
 * autoreplace_cmd.h CmdSetAutoReplace(id_g, old_engine, new_engine, when_old).
 * groupId < 0 → ALL_GROUP (all vehicles). toEngine < 0 → EngineID::Invalid(),
 * which stops replacing fromEngine. replaceWhenOld != 0 → only replace when old.
 * @return {ok, error}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_autoreplace(int group_id, int from_engine, int to_engine, int replace_when_old)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!SctCanBuild()) {
		return dump({{"ok", false}, {"error", "not in game"}});
	}

	AutoRestoreBackup backup(_current_company, _local_company);

	const GroupID gid = (group_id < 0) ? ALL_GROUP : GroupID{static_cast<uint16_t>(group_id)};
	const EngineID from{static_cast<uint16_t>(from_engine)};
	const EngineID to = (to_engine < 0)
			? EngineID::Invalid()
			: EngineID{static_cast<uint16_t>(to_engine)};

	CommandCost cost = Command<CMD_SET_AUTOREPLACE>::Do(
			DoCommandFlag::Execute, gid, from, to, replace_when_old != 0);
	return dump(SctOkResult(cost));
}

/**
 * Wave 13 — accept an engine's introductory preview offer.
 * accept != 0 → engine_cmd.h CmdWantEnginePreview(engineId); accept == 0 is a
 * decline, which is a pure no-op (the offer simply lapses).
 * @return {ok, error}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_engine_preview(int engine_id, int accept)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!SctCanBuild()) {
		return dump({{"ok", false}, {"error", "not in game"}});
	}
	if (accept == 0) {
		return dump({{"ok", true}, {"error", std::string{}}});
	}

	AutoRestoreBackup backup(_current_company, _local_company);

	CommandCost cost = Command<CMD_WANT_ENGINE_PREVIEW>::Do(
			DoCommandFlag::Execute, EngineID{static_cast<uint16_t>(engine_id)});
	return dump(SctOkResult(cost));
}

/**
 * Wave 13 — set a vehicle's servicing interval.
 * vehicle_cmd.h CmdChangeServiceInt(veh_id, serv_int, is_custom, is_percent).
 * Always a custom interval (is_custom=true); isPercent != 0 treats the value as a
 * reliability percentage rather than a day count (vehicle_gui.cpp:2780).
 * @return {ok, error}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_service_interval(int vehicle_id, int interval, int is_percent)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!SctCanBuild()) {
		return dump({{"ok", false}, {"error", "not in game"}});
	}

	const VehicleID vid{static_cast<uint32_t>(vehicle_id)};
	if (!Vehicle::IsValidID(vid)) {
		return dump({{"ok", false}, {"error", "invalid vehicle"}});
	}

	AutoRestoreBackup backup(_current_company, _local_company);

	CommandCost cost = Command<CMD_CHANGE_SERVICE_INT>::Do(
			DoCommandFlag::Execute, vid, static_cast<uint16_t>(interval),
			true, is_percent != 0);
	return dump(SctOkResult(cost));
}

/**
 * Wave 13 — rename a town. Empty name resets to the default.
 * town_cmd.h CmdRenameTown(town_id, text).
 * @return {ok, error}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_rename_town(int town_id, const char *name)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!SctCanBuild()) {
		return dump({{"ok", false}, {"error", "not in game"}});
	}

	const TownID tid{static_cast<uint16_t>(town_id)};
	if (!Town::IsValidID(tid)) {
		return dump({{"ok", false}, {"error", "invalid town"}});
	}

	AutoRestoreBackup backup(_current_company, _local_company);

	CommandCost cost = Command<CMD_RENAME_TOWN>::Do(
			DoCommandFlag::Execute, tid, std::string{name == nullptr ? "" : name});
	return dump(SctOkResult(cost));
}

/**
 * Wave 13 — rename a waypoint. Waypoints live in the station pool, so the id is a
 * StationID. Empty name resets to the default. waypoint_cmd.h CmdRenameWaypoint.
 * @return {ok, error}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_rename_waypoint(int waypoint_id, const char *name)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!SctCanBuild()) {
		return dump({{"ok", false}, {"error", "not in game"}});
	}

	AutoRestoreBackup backup(_current_company, _local_company);

	CommandCost cost = Command<CMD_RENAME_WAYPOINT>::Do(
			DoCommandFlag::Execute, StationID{static_cast<uint16_t>(waypoint_id)},
			std::string{name == nullptr ? "" : name});
	return dump(SctOkResult(cost));
}

/**
 * Wave 13 — sign management. cmdKind: 0 place a sign at tile `tileOrId` with `name`
 * (signs_cmd.h CmdPlaceSign, returns the new SignID); 1 rename sign `tileOrId` to
 * `name` (CmdRenameSign); 2 delete sign `tileOrId` (CmdRenameSign with an empty
 * string — signs_cmd.cpp:85 treats an empty name as delete). Signs are placed as
 * the local company so the company can later edit/delete them (CompanyCanEditSign).
 * @return {ok, error, id?}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_sign(int cmd_kind, int tile_or_id, const char *name)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!SctCanBuild()) {
		return dump({{"ok", false}, {"error", "not in game"}});
	}

	AutoRestoreBackup backup(_current_company, _local_company);

	if (cmd_kind == 0) {
		auto res = Command<CMD_PLACE_SIGN>::Do(
				DoCommandFlag::Execute, TileIndex{static_cast<uint32_t>(tile_or_id)},
				std::string{name == nullptr ? "" : name});
		const CommandCost &cost = std::get<0>(res);
		const SignID sid = std::get<1>(res);
		const bool ok = cost.Succeeded() && sid != SignID::Invalid();
		return dump({
			{"ok", ok},
			{"error", ok ? std::string{} : SctErrorFromCost(cost)},
			{"id", ok ? static_cast<int>(sid.base()) : -1},
		});
	}

	/* Rename (cmd_kind 1) or delete (cmd_kind 2 → empty name). */
	const std::string text = (cmd_kind == 2) ? std::string{} : std::string{name == nullptr ? "" : name};
	CommandCost cost = Command<CMD_RENAME_SIGN>::Do(
			DoCommandFlag::Execute, SignID{static_cast<uint16_t>(tile_or_id)}, text);
	return dump(SctOkResult(cost));
}

/**
 * Wave 13 — found a new town at `tile`. size 0 small / 1 medium / 2 large. The town
 * uses the game's configured layout (town_cmd.h CmdFoundTown) and an auto-generated
 * unique name. Runs as the local company (the in-game "Found new town" path), so it
 * obeys the economy.found_town setting — large towns and random placement are only
 * allowed when that setting permits (else the command returns a surfaced error).
 * @return {ok, error, id?, cost?}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_found_town(int tile, int size, int city_layout)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	/* Wave 22 — founding a town is a legitimate scenario-editor action, so use the
	 * editor-aware gate. In the editor this runs as OWNER_NONE (SctActingCompany),
	 * matching the SE found-town tool; in a normal game it stays the local company
	 * (a player-founded town, obeying economy.found_town). */
	if (!SctCanEditLandscape()) {
		return dump({{"ok", false}, {"error", "not in game"}, {"id", -1}, {"cost", 0}});
	}

	const TileIndex t{static_cast<uint32_t>(tile)};
	if (!IsValidTile(t)) {
		return dump({{"ok", false}, {"error", "invalid tile"}, {"id", -1}, {"cost", 0}});
	}

	AutoRestoreBackup backup(_current_company, SctActingCompany());

	const TownSize ts = (size >= 0 && size < TSZ_END) ? static_cast<TownSize>(size) : TSZ_SMALL;
	/* Use the configured layout so the player-founding layout check passes; city_layout
	 * is accepted for forward-compat but the game setting governs the actual road grid. */
	(void)city_layout;
	const TownLayout layout = _settings_game.economy.town_layout;

	/* Empty text → CmdFoundTown needs a unique auto name in townnameparts. */
	uint32_t townnameparts = 0;
	(void)GenerateTownName(_interactive_random, &townnameparts);

	auto res = Command<CMD_FOUND_TOWN>::Do(
			DoCommandFlag::Execute, t, ts, false, layout, false, townnameparts, std::string{});
	const CommandCost &cost = std::get<0>(res);
	const TownID tid = std::get<2>(res);
	const bool ok = cost.Succeeded();
	return dump({
		{"ok", ok},
		{"error", ok ? std::string{} : SctErrorFromCost(cost)},
		{"id", ok ? static_cast<int>(tid.base()) : -1},
		{"cost", static_cast<int64_t>(std::get<1>(res))},
	});
}

/**
 * Wave 13 — build/fund an industry. prospect != 0 uses the prospecting variant
 * (industry_cmd.h CmdBuildIndustry, run as OWNER_DEITY so the deity-prospect path
 * places it somewhere on the map — `tile` is ignored); prospect == 0 funds the
 * industry at `tile` as the local company (fund=true, matching the GUI Fund button).
 * @return {ok, error, cost?}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_fund_industry(int tile, int industry_type, int prospect)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	/* Wave 22 — building an industry is a legitimate scenario-editor action. */
	if (!SctCanEditLandscape()) {
		return dump({{"ok", false}, {"error", "not in game"}, {"cost", 0}});
	}

	const IndustryType it = static_cast<IndustryType>(industry_type);
	const uint32_t seed = _interactive_random.Next();

	/* Wave 22 — in the scenario editor, mirror the SE "build industry" tool
	 * (industry_gui.cpp:708-720): act as OWNER_NONE with _generating_world and
	 * _ignore_industry_restrictions set so the normal placement restrictions are
	 * bypassed, then build (fund=false) at the clicked tile with a random layout. The
	 * prospect flag is a normal-game deity feature and is ignored in the editor. */
	if (_game_mode == GM_EDITOR) {
		const TileIndex t{static_cast<uint32_t>(tile)};
		if (!IsValidTile(t)) {
			return dump({{"ok", false}, {"error", "invalid tile"}, {"cost", 0}});
		}
		const IndustrySpec *indsp = GetIndustrySpec(it);
		const uint32_t layout = (indsp != nullptr && !indsp->layouts.empty())
				? (_interactive_random.Next() % static_cast<uint32_t>(indsp->layouts.size()))
				: 0;
		AutoRestoreBackup backup_company(_current_company, OWNER_NONE);
		AutoRestoreBackup backup_gw(_generating_world, true);
		AutoRestoreBackup backup_ir(_ignore_industry_restrictions, true);
		CommandCost cost = Command<CMD_BUILD_INDUSTRY>::Do(
				DoCommandFlag::Execute, t, it, layout, false, seed);
		return dump(SctCostResult(cost));
	}

	if (prospect != 0) {
		/* Deity prospecting (industry_cmd.cpp:2089): fund=false as OWNER_DEITY, tile
		 * ignored. CMD_BUILD_INDUSTRY carries CommandFlag::Deity so this is allowed. */
		AutoRestoreBackup backup(_current_company, OWNER_DEITY);
		CommandCost cost = Command<CMD_BUILD_INDUSTRY>::Do(
				DoCommandFlag::Execute, TileIndex{}, it,
				static_cast<uint32_t>(0), false, seed);
		return dump(SctCostResult(cost));
	}

	AutoRestoreBackup backup(_current_company, _local_company);
	CommandCost cost = Command<CMD_BUILD_INDUSTRY>::Do(
			DoCommandFlag::Execute, TileIndex{static_cast<uint32_t>(tile)}, it,
			static_cast<uint32_t>(0), true, seed);
	return dump(SctCostResult(cost));
}

/**
 * Wave 13 — perform a local-authority action on a town (town_cmd.h CmdDoTownAction).
 * action 0-7: 0 small ad, 1 medium ad, 2 large ad, 3 road rebuild, 4 statue,
 * 5 fund buildings, 6 buy exclusive rights, 7 bribe (town.h TownAction).
 * @return {ok, error}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_town_action(int town_id, int action)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!SctCanBuild()) {
		return dump({{"ok", false}, {"error", "not in game"}});
	}

	const TownID tid{static_cast<uint16_t>(town_id)};
	if (!Town::IsValidID(tid)) {
		return dump({{"ok", false}, {"error", "invalid town"}});
	}
	if (action < 0 || action >= static_cast<int>(TownAction::End)) {
		return dump({{"ok", false}, {"error", "invalid action"}});
	}

	AutoRestoreBackup backup(_current_company, _local_company);

	CommandCost cost = Command<CMD_DO_TOWN_ACTION>::Do(
			DoCommandFlag::Execute, tid, static_cast<TownAction>(action));
	return dump(SctOkResult(cost));
}

/**
 * Wave 13 — centre the main viewport on a vehicle's current position (not a Command).
 * Vehicle::GetIfValid → ScrollMainWindowTo(x_pos, y_pos, z_pos) (viewport_func.h:80).
 * @return {ok}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_scroll_to_vehicle(int vehicle_id)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!SctInGame() || !Map::IsInitialized()) {
		return dump({{"ok", false}});
	}

	const Vehicle *v = Vehicle::GetIfValid(vehicle_id);
	if (v == nullptr) {
		return dump({{"ok", false}});
	}

	bool ok = ScrollMainWindowTo(v->x_pos, v->y_pos, v->z_pos, true);
	return dump({{"ok", ok}});
}

/**
 * Wave 14 — set an order's timetabled wait (field 0) or travel (field 1) time.
 * field maps to ModifyTimetableFlags (order_type.h:186): 0 → MTF_WAIT_TIME,
 * 1 → MTF_TRAVEL_TIME. ticks is clamped to the uint16_t data field. Setting 0
 * clears the timetabled value for that leg. timetable_cmd.h:18
 * CmdChangeTimetable(veh, order_number, mtf, data).
 * @return {ok, error}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_timetable(int vehicle_id, int order_index, int field, int ticks)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!SctCanBuild()) {
		return dump({{"ok", false}, {"error", "not in game"}});
	}

	const VehicleID vid{static_cast<uint32_t>(vehicle_id)};
	if (!Vehicle::IsValidID(vid)) {
		return dump({{"ok", false}, {"error", "invalid vehicle"}});
	}

	ModifyTimetableFlags mtf;
	if (field == 0) {
		mtf = MTF_WAIT_TIME;
	} else if (field == 1) {
		mtf = MTF_TRAVEL_TIME;
	} else {
		return dump({{"ok", false}, {"error", "invalid field"}});
	}

	const uint16_t data = static_cast<uint16_t>(std::clamp(ticks, 0, 0xFFFF));

	AutoRestoreBackup backup(_current_company, _local_company);

	CommandCost cost = Command<CMD_CHANGE_TIMETABLE>::Do(
			DoCommandFlag::Execute, vid, static_cast<VehicleOrderID>(order_index), mtf, data);
	return dump(SctOkResult(cost));
}

/**
 * Wave 14 — toggle autofill of a vehicle's timetable. on != 0 enables autofill;
 * preserve-wait-time is fixed false. timetable_cmd.h:21
 * CmdAutofillTimetable(veh, autofill, preserve_wait_time).
 * @return {ok, error}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_autofill_timetable(int vehicle_id, int on)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!SctCanBuild()) {
		return dump({{"ok", false}, {"error", "not in game"}});
	}

	const VehicleID vid{static_cast<uint32_t>(vehicle_id)};
	if (!Vehicle::IsValidID(vid)) {
		return dump({{"ok", false}, {"error", "invalid vehicle"}});
	}

	AutoRestoreBackup backup(_current_company, _local_company);

	CommandCost cost = Command<CMD_AUTOFILL_TIMETABLE>::Do(
			DoCommandFlag::Execute, vid, on != 0, false);
	return dump(SctOkResult(cost));
}

/**
 * Wave 14 — set the timetable start to ticksFromNow ticks from the current tick.
 * start_tick = TimerGameTick::counter + ticksFromNow (timer_game_tick.h:60), so a
 * negative offset starts it in the past (immediately). timetable_all is false —
 * this vehicle only. timetable_cmd.h:22
 * CmdSetTimetableStart(veh_id, timetable_all, start_tick).
 * @return {ok, error}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_timetable_start(int vehicle_id, int ticks_from_now)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!SctCanBuild()) {
		return dump({{"ok", false}, {"error", "not in game"}});
	}

	const VehicleID vid{static_cast<uint32_t>(vehicle_id)};
	if (!Vehicle::IsValidID(vid)) {
		return dump({{"ok", false}, {"error", "invalid vehicle"}});
	}

	AutoRestoreBackup backup(_current_company, _local_company);

	const int64_t base_tick = static_cast<int64_t>(TimerGameTick::counter);
	const int64_t target = std::max<int64_t>(0, base_tick + ticks_from_now);
	const TimerGameTick::TickCounter start_tick = static_cast<TimerGameTick::TickCounter>(target);

	CommandCost cost = Command<CMD_SET_TIMETABLE_START>::Do(
			DoCommandFlag::Execute, vid, false, start_tick);
	return dump(SctOkResult(cost));
}

/**
 * Wave 16 — zoom the main viewport (mirrors the toolbar zoom buttons).
 * dir > 0 zooms in, dir < 0 zooms out, dir == 0 is a no-op. Runs
 * DoZoomInOutWindow(ZOOM_IN/ZOOM_OUT, GetMainWindow()) (viewport_func.h:33; the
 * exact call toolbar_gui.cpp:860/870 makes). `ok` reflects whether the zoom level
 * actually changed (false when already at the min/max zoom limit).
 * @return {ok}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_zoom(int dir)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!SctInGame() || !Map::IsInitialized()) {
		return dump({{"ok", false}});
	}
	if (dir == 0) {
		return dump({{"ok", true}});
	}

	Window *w = GetMainWindow();
	if (w == nullptr || w->viewport == nullptr) {
		return dump({{"ok", false}});
	}

	const bool ok = DoZoomInOutWindow(dir > 0 ? ZOOM_IN : ZOOM_OUT, w);
	return dump({{"ok", ok}});
}

/**
 * Wave 16 — most-recent console backlog lines, oldest -> newest, capped at
 * maxLines. Reads the in-game console's `_iconsole_buffer` (console_gui.cpp) via
 * the SctGetConsoleBacklog accessor (console_func.h); that buffer holds the newest
 * line at index 0, so the accessor reverses the kept slice.
 * @return JSON array of strings (possibly empty).
 */
const char *EMSCRIPTEN_KEEPALIVE sct_console_output(int max_lines)
{
	static std::string buffer;

	std::vector<std::string> lines;
	SctGetConsoleBacklog(max_lines, lines);

	nlohmann::json arr = nlohmann::json::array();
	for (const std::string &s : lines) arr.push_back(s);

	buffer = arr.dump();
	return buffer.c_str();
}

/**
 * Wave 16 — rename a vehicle group. Empty name resets to the default.
 * group_cmd.h CmdAlterGroup(flags, AlterGroupMode::Rename, group_id, parent, text).
 * @return {ok, error}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_rename_group(int group_id, const char *name)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!SctCanBuild()) {
		return dump({{"ok", false}, {"error", "not in game"}});
	}

	const GroupID gid{static_cast<uint16_t>(group_id)};
	if (!Group::IsValidID(gid)) {
		return dump({{"ok", false}, {"error", "invalid group"}});
	}

	AutoRestoreBackup backup(_current_company, _local_company);

	CommandCost cost = Command<CMD_ALTER_GROUP>::Do(
			DoCommandFlag::Execute, AlterGroupMode::Rename, gid, GroupID::Invalid(),
			std::string{name == nullptr ? "" : name});
	return dump(SctOkResult(cost));
}

/**
 * Wave 16 — delete a vehicle group. group_cmd.h CmdDeleteGroup(flags, group_id).
 * Vehicles in the group move back to the default group (engine behaviour).
 * @return {ok, error}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_delete_group(int group_id)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!SctCanBuild()) {
		return dump({{"ok", false}, {"error", "not in game"}});
	}

	const GroupID gid{static_cast<uint16_t>(group_id)};
	if (!Group::IsValidID(gid)) {
		return dump({{"ok", false}, {"error", "invalid group"}});
	}

	AutoRestoreBackup backup(_current_company, _local_company);

	CommandCost cost = Command<CMD_DELETE_GROUP>::Do(DoCommandFlag::Execute, gid);
	return dump(SctOkResult(cost));
}

/**
 * Wave 16 — toggle a group's autoreplace-protection flag. protect != 0 sets it
 * (global autoreplace no longer touches the group), == 0 clears it. group_cmd.h
 * CmdSetGroupFlag(flags, group_id, GroupFlag::ReplaceProtection, value, recursive);
 * recursive is fixed false (this group only, matching the non-ctrl GUI click,
 * group_gui.cpp:907).
 * @return {ok, error}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_protect_group(int group_id, int protect)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!SctCanBuild()) {
		return dump({{"ok", false}, {"error", "not in game"}});
	}

	const GroupID gid{static_cast<uint16_t>(group_id)};
	if (!Group::IsValidID(gid)) {
		return dump({{"ok", false}, {"error", "invalid group"}});
	}

	AutoRestoreBackup backup(_current_company, _local_company);

	CommandCost cost = Command<CMD_SET_GROUP_FLAG>::Do(
			DoCommandFlag::Execute, gid, GroupFlag::ReplaceProtection, protect != 0, false);
	return dump(SctOkResult(cost));
}

/**
 * Wave 16 — buy a single tile of land (the "purchase land" tool). In 15.3 owned
 * land is an object, so this is object_cmd.h CmdBuildObject(flags, tile,
 * OBJECT_OWNED_LAND, view=0) (object_type.h:21; terraform_gui.cpp:283 uses the
 * area variant, this is the single-tile case). Runs as the local company.
 * @return {ok, error, cost}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_buy_land(int tile)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!SctCanBuild()) {
		return dump({{"ok", false}, {"error", "not in game"}, {"cost", 0}});
	}

	const TileIndex t{static_cast<uint32_t>(tile)};
	if (!IsValidTile(t)) {
		return dump({{"ok", false}, {"error", "invalid tile"}, {"cost", 0}});
	}

	AutoRestoreBackup backup(_current_company, _local_company);

	CommandCost cost = Command<CMD_BUILD_OBJECT>::Do(
			DoCommandFlag::Execute, t, OBJECT_OWNED_LAND, static_cast<uint8_t>(0));
	return dump(SctCostResult(cost));
}

/**
 * Wave 16 — apply the year part of the change-date cheat. Replicates the essential
 * half of cheat_gui.cpp:107 ClickChangeDateCheat: clamp the year, move the calendar
 * date (keeping month/day), keep the economy date in sync when not using wallclock
 * units (shifting cached vehicle / link-graph dates first), then refresh engine
 * availability and signal-variant caches. The native-window invalidations from the
 * GUI path are intentionally omitted (React owns the UI in this fork).
 */
static void SctChangeDateCheatYear(int year)
{
	const TimerGameCalendar::Year new_year = Clamp(
			TimerGameCalendar::Year(year), CalendarTime::MIN_YEAR, CalendarTime::MAX_YEAR);
	if (new_year == TimerGameCalendar::year) return;

	TimerGameCalendar::YearMonthDay ymd = TimerGameCalendar::ConvertDateToYMD(TimerGameCalendar::date);
	const TimerGameCalendar::Date new_calendar_date =
			TimerGameCalendar::ConvertYMDToDate(new_year, ymd.month, ymd.day);
	TimerGameCalendar::SetDate(new_calendar_date, TimerGameCalendar::date_fract);

	if (!TimerGameEconomy::UsingWallclockUnits()) {
		const TimerGameEconomy::Date new_economy_date{new_calendar_date.base()};
		for (auto v : Vehicle::Iterate()) v->ShiftDates(new_economy_date - TimerGameEconomy::date);
		LinkGraphSchedule::instance.ShiftDates(new_economy_date - TimerGameEconomy::date);
		TimerGameEconomy::SetDate(new_economy_date, TimerGameEconomy::date_fract);
	}

	CalendarEnginesMonthlyLoop();
	ResetSignalVariant();
}

/**
 * Wave 16 — apply a cheat. kind:
 *   "money"           → misc_cmd.h CMD_MONEY_CHEAT with a (Money)value delta.
 *   "magic_bulldozer" → _cheats.magic_bulldozer.value = value != 0 (dynamite anything).
 *   "crossing_tunnels"→ _cheats.crossing_tunnels.value = value != 0.
 *   "no_jetcrash"     → _cheats.no_jetcrash.value = value != 0.
 *   "date"            → change the calendar year to (int)value (see SctChangeDateCheatYear).
 * Bool cheats also mark been_used, exactly like the cheat GUI. Unknown kind → error.
 * @return {ok, error}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_cheat(const char *kind, double value)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (kind == nullptr) {
		return dump({{"ok", false}, {"error", "unknown cheat"}});
	}
	if (!SctInGame()) {
		return dump({{"ok", false}, {"error", "not in game"}});
	}

	if (std::strcmp(kind, "money") == 0) {
		if (!Company::IsValidID(_local_company)) {
			return dump({{"ok", false}, {"error", "no company"}});
		}
		AutoRestoreBackup backup(_current_company, _local_company);
		_cheats.money.been_used = true;
		CommandCost cost = Command<CMD_MONEY_CHEAT>::Do(
				DoCommandFlag::Execute, static_cast<Money>(value));
		return dump(SctOkResult(cost));
	}

	if (std::strcmp(kind, "magic_bulldozer") == 0) {
		_cheats.magic_bulldozer.value = (value != 0);
		_cheats.magic_bulldozer.been_used = true;
		return dump({{"ok", true}, {"error", std::string{}}});
	}
	if (std::strcmp(kind, "crossing_tunnels") == 0) {
		_cheats.crossing_tunnels.value = (value != 0);
		_cheats.crossing_tunnels.been_used = true;
		return dump({{"ok", true}, {"error", std::string{}}});
	}
	if (std::strcmp(kind, "no_jetcrash") == 0) {
		_cheats.no_jetcrash.value = (value != 0);
		_cheats.no_jetcrash.been_used = true;
		return dump({{"ok", true}, {"error", std::string{}}});
	}
	if (std::strcmp(kind, "date") == 0) {
		_cheats.change_date.been_used = true;
		SctChangeDateCheatYear(static_cast<int>(value));
		return dump({{"ok", true}, {"error", std::string{}}});
	}

	return dump({{"ok", false}, {"error", "unknown cheat"}});
}

/**
 * Wave 16 — set or clear a transparency option bit in _transparency_opt
 * (transparency.h TransparencyOption). option strings map: "signs"→TO_SIGNS,
 * "trees"→TO_TREES, "houses"→TO_HOUSES, "industries"→TO_INDUSTRIES,
 * "buildings"→TO_BUILDINGS, "bridges"→TO_BRIDGES, "structures"→TO_STRUCTURES,
 * "catenary"→TO_CATENARY, "loading"→TO_TEXT (loading + cost/income text). on != 0
 * sets the bit, == 0 clears it; then MarkWholeScreenDirty() so the change shows.
 * Unknown option → {ok:false}.
 * @return {ok}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_set_transparency(const char *option, int on)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (option == nullptr) {
		return dump({{"ok", false}});
	}

	TransparencyOption to = TO_INVALID;
	if (std::strcmp(option, "signs") == 0) to = TO_SIGNS;
	else if (std::strcmp(option, "trees") == 0) to = TO_TREES;
	else if (std::strcmp(option, "houses") == 0) to = TO_HOUSES;
	else if (std::strcmp(option, "industries") == 0) to = TO_INDUSTRIES;
	else if (std::strcmp(option, "buildings") == 0) to = TO_BUILDINGS;
	else if (std::strcmp(option, "bridges") == 0) to = TO_BRIDGES;
	else if (std::strcmp(option, "structures") == 0) to = TO_STRUCTURES;
	else if (std::strcmp(option, "catenary") == 0) to = TO_CATENARY;
	else if (std::strcmp(option, "loading") == 0) to = TO_TEXT;

	if (to == TO_INVALID) {
		return dump({{"ok", false}});
	}

	if (on != 0) {
		SetBit(_transparency_opt, to);
	} else {
		ClrBit(_transparency_opt, to);
	}
	MarkWholeScreenDirty();

	return dump({{"ok", true}});
}

/**
 * Wave 16 — detail for one town (town.h Town). Fields are the cheaply-cached ones:
 *   population   cache.population
 *   houses       cache.num_houses
 *   growthRate   growth_rate ticks between house grows; null when growth is disabled
 *                (TOWN_GROWTH_RATE_NONE)
 *   rating       local company's town rating (int16), or null if it has none yet
 *   passMax      max passengers delivered to the town last month (received[TAE_PASSENGERS].old_max)
 *   mailMax      max mail delivered to the town last month (received[TAE_MAIL].old_max)
 *   cargoAccepted per-town-effect delivery stats with a non-zero last-month max:
 *                [{effect:<TownAcceptanceEffect 1..5>, max:<old_max>, transported:<old_act>}]
 * Note: this fork's Town keeps production in a `supplied` vector keyed by cargo and
 * deliveries in the TAE-indexed `received` array; passMax/mailMax/cargoAccepted are
 * read from `received` (cargo delivered to the town) — the cheap, exact data.
 * @return JSON object, or the literal "null" for an invalid town.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_town_detail(int town_id)
{
	static std::string buffer;

	const Town *t = Town::GetIfValid(town_id);
	if (t == nullptr) {
		buffer = "null";
		return buffer.c_str();
	}

	nlohmann::json j;
	j["population"] = t->cache.population;
	j["houses"] = t->cache.num_houses;
	j["growthRate"] = (t->growth_rate == TOWN_GROWTH_RATE_NONE)
			? nlohmann::json(nullptr)
			: nlohmann::json(t->growth_rate);

	if (Company::IsValidID(_local_company) && t->have_ratings.Test(_local_company)) {
		j["rating"] = t->ratings[_local_company];
	} else {
		j["rating"] = nullptr;
	}

	j["passMax"] = t->received[TAE_PASSENGERS].old_max;
	j["mailMax"] = t->received[TAE_MAIL].old_max;

	nlohmann::json accepted = nlohmann::json::array();
	for (int tae = TAE_BEGIN + 1; tae < TAE_END; tae++) {
		const auto &stat = t->received[tae];
		if (stat.old_max == 0) continue;
		accepted.push_back({
			{"effect", tae},
			{"max", stat.old_max},
			{"transported", stat.old_act},
		});
	}
	j["cargoAccepted"] = std::move(accepted);

	buffer = j.dump();
	return buffer.c_str();
}

/**
 * Wave 16 — detail for one industry (industry.h Industry). Shape:
 *   {type, typeName?, produced:[{cargo, lastMonthProduction, lastMonthTransported}],
 *    accepts:[cargo, ...]}
 * `produced` iterates the industry's produced-cargo slots (each with a 25-month
 * HistoryData; LAST_MONTH is the previous month). `accepts` lists the accepted
 * cargo-type ids. Invalid/empty cargo slots are skipped.
 * @return JSON object, or the literal "null" for an invalid industry.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_industry_detail(int industry_id)
{
	static std::string buffer;

	const Industry *ind = Industry::GetIfValid(industry_id);
	if (ind == nullptr) {
		buffer = "null";
		return buffer.c_str();
	}

	nlohmann::json j;
	j["type"] = ind->type;

	const IndustrySpec *spec = GetIndustrySpec(ind->type);
	if (spec != nullptr && spec->name != STR_NULL && spec->name != INVALID_STRING_ID) {
		j["typeName"] = GetString(spec->name);
	}

	nlohmann::json produced = nlohmann::json::array();
	for (const auto &p : ind->produced) {
		if (!IsValidCargoType(p.cargo)) continue;
		produced.push_back({
			{"cargo", static_cast<int>(p.cargo)},
			{"lastMonthProduction", p.history[LAST_MONTH].production},
			{"lastMonthTransported", p.history[LAST_MONTH].transported},
		});
	}
	j["produced"] = std::move(produced);

	nlohmann::json accepts = nlohmann::json::array();
	for (const auto &a : ind->accepted) {
		if (!IsValidCargoType(a.cargo)) continue;
		accepts.push_back(static_cast<int>(a.cargo));
	}
	j["accepts"] = std::move(accepts);

	buffer = j.dump();
	return buffer.c_str();
}

/**
 * Stash build defaults read by sct_build.
 * Keys: "railtype", "roadtype" (used when p1 == 0), "stationjoin" (Wave 21 — the
 * next rail_station placement's join semantics: -1 fresh / 0 adjacent / >0 join
 * that StationID; consumed and reset to -1 by the build).
 */
void EMSCRIPTEN_KEEPALIVE sct_set_build_param(const char *key, int value)
{
	if (key == nullptr) return;
	/* Case-insensitive: the JS side sends camelCase ("railType"). */
	if (strcasecmp(key, "railtype") == 0) {
		g_sct_railtype = value;
	} else if (strcasecmp(key, "roadtype") == 0) {
		g_sct_roadtype = value;
	} else if (strcasecmp(key, "stationjoin") == 0) {
		g_sct_station_join = value;
	}
}

/**
 * Debug toggle: re-enable the engine's own viewport-click windows
 * (station/town/industry/vehicle views). Off by default in this fork.
 */
void EMSCRIPTEN_KEEPALIVE sct_set_native_click(int on)
{
	_sct_native_viewport_windows = on != 0;
}

/**
 * Wave 18 — drive the network content service (BaNaNaS), mirroring the console
 * `content update|select|unselect|download` command (console_cmds.cpp ConContent).
 * All calls go through `_network_content_client` (network_content.h). No AutoRestore
 * backup: content is company-independent. Actions:
 *   "update"   → RequestContentList(CONTENT_TYPE_END) — fetch the full downloadable
 *                list (connects to the content server itself; must run before the
 *                list is populated, exactly like the console `content update`).
 *   "select"   → Select((ContentID)id).
 *   "unselect" → Unselect((ContentID)id).
 *   "download" → DownloadSelectedContent(files, bytes) — downloads everything
 *                currently selected (same call the console `content download` makes;
 *                no separate connect step — `update` already connected). Reports the
 *                queued file/byte counts.
 * @return {ok, error} (plus files/bytes on a successful "download").
 */
const char *EMSCRIPTEN_KEEPALIVE sct_content(const char *action, int id)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (action == nullptr) {
		return dump({{"ok", false}, {"error", "unknown action"}});
	}

	if (std::strcmp(action, "update") == 0) {
		/* CONTENT_TYPE_END = "all types", the console default (console_cmds.cpp). */
		_network_content_client.RequestContentList(CONTENT_TYPE_END);
		return dump({{"ok", true}, {"error", std::string{}}});
	}

	if (std::strcmp(action, "select") == 0) {
		_network_content_client.Select(static_cast<ContentID>(id));
		return dump({{"ok", true}, {"error", std::string{}}});
	}

	if (std::strcmp(action, "unselect") == 0) {
		_network_content_client.Unselect(static_cast<ContentID>(id));
		return dump({{"ok", true}, {"error", std::string{}}});
	}

	if (std::strcmp(action, "download") == 0) {
		uint files = 0;
		uint bytes = 0;
		_network_content_client.DownloadSelectedContent(files, bytes);
		return dump({
			{"ok", true},
			{"error", std::string{}},
			{"files", static_cast<int>(files)},
			{"bytes", static_cast<int64_t>(bytes)},
		});
	}

	return dump({{"ok", false}, {"error", "unknown action"}});
}

/**
 * Wave 18 — enter the scenario editor from the menu, the way the intro screen's
 * "Scenario Editor" button does (intro_gui.cpp WID_SGI_EDIT_SCENARIO →
 * StartScenarioEditor, genworld.h). StartScenarioEditor() runs the real GUI path
 * (genworld_gui.cpp StartGeneratingLandscape(GLWM_SCENARIO): close non-vital
 * windows, MakeNewgameSettingsLive, ResetGRFConfig, then `_switch_mode = SM_EDITOR`),
 * so on the next tick SwitchToMode builds a fresh editor world (openttd.cpp:1065
 * MakeNewEditorWorld) and the mode actually starts.
 * @return {ok}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_start_editor()
{
	static std::string buffer;
	StartScenarioEditor();
	nlohmann::json j = {{"ok", true}};
	buffer = j.dump();
	return buffer.c_str();
}

/**
 * Wave 21 — add (`on` != 0) or remove (`on` == 0) a NewGRF from the new-game config
 * (_grfconfig_newgame). `grfid_hex` is the 8-hex-char canonical grfid string this
 * bridge emits from query('newgrfAvailable') (i.e. std::byteswap of the stored
 * little-endian grfid). `md5_hex` is optional (nullptr / "" = match by grfid alone);
 * when given it must be the 32-char hex checksum to disambiguate multiple versions of
 * the same grfid.
 *
 * Add: find the matching GRFConfig in _all_grfs, deep-copy it the way the GRF
 * settings GUI does (std::make_unique<GRFConfig>(*src) + SetParameterDefaults(),
 * newgrf_gui.cpp:1486) and append via AppendToGRFConfigList(_grfconfig_newgame, …).
 * A no-op {ok:true} when an entry is already present. Remove: erase every matching
 * entry from _grfconfig_newgame.
 *
 * New games pick these up because the newgame path copies _grfconfig_newgame.
 * Cross-session persistence is via the normal SaveConfig path — settings.cpp:1522
 * serialises _grfconfig_newgame into the config's [newgrf] section — so no explicit
 * save is issued here (matching the GUI, which relies on the same save-on-exit path).
 *
 * @return {ok, error}. Not gated on being in-game: NewGRF config is a menu action.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_newgrf_select(const char *grfid_hex, const char *md5_hex, int on)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (grfid_hex == nullptr || grfid_hex[0] == '\0') {
		return dump({{"ok", false}, {"error", "missing grfid"}});
	}

	/* Parse the canonical display grfid back to the stored little-endian form. */
	const uint32_t display_grfid = static_cast<uint32_t>(std::strtoul(grfid_hex, nullptr, 16));
	const uint32_t grfid = std::byteswap(display_grfid);

	const bool have_md5 = (md5_hex != nullptr && md5_hex[0] != '\0');

	/* Locate the matching scanned GRFConfig. md5 (when supplied) is compared as the
	 * uppercase hex string this bridge emits, so no byte parsing is needed. */
	const GRFConfig *match = nullptr;
	for (const auto &c : _all_grfs) {
		if (c->ident.grfid != grfid) continue;
		if (have_md5 && !StrEqualsIgnoreCase(FormatArrayAsHex(c->ident.md5sum), md5_hex)) continue;
		match = c.get();
		break;
	}

	if (on != 0) {
		if (match == nullptr) {
			return dump({{"ok", false}, {"error", "grf not found"}});
		}
		/* Already present → idempotent success. */
		if (SctGrfInList(_grfconfig_newgame, grfid, have_md5 ? &match->ident.md5sum : nullptr)) {
			return dump({{"ok", true}, {"error", std::string{}}});
		}
		auto copy = std::make_unique<GRFConfig>(*match);
		copy->SetParameterDefaults();
		AppendToGRFConfigList(_grfconfig_newgame, std::move(copy));
		return dump({{"ok", true}, {"error", std::string{}}});
	}

	/* on == 0: remove every matching entry from the new-game config. */
	const MD5Hash *md5_filter = (have_md5 && match != nullptr) ? &match->ident.md5sum : nullptr;
	const size_t removed = std::erase_if(_grfconfig_newgame,
			[&](const std::unique_ptr<GRFConfig> &c) {
				return c->ident.HasGrfIdentifier(grfid, md5_filter);
			});
	if (removed == 0) {
		return dump({{"ok", false}, {"error", "not selected"}});
	}
	return dump({{"ok", true}, {"error", std::string{}}});
}

/**
 * Wave 22 — save the current editor map as a scenario. Sanitizes `name` into a bare
 * filename (path separators and other unsafe chars collapse to '_'), guarantees a
 * single ".scn" extension, then writes into the scenario directory via
 * SaveOrLoad(name, SLO_SAVE, DFT_GAME_FILE, SCENARIO_DIR, false) — the same call the
 * save GUI ultimately reaches (openttd.cpp SM_SAVE_GAME), with the scenario subdir and
 * .scn extension the SE save window would supply (fios.cpp FiosMakeSavegameName picks
 * .scn in GM_EDITOR). Editor-only: a scenario is an editor artefact. Runs unthreaded so
 * the caller can immediately persist IDBFS afterwards.
 * @return {ok, error, filename?}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_save_scenario(const char *name)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (_game_mode != GM_EDITOR) {
		return dump({{"ok", false}, {"error", "not in scenario editor"}});
	}
	if (name == nullptr || name[0] == '\0') {
		return dump({{"ok", false}, {"error", "empty name"}});
	}

	/* Sanitize into a bare filename: keep alphanumerics, space, dash, underscore and
	 * dot; collapse anything else (path separators included) to '_'. */
	std::string clean;
	for (const char *p = name; *p != '\0'; ++p) {
		const unsigned char c = static_cast<unsigned char>(*p);
		if (std::isalnum(c) || c == ' ' || c == '-' || c == '_' || c == '.') {
			clean.push_back(static_cast<char>(c));
		} else {
			clean.push_back('_');
		}
	}
	/* Trim leading/trailing spaces and dots so we never produce "." or a hidden name. */
	const size_t b = clean.find_first_not_of(" .");
	const size_t e = clean.find_last_not_of(" .");
	clean = (b == std::string::npos) ? std::string{} : clean.substr(b, e - b + 1);
	if (clean.empty()) clean = "scenario";

	/* Ensure exactly one ".scn" extension (case-insensitive). */
	if (clean.size() < 4 || !StrEqualsIgnoreCase(clean.substr(clean.size() - 4), ".scn")) {
		clean += ".scn";
	}

	/* Match the SE save-window pre-save step: refresh engine intro dates for the
	 * (possibly changed) editor date (fios_gui.cpp:849). */
	StartupEngines();

	const SaveOrLoadResult res = SaveOrLoad(clean, SLO_SAVE, DFT_GAME_FILE, SCENARIO_DIR, false);
	if (res != SL_OK) {
		return dump({{"ok", false}, {"error", GetSaveLoadErrorMessage().GetDecodedString()}});
	}
	return dump({{"ok", true}, {"error", std::string{}}, {"filename", clean}});
}

/**
 * Wave 22 — expand a town (editor-only), mirroring the SE town-view "Expand" button
 * which posts CMD_EXPAND_TOWN (town_cmd.h) with both expand modes
 * (Buildings|Roads). `cells` is the number of growth iterations: cells <= 0 uses the
 * engine's default one-click grow (grow_amount 0 → a house-count-scaled random burst,
 * exactly like the GUI button, town_cmd.cpp:3266); cells > 0 runs that many explicit
 * grow iterations. CMD_EXPAND_TOWN carries CommandFlag::Deity and is allowed in the
 * editor for any owner; we run it as OWNER_NONE to match the editor's local company.
 * @return {ok, error}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_expand_town(int town_id, int cells)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (_game_mode != GM_EDITOR) {
		return dump({{"ok", false}, {"error", "not in scenario editor"}});
	}

	const TownID tid{static_cast<uint16_t>(town_id)};
	if (!Town::IsValidID(tid)) {
		return dump({{"ok", false}, {"error", "invalid town"}});
	}

	const uint32_t grow_amount = (cells > 0) ? static_cast<uint32_t>(cells) : 0;

	AutoRestoreBackup backup(_current_company, OWNER_NONE);

	CommandCost cost = Command<CMD_EXPAND_TOWN>::Do(
			DoCommandFlag::Execute, tid, grow_amount,
			TownExpandModes{TownExpandMode::Buildings, TownExpandMode::Roads});
	return dump(SctOkResult(cost));
}

/**
 * Wave 22 — the catchment area of a station, for a coverage overlay.
 *
 * We return the catchment RECTANGLE corners (tile X/Y), NOT an enumeration of every
 * covered tile. Station::GetCatchmentRect() (station.cpp:366) is the authoritative
 * bounding box the engine itself computes — the station's spread rect grown by the
 * catchment radius and clamped to the map — while a full tile list can run to several
 * hundred indices for a large airport (radius up to MAX_CATCHMENT). The rect is compact
 * and is exactly what a box overlay needs; the four corners are also emitted as tile
 * indices (NW, NE, SW, SE) for convenience.
 *
 * @return JSON {radius, x0, y0, x1, y1, tiles:[nw,ne,sw,se]} in tile coordinates, or
 *         the literal "null" for an invalid station or one with no tiles yet.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_station_catchment(int station_id)
{
	static std::string buffer;

	if (!SctInGame() || !Map::IsInitialized()) {
		buffer = "null";
		return buffer.c_str();
	}

	const Station *st = Station::GetIfValid(station_id);
	if (st == nullptr || st->rect.IsEmpty()) {
		buffer = "null";
		return buffer.c_str();
	}

	const Rect r = st->GetCatchmentRect();
	const uint x0 = static_cast<uint>(r.left);
	const uint y0 = static_cast<uint>(r.top);
	const uint x1 = static_cast<uint>(r.right);
	const uint y1 = static_cast<uint>(r.bottom);

	nlohmann::json tiles = nlohmann::json::array();
	tiles.push_back(TileXY(x0, y0).base()); /* NW */
	tiles.push_back(TileXY(x1, y0).base()); /* NE */
	tiles.push_back(TileXY(x0, y1).base()); /* SW */
	tiles.push_back(TileXY(x1, y1).base()); /* SE */

	nlohmann::json j = {
		{"radius", st->GetCatchmentRadius()},
		{"x0", x0}, {"y0", y0},
		{"x1", x1}, {"y1", y1},
		{"tiles", std::move(tiles)},
	};
	buffer = j.dump();
	return buffer.c_str();
}

/**
 * Wave 26 — move a rail vehicle between consists (drag/drop in the depot editor).
 * train_cmd.h CmdMoveRailVehicle(flags, src_veh, dest_veh, move_chain). moveChain
 * != 0 moves src and everything behind it. destVeh < 0 → VehicleID::Invalid(),
 * which detaches src into a new free wagon chain in the same depot.
 * @return {ok, error, cost}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_move_rail_vehicle(int src_veh, int dest_veh, int move_chain)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!SctCanBuild()) {
		return dump({{"ok", false}, {"error", "not in game"}, {"cost", 0}});
	}

	const VehicleID src{static_cast<uint32_t>(src_veh)};
	if (!Vehicle::IsValidID(src)) {
		return dump({{"ok", false}, {"error", "invalid vehicle"}, {"cost", 0}});
	}
	const VehicleID dest = (dest_veh < 0)
			? VehicleID::Invalid()
			: VehicleID{static_cast<uint32_t>(dest_veh)};

	AutoRestoreBackup backup(_current_company, _local_company);

	CommandCost cost = Command<CMD_MOVE_RAIL_VEHICLE>::Do(
			DoCommandFlag::Execute, src, dest, move_chain != 0);
	return dump(SctCostResult(cost));
}

/**
 * Wave 26 — a vehicle's consist front→back, one entry per real unit (articulated
 * parts collapse into their engine via GetNextUnit). For a train, walks from the
 * front of the chain the given vehicle belongs to; any other vehicle type yields a
 * single entry. `wagon` is true for a non-motorised rail wagon; `cargo` is the unit's
 * current CargoType (-1 when none); `capacity` its cargo_cap; `count` is always 1.
 * @return JSON array, or "[]" for an invalid id.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_consist(int veh_id)
{
	static std::string buffer;

	const Vehicle *v = Vehicle::GetIfValid(veh_id);
	if (v == nullptr) {
		buffer = "[]";
		return buffer.c_str();
	}

	nlohmann::json arr = nlohmann::json::array();

	auto emit = [&](const Vehicle *u, bool wagon) {
		arr.push_back({
			{"id", static_cast<int>(u->index.base())},
			{"engine", static_cast<int>(u->engine_type.base())},
			{"name", GetString(STR_ENGINE_NAME, u->engine_type)},
			{"wagon", wagon},
			{"cargo", IsValidCargoType(u->cargo_type) ? static_cast<int>(u->cargo_type) : -1},
			{"capacity", static_cast<int>(u->cargo_cap)},
			{"count", 1},
		});
	};

	if (v->type == VEH_TRAIN) {
		for (const Train *u = Train::From(v)->First(); u != nullptr; u = u->GetNextUnit()) {
			emit(u, u->IsWagon());
		}
	} else {
		emit(v, false);
	}

	buffer = arr.dump();
	return buffer.c_str();
}

/**
 * Wave 26 — the trains parked in the rail depot at tile (x, y). vehiclelist.h
 * BuildDepotVehicleList(VEH_TRAIN, tile, &engines, &wagons) (depot_gui.cpp:737):
 * `vehicles` are the front-engine consists, `wagons` the free-wagon chains (each id
 * is the front of its chain).
 * @return JSON {vehicles:[id...], wagons:[id...]}, or the literal "null" when the
 *         tile is not a train depot.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_depot_vehicles(int x, int y)
{
	static std::string buffer;

	if (!SctInGame() || !Map::IsInitialized()) {
		buffer = "null";
		return buffer.c_str();
	}
	if (x < 0 || y < 0 || static_cast<uint>(x) > Map::MaxX() || static_cast<uint>(y) > Map::MaxY()) {
		buffer = "null";
		return buffer.c_str();
	}

	const TileIndex tile = TileXY(static_cast<uint>(x), static_cast<uint>(y));
	if (!IsRailDepotTile(tile)) {
		buffer = "null";
		return buffer.c_str();
	}

	VehicleList engines;
	VehicleList wagons;
	BuildDepotVehicleList(VEH_TRAIN, tile, &engines, &wagons);

	nlohmann::json veh = nlohmann::json::array();
	for (const Vehicle *e : engines) veh.push_back(static_cast<int>(e->index.base()));
	nlohmann::json wag = nlohmann::json::array();
	for (const Vehicle *w : wagons) wag.push_back(static_cast<int>(w->index.base()));

	nlohmann::json j = {{"vehicles", std::move(veh)}, {"wagons", std::move(wag)}};
	buffer = j.dump();
	return buffer.c_str();
}

/**
 * Wave 26 — build (or remove) signals along a straight rail drag from (x1,y1) to
 * (x2,y2). The drag is guaranteed axis- or diagonal-aligned by the UI; the track is
 * inferred with the shared rail-line snap helper (SctResolveRailDrag, auto-snap).
 * rail_cmd.h CmdBuildSignalTrack(flags, tile, end_tile, track, sigtype, sigvar,
 * mode, autofill, minimise_gaps, density) and CmdRemoveSignalTrack(flags, tile,
 * end_tile, track, autofill). sigtype is a SignalType int (signal_type.h:
 * 0=SIGTYPE_BLOCK, 1=ENTRY, 2=EXIT, 3=COMBO, 4=SIGTYPE_PBS, 5=SIGTYPE_PBS_ONEWAY);
 * out-of-range falls back to SIGTYPE_BLOCK. density is the tiles-between-signals
 * spacing (clamped 1..20; <=0 → 1). remove != 0 removes instead of building.
 * @return {ok, error, cost}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_build_signal_track(int x1, int y1, int x2, int y2, int sigtype, int density, int remove)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!SctCanBuild()) {
		return dump({{"ok", false}, {"error", "not in game"}, {"cost", 0}});
	}
	if (x1 < 0 || y1 < 0 || x2 < 0 || y2 < 0 ||
			static_cast<uint>(x1) > Map::MaxX() || static_cast<uint>(y1) > Map::MaxY() ||
			static_cast<uint>(x2) > Map::MaxX() || static_cast<uint>(y2) > Map::MaxY()) {
		return dump({{"ok", false}, {"error", "invalid tile"}, {"cost", 0}});
	}

	const TileIndex tile = TileXY(static_cast<uint>(x1), static_cast<uint>(y1));
	const TileIndex end_in = TileXY(static_cast<uint>(x2), static_cast<uint>(y2));

	/* Auto-snap (explicit_track = -1) reuses the same 8-direction rule the rail
	 * build uses, giving the primary track and a sibling to retry for staircases. */
	const SctRailDrag drag = SctResolveRailDrag(tile, end_in, -1);

	AutoRestoreBackup backup(_current_company, _local_company);

	if (remove != 0) {
		CommandCost cost = Command<CMD_REMOVE_SIGNAL_TRACK>::Do(
				DoCommandFlag::Execute, tile, drag.end, drag.track, false);
		if (cost.Failed() && drag.alternate != INVALID_TRACK) {
			cost = Command<CMD_REMOVE_SIGNAL_TRACK>::Do(
					DoCommandFlag::Execute, tile, drag.end, drag.alternate, false);
		}
		return dump(SctCostResult(cost));
	}

	const SignalType st = (sigtype >= SIGTYPE_BLOCK && sigtype <= SIGTYPE_LAST)
			? static_cast<SignalType>(sigtype) : SIGTYPE_BLOCK;
	const uint8_t dens = static_cast<uint8_t>(std::clamp(density <= 0 ? 1 : density, 1, 20));

	CommandCost cost = Command<CMD_BUILD_SIGNAL_TRACK>::Do(
			DoCommandFlag::Execute, tile, drag.end, drag.track, st, SIG_ELECTRIC,
			false, false, false, dens);
	if (cost.Failed() && drag.alternate != INVALID_TRACK) {
		cost = Command<CMD_BUILD_SIGNAL_TRACK>::Do(
				DoCommandFlag::Execute, tile, drag.end, drag.alternate, st, SIG_ELECTRIC,
				false, false, false, dens);
	}
	return dump(SctCostResult(cost));
}

/**
 * Wave 26 — reorder an order within a vehicle's list (drag/drop). order_cmd.h
 * CmdMoveOrder(flags, veh, moving_order, target_order).
 * @return {ok, error}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_move_order(int veh, int from, int to)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!SctCanBuild()) {
		return dump({{"ok", false}, {"error", "not in game"}});
	}

	const VehicleID vid{static_cast<uint32_t>(veh)};
	if (!Vehicle::IsValidID(vid)) {
		return dump({{"ok", false}, {"error", "invalid vehicle"}});
	}

	AutoRestoreBackup backup(_current_company, _local_company);

	CommandCost cost = Command<CMD_MOVE_ORDER>::Do(
			DoCommandFlag::Execute, vid,
			static_cast<VehicleOrderID>(from), static_cast<VehicleOrderID>(to));
	return dump(SctOkResult(cost));
}

/**
 * Wave 26 — append a conditional (skip-to) order to a vehicle's list. Mirrors the
 * order GUI's insert path (order_gui.cpp:1170): a default-constructed Order with
 * MakeConditional(skipTo), inserted at the end via order_cmd.h CmdInsertOrder. The
 * condition variable/comparator/value default to what MakeConditional sets and are
 * later edited through sct_modify_order (MOF_COND_VARIABLE/COMPARATOR/VALUE).
 * @return {ok, error}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_add_conditional_order(int veh, int skip_to)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!SctCanBuild()) {
		return dump({{"ok", false}, {"error", "not in game"}});
	}

	const Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr) {
		return dump({{"ok", false}, {"error", "no vehicle"}});
	}

	AutoRestoreBackup backup(_current_company, _local_company);

	Order o{};
	o.MakeConditional(static_cast<VehicleOrderID>(skip_to));

	const VehicleOrderID sel_ord = v->GetNumOrders();
	CommandCost cost = Command<CMD_INSERT_ORDER>::Do(
			DoCommandFlag::Execute, VehicleID{static_cast<uint32_t>(veh)}, sel_ord, o);
	return dump(SctOkResult(cost));
}

/**
 * Wave 26 — copy (share == 0) or share (share != 0) the source vehicle's order list
 * onto the destination vehicle. order_cmd.h CmdCloneOrder(flags, CloneOptions, dst,
 * src) with CO_SHARE / CO_COPY (order_type.h).
 * @return {ok, error}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_clone_orders(int dst_veh, int src_veh, int share)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!SctCanBuild()) {
		return dump({{"ok", false}, {"error", "not in game"}});
	}

	const VehicleID dst{static_cast<uint32_t>(dst_veh)};
	const VehicleID src{static_cast<uint32_t>(src_veh)};
	if (!Vehicle::IsValidID(dst) || !Vehicle::IsValidID(src)) {
		return dump({{"ok", false}, {"error", "invalid vehicle"}});
	}

	AutoRestoreBackup backup(_current_company, _local_company);

	CommandCost cost = Command<CMD_CLONE_ORDER>::Do(
			DoCommandFlag::Execute, (share != 0) ? CO_SHARE : CO_COPY, dst, src);
	return dump(SctOkResult(cost));
}

/**
 * Wave 26 — the vehicle ids sharing an order list with the given vehicle (walks the
 * shared-order ring FirstShared()/NextShared(), order_base.h/vehicle_base.h). Always
 * includes the vehicle itself; a single-element array means it shares with no one.
 * @return JSON array of vehicle ids, or "[]" for an invalid id.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_shared_vehicles(int veh_id)
{
	static std::string buffer;

	const Vehicle *v = Vehicle::GetIfValid(veh_id);
	if (v == nullptr) {
		buffer = "[]";
		return buffer.c_str();
	}

	nlohmann::json arr = nlohmann::json::array();
	for (const Vehicle *u = v->FirstShared(); u != nullptr; u = u->NextShared()) {
		arr.push_back(static_cast<int>(u->index.base()));
	}

	buffer = arr.dump();
	return buffer.c_str();
}

/**
 * Wave 26 — the bridge specs valid for a span from (x1,y1) to (x2,y2). Iterates
 * MAX_BRIDGES and keeps every spec CheckBridgeAvailability accepts for that length
 * (bridge_gui.cpp:415-431). `name` is the spec's material string; `speed` its max
 * speed (bridge km/h units); `maxLength` the max span; `price` the relative price
 * multiplier the UI can label with. `transport` (0 rail, 1 road, 2 water) is accepted
 * for forward-compat and does not change availability here.
 * @return JSON array, or "[]" for off-map tiles.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_bridge_types(int x1, int y1, int x2, int y2, int transport)
{
	static std::string buffer;

	(void)transport;

	if (!SctInGame() || !Map::IsInitialized() ||
			x1 < 0 || y1 < 0 || x2 < 0 || y2 < 0 ||
			static_cast<uint>(x1) > Map::MaxX() || static_cast<uint>(y1) > Map::MaxY() ||
			static_cast<uint>(x2) > Map::MaxX() || static_cast<uint>(y2) > Map::MaxY()) {
		buffer = "[]";
		return buffer.c_str();
	}

	const TileIndex t1 = TileXY(static_cast<uint>(x1), static_cast<uint>(y1));
	const TileIndex t2 = TileXY(static_cast<uint>(x2), static_cast<uint>(y2));
	const uint bridge_len = GetTunnelBridgeLength(t1, t2);

	nlohmann::json arr = nlohmann::json::array();
	for (BridgeType i = 0; i < MAX_BRIDGES; i++) {
		if (CheckBridgeAvailability(i, bridge_len).Failed()) continue;
		const BridgeSpec *spec = GetBridgeSpec(i);
		arr.push_back({
			{"id", static_cast<int>(i)},
			{"name", GetString(spec->material)},
			{"speed", spec->speed},
			{"maxLength", spec->max_length},
			{"price", spec->price},
		});
	}

	buffer = arr.dump();
	return buffer.c_str();
}

/**
 * Wave 26 — start a new AI company, mirroring console ConStartAI
 * (console_cmds.cpp:1446). Requires a normal game with a free company slot. An empty
 * `name` starts a random AI; otherwise the next free AIConfig slot is configured with
 * that AI (config->Change) before the company is created via CMD_COMPANY_CTRL
 * (CCA_NEW_AI). A name that matches no installed AI is rejected.
 * @return {ok, error}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_start_ai(const char *name)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (_game_mode != GM_NORMAL) {
		return dump({{"ok", false}, {"error", "not in game"}});
	}
	if (Company::GetNumItems() >= MAX_COMPANIES || !AI::CanStartNew()) {
		return dump({{"ok", false}, {"error", "no free company slots"}});
	}

	/* Find the next free company slot (same scan as ConStartAI). */
	int n = 0;
	for (const Company *c : Company::Iterate()) {
		if (c->index != n) break;
		n++;
	}

	AIConfig *config = AIConfig::GetConfig(static_cast<CompanyID>(static_cast<CompanyID::BaseType>(n)));
	if (name != nullptr && name[0] != '\0') {
		config->Change(name, -1, false);
		if (!config->HasScript()) {
			return dump({{"ok", false}, {"error", "AI not found"}});
		}
	}

	Command<CMD_COMPANY_CTRL>::Do(
			DoCommandFlag::Execute, CCA_NEW_AI, CompanyID::Invalid(), CRR_NONE, INVALID_CLIENT_ID);
	return dump({{"ok", true}, {"error", std::string{}}});
}

/**
 * Wave 26 — set (or clear) the Game Script used for new games. Mirrors the GS GUI /
 * settings path: GameConfig::GetConfig(SSS_FORCE_NEWGAME)->Change(name)
 * (settings.cpp:985-994). An empty `name` clears the newgame GS (Change(nullopt)). A
 * name that matches no installed GS leaves the slot without a script. Takes effect on
 * the next new game.
 * @return {ok, error}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_set_game_script(const char *name)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	GameConfig *config = GameConfig::GetConfig(GameConfig::SSS_FORCE_NEWGAME);
	if (config == nullptr) {
		return dump({{"ok", false}, {"error", "no game-script config"}});
	}

	if (name != nullptr && name[0] != '\0') {
		config->Change(std::string_view{name});
		if (!config->HasScript()) {
			return dump({{"ok", false}, {"error", "game script not found"}});
		}
	} else {
		config->Change(std::nullopt);
	}
	return dump({{"ok", true}, {"error", std::string{}}});
}

/**
 * Wave 29 — describe the parameters of a currently-selected NewGRF. `grfid_hex` is the
 * canonical 8-hex-char display grfid (as query('newgrfAvailable') emits); the GRF must
 * already be in the new-game config (added via sct_newgrf_select). Mirrors the parameter
 * window (newgrf_gui.cpp): one descriptor slot per `num_valid_params`. Slots with declared
 * action-14 info (param_info[i]) expose that name/description/min/max/default; GRFs without
 * declared info fall back to generic "Parameter N" slots (min 0, max 2147483647, default 0),
 * exactly as the GUI's dummy parameter-info does. `max` is clamped to 2147483647 so consumers
 * treating it as a signed 32-bit int stay safe. `values` is the raw current param array
 * (config->param); entries beyond its length are implicitly 0.
 * @return JSON `{params:[{index,name,description,min,max,defaultValue}], values:[…]}`, or
 *         the literal JSON `null` when the GRF is not selected.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_newgrf_params(const char *grfid_hex)
{
	static std::string buffer;

	const GRFConfig *config = SctFindNewgameGrf(grfid_hex);
	if (config == nullptr) {
		buffer = "null";
		return buffer.c_str();
	}

	constexpr uint32_t MAX_CLAMP = 0x7fffffffu; // 2147483647: keep JSON ints signed-32-bit safe

	nlohmann::json params = nlohmann::json::array();
	const uint count = config->num_valid_params;
	for (uint i = 0; i < count; i++) {
		std::string name;
		std::string desc;
		uint32_t minv = 0;
		uint32_t maxv = MAX_CLAMP;
		uint32_t defv = 0;

		if (i < config->param_info.size() && config->param_info[i].has_value()) {
			const GRFParameterInfo &info = config->param_info[i].value();
			if (auto n = GetGRFStringFromGRFText(info.name); n.has_value()) name = std::string(*n);
			if (auto d = GetGRFStringFromGRFText(info.desc); d.has_value()) desc = std::string(*d);
			minv = std::min<uint32_t>(info.min_value, MAX_CLAMP);
			maxv = std::min<uint32_t>(info.max_value, MAX_CLAMP);
			defv = std::min<uint32_t>(info.def_value, MAX_CLAMP);
		}
		if (name.empty()) name = fmt::format("Parameter {}", i + 1);

		params.push_back({
			{"index", static_cast<int>(i)},
			{"name", name},
			{"description", desc},
			{"min", static_cast<int64_t>(minv)},
			{"max", static_cast<int64_t>(maxv)},
			{"defaultValue", static_cast<int64_t>(defv)},
		});
	}

	nlohmann::json values = nlohmann::json::array();
	for (uint32_t v : config->param) values.push_back(static_cast<int64_t>(v));

	nlohmann::json j = {{"params", std::move(params)}, {"values", std::move(values)}};
	buffer = j.dump();
	return buffer.c_str();
}

/**
 * Wave 29 — set one parameter of a currently-selected NewGRF in the new-game config.
 * `grfid_hex` is the canonical display grfid; the GRF must already be selected. `index`
 * must be < num_valid_params. Uses the GUI's exact mutation path: GRFConfig::SetValue on
 * the declared GRFParameterInfo (which clamps to the param's range, resizes config->param
 * as needed, and honours bit-packed sub-parameters); for slots without declared info a
 * fresh dummy GRFParameterInfo(index) is used (min 0, max UINT32_MAX, 32-bit), mirroring
 * newgrf_gui.cpp's GetDummyParameterInfo. Takes effect on the next new game (no rescan).
 * @return {ok, error}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_newgrf_set_param(const char *grfid_hex, int index, int value)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	GRFConfig *config = SctFindNewgameGrf(grfid_hex);
	if (config == nullptr) {
		return dump({{"ok", false}, {"error", "grf not selected"}});
	}
	if (index < 0 || index >= static_cast<int>(config->num_valid_params)) {
		return dump({{"ok", false}, {"error", "param index out of range"}});
	}

	const uint idx = static_cast<uint>(index);
	if (idx < config->param_info.size() && config->param_info[idx].has_value()) {
		config->SetValue(config->param_info[idx].value(), static_cast<uint32_t>(value));
	} else {
		GRFParameterInfo dummy(idx);
		config->SetValue(dummy, static_cast<uint32_t>(value));
	}
	return dump({{"ok", true}, {"error", std::string{}}});
}

/**
 * Wave 29 — start world generation from a heightmap PNG/BMP the app has already written
 * into the Emscripten FS heightmap directory (HEIGHTMAP_DIR, i.e. scenario/heightmap).
 * Mirrors the command-line -g path (openttd.cpp) + the GUI's StartGeneratingLandscape
 * (genworld_gui.cpp): classify the file by extension via FiosGetHeightmapListCallback,
 * verify it exists, read its dimensions (GetHeightmapDimensions) and pick fitting
 * power-of-two map sizes (rotation-aware), stage it into _file_to_saveload, make the
 * new-game settings live, reset the GRF config, then request SM_START_HEIGHTMAP so the
 * next tick generates a normal game from the heightmap.
 * @return {ok, error}. ok:false with a clear error when the file is missing or unreadable.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_start_heightmap(const char *filename)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (filename == nullptr || filename[0] == '\0') {
		return dump({{"ok", false}, {"error", "empty filename"}});
	}

	const std::string name = filename;

	/* Classify by extension exactly as openttd.cpp's -g path does. */
	const size_t dot = name.find_last_of('.');
	const std::string ext = (dot == std::string::npos) ? std::string{} : name.substr(dot);
	const FiosType ft = std::get<0>(FiosGetHeightmapListCallback(SLO_LOAD, name, ext));
	if (ft.abstract != FT_HEIGHTMAP) {
		return dump({{"ok", false}, {"error", "not a heightmap file (.png/.bmp)"}});
	}

	/* The heightmap loader (heightmap.cpp) reads from HEIGHTMAP_DIR (scenario/heightmap). */
	if (!FioCheckFileExists(name, HEIGHTMAP_DIR)) {
		return dump({{"ok", false}, {"error", "heightmap file not found"}});
	}

	uint hx = 0;
	uint hy = 0;
	if (!GetHeightmapDimensions(ft.detailed, name, &hx, &hy) || hx == 0 || hy == 0) {
		return dump({{"ok", false}, {"error", "could not read heightmap"}});
	}

	/* Pick the smallest power-of-two map that fits each dimension (as the GUI recommends),
	 * clamped to the engine's map-size range and oriented like the current rotation. */
	auto fit_bits = [](uint dim) -> uint {
		uint bits = MIN_MAP_SIZE_BITS;
		while ((1U << bits) < dim && bits < MAX_MAP_SIZE_BITS) bits++;
		return bits;
	};
	uint bits_x = fit_bits(hx);
	uint bits_y = fit_bits(hy);
	if (_settings_newgame.game_creation.heightmap_rotation == HM_CLOCKWISE) std::swap(bits_x, bits_y);
	_settings_newgame.game_creation.map_x = bits_x;
	_settings_newgame.game_creation.map_y = bits_y;

	/* Stage the file the same way _file_to_saveload.Set(...) does for the GUI/-g path. */
	_file_to_saveload.name = name;
	_file_to_saveload.SetMode(ft, SLO_LOAD);

	/* Mirror genworld_gui.cpp StartGeneratingLandscape(GLWM_HEIGHTMAP) in a normal game. */
	CloseAllNonVitalWindows();
	ClearErrorMessages();
	MakeNewgameSettingsLive();
	ResetGRFConfig(true);
	_switch_mode = SM_START_HEIGHTMAP;

	return dump({{"ok", true}, {"error", std::string{}}});
}

/**
 * Wave 29 — send a chat message in a network game (network_func.h). dest_type maps to
 * DestType: 0 = broadcast (all), 1 = team (company `dest`), 2 = client (client id `dest`).
 * The NetworkAction is NETWORK_ACTION_CHAT + dest_type, exactly as network_chat_gui.cpp's
 * SendChat computes it. Networked games only.
 * @return {ok, error}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_chat(int dest_type, int dest, const char *msg)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!_networking) {
		return dump({{"ok", false}, {"error", "not in a network game"}});
	}
	if (dest_type < 0 || dest_type > 2) {
		return dump({{"ok", false}, {"error", "invalid dest_type"}});
	}

	const DestType type = static_cast<DestType>(dest_type);
	const NetworkAction action = static_cast<NetworkAction>(NETWORK_ACTION_CHAT + dest_type);
	NetworkClientSendChat(action, type, dest, msg == nullptr ? "" : msg);

	return dump({{"ok", true}, {"error", std::string{}}});
}

/**
 * Wave 29 — send an rcon command to the server (network_func.h). Client-side only: rcon
 * is meaningless on a listen server and this emscripten build is always a client, but the
 * guard is kept explicit. Networked games only.
 * @return {ok, error}.
 */
const char *EMSCRIPTEN_KEEPALIVE sct_rcon(const char *password, const char *cmd)
{
	static std::string buffer;

	auto dump = [&](const nlohmann::json &j) -> const char * {
		buffer = j.dump();
		return buffer.c_str();
	};

	if (!_networking) {
		return dump({{"ok", false}, {"error", "not in a network game"}});
	}
	if (_network_server) {
		return dump({{"ok", false}, {"error", "rcon is a client action"}});
	}

	NetworkClientSendRcon(password == nullptr ? "" : password, cmd == nullptr ? "" : cmd);

	return dump({{"ok", true}, {"error", std::string{}}});
}

} /* extern "C" */

#endif /* __EMSCRIPTEN__ */
