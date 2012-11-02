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
  , socketio = require('socket.io');
 
var ip = '255.255.255.255';
if (process.argv[2]) {
    ip = process.argv[2];
}
console.log(ip);

var oscClient1 = new osc.Client(ip, 60000);
var oscClient2 = new osc.Client('255.255.255.255', 60000);
oscClient2._sock.bind();
oscClient2._sock.setBroadcast(true);

var file = new(static.Server)("./static", { cache: false });

var server = http.createServer(function(req, res) {
    req.addListener('end', function() {
        file.serve(req, res);
   });
}).listen(8080);
 
socketio.listen(server).on('connection', function (socket) {
    socket.on('message', function (msg) {
        console.log(msg);
        oscClient1.send('/abstract/conf', msg);
        oscClient2.send('/abstract/conf', msg);
    });
});
