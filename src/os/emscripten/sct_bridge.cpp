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
#include "../../order_cmd.h"
#include "../../order_type.h"
#include "../../misc_cmd.h"
#include "../../landscape_cmd.h"
#include "../../terraform_cmd.h"
#include "../../newgrf_station.h"
#include "../../rail.h"
#include "../../road_func.h"
#include "../../track_type.h"
#include "../../direction_type.h"
#include "../../slope_type.h"
#include "../../cargo_type.h"
#include "../../cargotype.h"
#include "../../network/network_type.h"
#include "../../viewport_func.h"
#include "../../window_func.h"
#include "../../window_gui.h"
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
#include "../../3rdparty/nlohmann/json.hpp"

#include "table/strings.h"

#include <emscripten.h>
#include <algorithm>
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
	} else if (std::strcmp(kind, "cargos") == 0) {
		j = SctQueryCargos();
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
		/* Wave 7 — robust track drag: snap to dominant axis, auto-flatten the
		 * line, then build. rail_cmd.h:19 CmdBuildRailroadTrack(end, start,
		 * railtype, track, auto_remove_signals, fail_on_obstacle).
		 * p2==0 (or out of range) = auto snap; non-zero valid Track overrides
		 * and keeps end = b (contract: track dir 0 auto). */
		const RailType rt = SctResolveRailType(p1);

		TileIndex snapped_end = tile_b;
		Track track = TRACK_X;
		Track track_alt = INVALID_TRACK; /* sibling half-track to retry with */
		bool diagonal = false;

		if (p2 != 0 && p2 >= TRACK_BEGIN && p2 < TRACK_END) {
			/* Explicit track override — keep dragged end tile as-is. */
			track = static_cast<Track>(p2);
			snapped_end = tile_b;
		} else {
			/* Wave 12 — snap to the nearest of the EIGHT drag directions:
			 * tile-axis runs (TRACK_X / TRACK_Y, the screen diagonals) or 45°
			 * staircase runs built from half-tile tracks (TRACK_UPPER/LOWER for
			 * screen-horizontal, TRACK_LEFT/RIGHT for screen-vertical drags).
			 * CmdBuildRailroadTrack alternates the half tracks itself
			 * (ValidateAutoDrag + the trackdir toggle in its build loop). The
			 * React BuildCaptureLayer mirrors this exact rule for its preview:
			 * axis wins when 2*max(|dx|,|dy|) >= 5*min (i.e. within ~22.5° of
			 * the axis), else staircase of length round((|dx|+|dy|)/2). */
			const int ax = static_cast<int>(TileX(tile_a));
			const int ay = static_cast<int>(TileY(tile_a));
			const int bx = static_cast<int>(TileX(tile_b));
			const int by = static_cast<int>(TileY(tile_b));
			const int dx = bx - ax;
			const int dy = by - ay;
			const int adx = std::abs(dx);
			const int ady = std::abs(dy);
			const int max_x = static_cast<int>(Map::MaxX());
			const int max_y = static_cast<int>(Map::MaxY());

			if (2 * std::max(adx, ady) >= 5 * std::min(adx, ady)) {
				/* Axis-aligned run (track_type.h:21-22). */
				if (adx >= ady) {
					track = TRACK_X;
					snapped_end = TileXY(static_cast<uint>(std::clamp(bx, 0, max_x)),
							static_cast<uint>(std::clamp(ay, 0, max_y)));
				} else {
					track = TRACK_Y;
					snapped_end = TileXY(static_cast<uint>(std::clamp(ax, 0, max_x)),
							static_cast<uint>(std::clamp(by, 0, max_y)));
				}
			} else {
				/* 45° staircase. Which half (upper/lower, left/right) the run
				 * starts with depends on sub-tile grab position we don't have,
				 * so try one and fall back to its sibling. */
				diagonal = true;
				const int sx = dx >= 0 ? 1 : -1;
				const int sy = dy >= 0 ? 1 : -1;
				const int n = (adx + ady + 1) / 2;
				const int end_x = std::clamp(ax + n * sx, 0, max_x);
				const int end_y = std::clamp(ay + n * sy, 0, max_y);
				snapped_end = TileXY(static_cast<uint>(end_x), static_cast<uint>(end_y));
				if (sx == sy) {
					/* Screen-vertical (x and y move together). */
					track = TRACK_LEFT;
					track_alt = TRACK_RIGHT;
				} else {
					/* Screen-horizontal (x and y move opposite). */
					track = TRACK_UPPER;
					track_alt = TRACK_LOWER;
				}
			}
		}

		/* Off-map guard: start/end must be on the map (tile_map.h IsValidTile). */
		if (!IsValidTile(tile_a) || !IsValidTile(snapped_end)) {
			return dump({{"ok", false}, {"error", "invalid tile"}, {"cost", 0}});
		}

		/* Auto-flatten axis runs first (terraform_cmd.h:18 CmdLevelLand — the
		 * rect between the endpoints IS the line there). For staircase runs the
		 * rect would be n×n tiles, so try the build on natural terrain first
		 * and only level-then-retry when it fails. */
		if (!diagonal) {
			(void)Command<CMD_LEVEL_LAND>::Do(DoCommandFlag::Execute, snapped_end, tile_a, false, LM_LEVEL);
		}

		CommandCost cost = Command<CMD_BUILD_RAILROAD_TRACK>::Do(
				DoCommandFlag::Execute, snapped_end, tile_a, rt, track, true, false);
		if (cost.Failed() && track_alt != INVALID_TRACK) {
			cost = Command<CMD_BUILD_RAILROAD_TRACK>::Do(
					DoCommandFlag::Execute, snapped_end, tile_a, rt, track_alt, true, false);
		}
		if (cost.Failed() && diagonal) {
			(void)Command<CMD_LEVEL_LAND>::Do(DoCommandFlag::Execute, snapped_end, tile_a, false, LM_LEVEL);
			cost = Command<CMD_BUILD_RAILROAD_TRACK>::Do(
					DoCommandFlag::Execute, snapped_end, tile_a, rt, track, true, false);
			if (cost.Failed() && track_alt != INVALID_TRACK) {
				cost = Command<CMD_BUILD_RAILROAD_TRACK>::Do(
						DoCommandFlag::Execute, snapped_end, tile_a, rt, track_alt, true, false);
			}
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
		CommandCost cost = Command<CMD_BUILD_RAIL_STATION>::Do(
				DoCommandFlag::Execute, tile_a, rt, axis, numtracks, plat_len,
				STAT_CLASS_DFLT, 0, StationID::Invalid(), false);
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
 * Read-back of a vehicle's order list (Wave 4).
 * @return JSON array of orders, or the literal string "null" if vehicle is invalid.
 * Schema: [{"index", "type", "dest", "nonstop", "load", "unload"}, ...]
 * type is "station"|"depot"|"waypoint"|"other"; dest is -1 for "other".
 */
const char *EMSCRIPTEN_KEEPALIVE sct_vehicle_orders(int vehicle_id)
{
	static std::string buffer;

	const Vehicle *v = Vehicle::GetIfValid(vehicle_id);
	if (v == nullptr) {
		buffer = "null";
		return buffer.c_str();
	}

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
 * Stash build defaults read by sct_build when p1 == 0.
 * Keys: "railtype", "roadtype".
 */
void EMSCRIPTEN_KEEPALIVE sct_set_build_param(const char *key, int value)
{
	if (key == nullptr) return;
	/* Case-insensitive: the JS side sends camelCase ("railType"). */
	if (strcasecmp(key, "railtype") == 0) {
		g_sct_railtype = value;
	} else if (strcasecmp(key, "roadtype") == 0) {
		g_sct_roadtype = value;
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

} /* extern "C" */

#endif /* __EMSCRIPTEN__ */
