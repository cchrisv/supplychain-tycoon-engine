Module.arguments.push('-mnull', '-snull', '-vsdl');

/* Supplychain Tycoon external-UI bridge.
 * cwrap must not run before the runtime is ready; wrap lazily on first call. */
Module.sct = (function() {
    var _exec = null;
    var _getState = null;
    var _setFastForward = null;
    var _returnToMenu = null;
    var _query = null;
    var _tileAt = null;
    var _tilePoly = null;
    var _build = null;
    var _buildVehicle = null;
    var _vehicleCmd = null;
    var _setBuildParam = null;
    var _addOrder = null;
    var _vehicleOrders = null;
    var _cloneVehicle = null;
    var _createGroup = null;
    var _addToGroup = null;
    var _refitVehicle = null;
    var _renameVehicle = null;
    var _companyLoan = null;
    var _deleteOrder = null;
    var _modifyOrder = null;
    var _scrollToTile = null;
    var _tileInfo = null;
    var _renameStation = null;
    var _autoreplace = null;
    var _enginePreview = null;
    var _serviceInterval = null;
    var _renameTown = null;
    var _renameWaypoint = null;
    var _sign = null;
    var _foundTown = null;
    var _fundIndustry = null;
    var _townAction = null;
    var _scrollToVehicle = null;
    var _setNativeClick = null;
    return {
        exec: function(cmd) {
            if (!_exec) _exec = Module.cwrap('sct_console_exec', null, ['string']);
            return _exec(cmd);
        },
        getState: function() {
            if (!_getState) _getState = Module.cwrap('sct_get_state', 'string', []);
            return JSON.parse(_getState());
        },
        setFastForward: function(on) {
            if (!_setFastForward) _setFastForward = Module.cwrap('sct_set_fast_forward', null, ['number']);
            return _setFastForward(on ? 1 : 0);
        },
        returnToMenu: function() {
            if (!_returnToMenu) _returnToMenu = Module.cwrap('sct_return_to_menu', null, []);
            return _returnToMenu();
        },
        query: function(kind) {
            if (!_query) _query = Module.cwrap('sct_query', 'string', ['string']);
            return JSON.parse(_query(kind));
        },
        tileAt: function(px, py) {
            if (!_tileAt) _tileAt = Module.cwrap('sct_tile_at_screen', 'string', ['number', 'number']);
            return JSON.parse(_tileAt(px, py));
        },
        tilePoly: function(tile) {
            if (!_tilePoly) _tilePoly = Module.cwrap('sct_tile_poly', 'string', ['number']);
            var r = _tilePoly(tile|0);
            return r ? JSON.parse(r) : null;
        },
        build: function(action, a, b, p1, p2) {
            if (!_build) _build = Module.cwrap('sct_build', 'string', ['string', 'number', 'number', 'number', 'number']);
            return JSON.parse(_build(action, a|0, b|0, p1|0, p2|0));
        },
        buildVehicle: function(depotTile, engineId) {
            if (!_buildVehicle) _buildVehicle = Module.cwrap('sct_build_vehicle', 'string', ['number', 'number']);
            return JSON.parse(_buildVehicle(depotTile|0, engineId|0));
        },
        vehicleCmd: function(vehicleId, action) {
            if (!_vehicleCmd) _vehicleCmd = Module.cwrap('sct_vehicle_cmd', 'string', ['number', 'string']);
            return JSON.parse(_vehicleCmd(vehicleId|0, action));
        },
        setBuildParam: function(key, value) {
            if (!_setBuildParam) _setBuildParam = Module.cwrap('sct_set_build_param', null, ['string', 'number']);
            return _setBuildParam(key, value|0);
        },
        addOrder: function(vehicleId, kind, destId) {
            if (!_addOrder) _addOrder = Module.cwrap('sct_add_order', 'string', ['number', 'string', 'number']);
            return JSON.parse(_addOrder(vehicleId|0, kind, destId|0));
        },
        vehicleOrders: function(id) {
            if (!_vehicleOrders) _vehicleOrders = Module.cwrap('sct_vehicle_orders', 'string', ['number']);
            var r = _vehicleOrders(id|0);
            return r ? JSON.parse(r) : null;
        },
        cloneVehicle: function(depotTile, vehicleId, shareOrders) {
            if (!_cloneVehicle) _cloneVehicle = Module.cwrap('sct_clone_vehicle', 'string', ['number', 'number', 'number']);
            return JSON.parse(_cloneVehicle(depotTile|0, vehicleId|0, shareOrders ? 1 : 0));
        },
        createGroup: function(vehicleType, parentGroup) {
            if (!_createGroup) _createGroup = Module.cwrap('sct_create_group', 'string', ['number', 'number']);
            return JSON.parse(_createGroup(vehicleType|0, parentGroup === undefined ? -1 : parentGroup|0));
        },
        addToGroup: function(groupId, vehicleId, addShared) {
            if (!_addToGroup) _addToGroup = Module.cwrap('sct_add_to_group', 'string', ['number', 'number', 'number']);
            return JSON.parse(_addToGroup(groupId|0, vehicleId|0, addShared ? 1 : 0));
        },
        refitVehicle: function(vehicleId, cargoType) {
            if (!_refitVehicle) _refitVehicle = Module.cwrap('sct_refit_vehicle', 'string', ['number', 'number']);
            return JSON.parse(_refitVehicle(vehicleId|0, cargoType|0));
        },
        renameVehicle: function(vehicleId, name) {
            if (!_renameVehicle) _renameVehicle = Module.cwrap('sct_rename_vehicle', 'string', ['number', 'string']);
            return JSON.parse(_renameVehicle(vehicleId|0, name == null ? '' : String(name)));
        },
        companyLoan: function(delta) {
            if (!_companyLoan) _companyLoan = Module.cwrap('sct_company_loan', 'string', ['number']);
            return JSON.parse(_companyLoan(delta|0));
        },
        deleteOrder: function(vehicleId, orderIndex) {
            if (!_deleteOrder) _deleteOrder = Module.cwrap('sct_delete_order', 'string', ['number', 'number']);
            return JSON.parse(_deleteOrder(vehicleId|0, orderIndex|0));
        },
        modifyOrder: function(vehicleId, orderIndex, mof, value) {
            if (!_modifyOrder) _modifyOrder = Module.cwrap('sct_modify_order', 'string', ['number', 'number', 'number', 'number']);
            return JSON.parse(_modifyOrder(vehicleId|0, orderIndex|0, mof|0, value|0));
        },
        scrollToTile: function(tile) {
            if (!_scrollToTile) _scrollToTile = Module.cwrap('sct_scroll_to_tile', 'string', ['number']);
            return JSON.parse(_scrollToTile(tile|0));
        },
        tileInfo: function(tile) {
            if (!_tileInfo) _tileInfo = Module.cwrap('sct_tile_info', 'string', ['number']);
            return JSON.parse(_tileInfo(tile|0));
        },
        renameStation: function(stationId, name) {
            if (!_renameStation) _renameStation = Module.cwrap('sct_rename_station', 'string', ['number', 'string']);
            return JSON.parse(_renameStation(stationId|0, name == null ? '' : String(name)));
        },
        autoreplace: function(groupId, fromEngine, toEngine, replaceWhenOld) {
            if (!_autoreplace) _autoreplace = Module.cwrap('sct_autoreplace', 'string', ['number', 'number', 'number', 'number']);
            return JSON.parse(_autoreplace(groupId|0, fromEngine|0, toEngine|0, replaceWhenOld ? 1 : 0));
        },
        enginePreview: function(engineId, accept) {
            if (!_enginePreview) _enginePreview = Module.cwrap('sct_engine_preview', 'string', ['number', 'number']);
            return JSON.parse(_enginePreview(engineId|0, accept ? 1 : 0));
        },
        serviceInterval: function(vehicleId, interval, isPercent) {
            if (!_serviceInterval) _serviceInterval = Module.cwrap('sct_service_interval', 'string', ['number', 'number', 'number']);
            return JSON.parse(_serviceInterval(vehicleId|0, interval|0, isPercent ? 1 : 0));
        },
        renameTown: function(townId, name) {
            if (!_renameTown) _renameTown = Module.cwrap('sct_rename_town', 'string', ['number', 'string']);
            return JSON.parse(_renameTown(townId|0, name == null ? '' : String(name)));
        },
        renameWaypoint: function(waypointId, name) {
            if (!_renameWaypoint) _renameWaypoint = Module.cwrap('sct_rename_waypoint', 'string', ['number', 'string']);
            return JSON.parse(_renameWaypoint(waypointId|0, name == null ? '' : String(name)));
        },
        sign: function(cmdKind, tileOrId, name) {
            if (!_sign) _sign = Module.cwrap('sct_sign', 'string', ['number', 'number', 'string']);
            return JSON.parse(_sign(cmdKind|0, tileOrId|0, name == null ? '' : String(name)));
        },
        foundTown: function(tile, size, cityLayout) {
            if (!_foundTown) _foundTown = Module.cwrap('sct_found_town', 'string', ['number', 'number', 'number']);
            return JSON.parse(_foundTown(tile|0, size|0, cityLayout === undefined ? 0 : cityLayout|0));
        },
        fundIndustry: function(tile, industryType, prospect) {
            if (!_fundIndustry) _fundIndustry = Module.cwrap('sct_fund_industry', 'string', ['number', 'number', 'number']);
            return JSON.parse(_fundIndustry(tile|0, industryType|0, prospect ? 1 : 0));
        },
        townAction: function(townId, action) {
            if (!_townAction) _townAction = Module.cwrap('sct_town_action', 'string', ['number', 'number']);
            return JSON.parse(_townAction(townId|0, action|0));
        },
        scrollToVehicle: function(vehicleId) {
            if (!_scrollToVehicle) _scrollToVehicle = Module.cwrap('sct_scroll_to_vehicle', 'string', ['number']);
            return JSON.parse(_scrollToVehicle(vehicleId|0));
        },
        setNativeClick: function(on) {
            if (!_setNativeClick) _setNativeClick = Module.cwrap('sct_set_native_click', null, ['number']);
            return _setNativeClick(on ? 1 : 0);
        },
    };
})();

