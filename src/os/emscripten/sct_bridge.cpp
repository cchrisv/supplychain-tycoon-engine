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
#include "../../landscape_cmd.h"
#include "../../terraform_cmd.h"
#include "../../newgrf_station.h"
#include "../../rail.h"
#include "../../road_func.h"
#include "../../track_type.h"
#include "../../direction_type.h"
#include "../../slope_type.h"
#include "../../cargo_type.h"
#include "../../network/network_type.h"
#include "../../viewport_func.h"
#include "../../window_func.h"
#include "../../window_gui.h"
#include "../../tile_map.h"
#include "../../3rdparty/nlohmann/json.hpp"

#include "table/strings.h"

#include <emscripten.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <tuple>

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

/* ---- Write-bridge helpers (build / place / vehicle commands) ---- */

/** Default railtype for build commands when p1 == 0 (0 = auto-pick first available). */
static int g_sct_railtype = 0;
/** Default roadtype for build commands when p1 == 0 (0 = auto-pick first available). */
static int g_sct_roadtype = 0;

static bool SctCanBuild()
{
	return SctInGame() && Map::IsInitialized() && Company::IsValidID(_local_company);
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

static Track SctResolveTrack(int p2, TileIndex start, TileIndex end)
{
	if (p2 >= TRACK_BEGIN && p2 < TRACK_END) return static_cast<Track>(p2);
	if (TileY(start) == TileY(end)) return TRACK_X;
	if (TileX(start) == TileX(end)) return TRACK_Y;
	return TRACK_X;
}

static DiagDirection SctResolveDiagDir(int p2)
{
	if (p2 >= DIAGDIR_BEGIN && p2 < DIAGDIR_END) return static_cast<DiagDirection>(p2);
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
	if (!SctCanBuild()) {
		return dump({{"ok", false}, {"error", "not in game"}, {"cost", 0}});
	}

	AutoRestoreBackup backup(_current_company, _local_company);

	const TileIndex tile_a{static_cast<uint32_t>(a)};
	const TileIndex tile_b{static_cast<uint32_t>(b)};

	if (std::strcmp(action, "rail_track") == 0) {
		/* rail_cmd.h:19 CmdBuildRailroadTrack(end, start, railtype, track, auto_remove_signals, fail_on_obstacle) */
		const RailType rt = SctResolveRailType(p1);
		const Track track = SctResolveTrack(p2, tile_a, tile_b);
		CommandCost cost = Command<CMD_BUILD_RAILROAD_TRACK>::Do(
				DoCommandFlag::Execute, tile_b, tile_a, rt, track, true, false);
		return dump(SctCostResult(cost));
	}

	if (std::strcmp(action, "rail_station") == 0) {
		/* station_cmd.h:28 CmdBuildRailStation */
		const RailType rt = SctResolveRailType(0); /* use stash / first available; p1 is axis */
		const Axis axis = SctResolveAxis(p1);
		const uint8_t numtracks = SctClampStationDim((p2 >> 8) & 0xFF, 1);
		const uint8_t plat_len = SctClampStationDim(p2 & 0xFF, 3);
		CommandCost cost = Command<CMD_BUILD_RAIL_STATION>::Do(
				DoCommandFlag::Execute, tile_a, rt, axis, numtracks, plat_len,
				STAT_CLASS_DFLT, 0, StationID::Invalid(), false);
		return dump(SctCostResult(cost));
	}

	if (std::strcmp(action, "rail_depot") == 0) {
		/* rail_cmd.h:23 CmdBuildTrainDepot */
		const RailType rt = SctResolveRailType(p1);
		const DiagDirection dir = SctResolveDiagDir(p2);
		CommandCost cost = Command<CMD_BUILD_TRAIN_DEPOT>::Do(
				DoCommandFlag::Execute, tile_a, rt, dir);
		return dump(SctCostResult(cost));
	}

	if (std::strcmp(action, "signal") == 0) {
		/* No CMD_BUILD_SIGNALS in 15.3; CMD_BUILD_SINGLE_SIGNAL / CMD_BUILD_SIGNAL_TRACK
		 * (rail_cmd.h:24,27) need many SignalType/variant args. Stub for now. */
		return dump({{"ok", false}, {"error", "signals pending"}, {"cost", 0}});
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

	if (std::strcmp(action, "road_depot") == 0) {
		/* road_cmd.h:27 CmdBuildRoadDepot */
		const RoadType rt = SctResolveRoadType(p1);
		const DiagDirection dir = SctResolveDiagDir(p2);
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

	if (std::strcmp(action, "airport") == 0) {
		/* station_cmd.h:26 CmdBuildAirport */
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

	return dump({{"ok", false}, {"error", "unknown action"}, {"cost", 0}});
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
		/* vehicle_cmd.h:27 CmdStartStopVehicle — toggles start/stop */
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
		return dump({{"ok", false}, {"error", "skipOrder pending"}});
	}

	return dump({{"ok", false}, {"error", "unknown action"}});
}

/**
 * Stash build defaults read by sct_build when p1 == 0.
 * Keys: "railtype", "roadtype".
 */
void EMSCRIPTEN_KEEPALIVE sct_set_build_param(const char *key, int value)
{
	if (key == nullptr) return;
	if (std::strcmp(key, "railtype") == 0) {
		g_sct_railtype = value;
	} else if (std::strcmp(key, "roadtype") == 0) {
		g_sct_roadtype = value;
	}
}

} /* extern "C" */

#endif /* __EMSCRIPTEN__ */
