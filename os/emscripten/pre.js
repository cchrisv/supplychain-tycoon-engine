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
    var _build = null;
    var _buildVehicle = null;
    var _vehicleCmd = null;
    var _setBuildParam = null;
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
