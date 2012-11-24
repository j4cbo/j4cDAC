/* Ether Dream socket.io <-> OSC relay
 *
 * Copyright 2012 Jacob Potter
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

var static = require('node-static')
  , http = require('http')
  , osc = require('node-osc')
  , fs = require('fs')
  , socketio = require('socket.io');
 
var ip = '255.255.255.255';
if (process.argv[2]) {
    ip = process.argv[2];
}
console.log(ip);

var oscClient1 = new osc.Client(ip, 60000);
var oscClient2 = new osc.Client('127.0.0.1', 60000);
oscClient2._sock.bind();
oscClient2._sock.setBroadcast(true);

var state = {};


function dump() {
    var kvpairs = [];
    for (var k in state) {
        kvpairs.push(k + ":" + state[k]);
    }
    return kvpairs.join(" ");
}

function updateState(msg) {
    var sections = msg.split(" ");
    for (var i in sections) {
        var c = sections[i].indexOf(":");
        if (c == -1) continue;
        state[sections[i].substr(0, c)] = sections[i].substr(c + 1);
    }
}

var staticFiles = new(static.Server)("./static", { cache: false });
var presetFiles = new(static.Server)("./presets", { cache: false });

var server = http.createServer(function(req, res) {
    if (req.url == "/state.txt") {
        res.writeHead(200, { 'Content-Type': 'text/plain' });
        res.write(dump());
        res.end();
    } else if (req.url.substr(0, 3) == "/s/") {
        req.url = req.url.substr(2);
        req.addListener('end', function() {
            presetFiles.serve(req, res);
        });
    } else {
        req.addListener('end', function() {
            staticFiles.serve(req, res);
        });
    }
}).listen(8080);

socketio.listen(server).on('connection', function (socket) {
    socket.on('message', function (msg) {
        if (msg.substr(0, 5) == "save:") {
            var fname = "presets/" + msg.substr(5).replace(/[^a-zA-Z0-9. ]/g, "") + ".preset";
            var state = dump() + "\n";
            var f = fs.openSync(fname, "w");
            fs.writeSync(f, state, 0, state.length, 0);
            fs.closeSync(f);
        } else {
            updateState(msg);
            socket.broadcast.emit('message', msg);
            console.log(dump());
            oscClient1.send('/abstract/conf', msg);
            oscClient2.send('/abstract/conf', msg);
        }
    });
});