Module['websocket'] = { url: function(host, port, proto) {
    /* openttd.org hosts a WebSocket proxy for the content service. */
    if (host == "content.openttd.org" && port == 3978 && proto == "tcp") {
        return "wss://bananas-server.openttd.org/";
    }

    /* Everything else just tries to make a default WebSocket connection.
     * If you run your own server you can setup your own WebSocket proxy in
     * front of it and let people connect to your server via the proxy. You
     * are best to add another "if" statement as above for this. */

    if (location.protocol === 'https:') {
        /* Insecure WebSockets do not work over HTTPS, so we force
         * secure ones. */
        return 'wss://';
    } else {
        /* Use the default provided by Emscripten. */
        return null;
    }
} };

Module.preRun.push(function() {
    personal_dir = '/home/web_user/.openttd';
    content_download_dir = personal_dir + '/content_download'

    /* Because of the "-c" above, all user-data is stored in /user_data. */
    FS.mkdir(personal_dir);
    FS.mount(IDBFS, {}, personal_dir);

    Module.addRunDependency('syncfs');
    FS.syncfs(true, function (err) {
        Module.removeRunDependency('syncfs');
    });

    window.openttd_syncfs_shown_warning = false;
    window.openttd_syncfs = function(callback) {
        /* Copy the virtual FS to the persistent storage. */
        FS.syncfs(false, function (err) {
            /* On first time, warn the user about the volatile behaviour of
             * persistent storage. */
            if (!window.openttd_syncfs_shown_warning) {
                window.openttd_syncfs_shown_warning = true;
                Module.onWarningFs();
            }

            if (callback) callback();
        });
    }

    window.openttd_exit = function() {
        window.openttd_syncfs(Module.onExit);
    }

    window.openttd_abort = function() {
        window.openttd_syncfs(Module.onAbort);
    }

    window.openttd_bootstrap = function(current, total) {
        Module.onBootstrap(current, total);
    }

    window.openttd_bootstrap_failed = function() {
        Module.onBootstrapFailed();
    }

    window.openttd_bootstrap_reload = function() {
        window.openttd_syncfs(function() {
            Module.onBootstrapReload();
            setTimeout(function() {
                location.reload();
            }, 1000);
        });
    }

    window.openttd_server_list = function() {
        add_server = Module.cwrap("em_openttd_add_server", null, ["string"]);

        /* Add servers that support WebSocket here. Examples:
         *  add_server("localhost");
         *  add_server("localhost:3979");
         *  add_server("127.0.0.1:3979");
         *  add_server("[::1]:3979");
         */
    }

    var leftButtonDown = false;
    document.addEventListener("mousedown", e => {
        if (e.button == 0) {
            leftButtonDown = true;
        }
    });
    document.addEventListener("mouseup", e => {
        if (e.button == 0) {
            leftButtonDown = false;
        }
    });
    window.openttd_open_url = function(url, url_len) {
        const url_string = UTF8ToString(url, url_len);
        function openWindow() {
            document.removeEventListener("mouseup", openWindow);
            window.open(url_string, '_blank');
        }
        /* Trying to open the URL while the mouse is down results in the button getting stuck, so wait for the
         * mouse to be released before opening it. However, when OpenTTD is lagging, the mouse can get released
         * before the button click even registers, so check for that, and open the URL immediately if that's the
         * case. */
        if (leftButtonDown) {
            document.addEventListener("mouseup", openWindow);
        } else {
            openWindow();
        }
    }

    /* https://github.com/emscripten-core/emscripten/pull/12995 implements this
    * properly. Till that time, we use a polyfill. */
   SOCKFS.websocket_sock_ops.createPeer_ = SOCKFS.websocket_sock_ops.createPeer;
   SOCKFS.websocket_sock_ops.createPeer = function(sock, addr, port)
   {
       let func = Module['websocket']['url'];
       Module['websocket']['url'] = func(addr, port, (sock.type == 2) ? 'udp' : 'tcp');
       let ret = SOCKFS.websocket_sock_ops.createPeer_(sock, addr, port);
       Module['websocket']['url'] = func;
       return ret;
   }
});
