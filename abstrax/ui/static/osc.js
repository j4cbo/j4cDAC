/* Ether Dream abstract controller
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

var socket = io.connect();
var topBar = 0;

function clickify(elem, f) {
	if (window.Touch)
		elem.ontouchend = f;
	else
		elem.onclick = f;
}

function post(kv) {
	var kvpairs = [];

	for (var k in kv) {
		kvpairs.push(k + ":" + kv[k]);
	}

	socket.send(kvpairs.join(" "));
}

function applyLayout(root, layout) {
	var topBar = 0;
	var h = window.innerHeight - topBar;
	root.x =  (layout.x * window.innerWidth);
	root.y = (layout.y * h) + topBar;
	root.w = layout.w * window.innerWidth;
	root.h = layout.h * h;
	root.style.left = root.x + "px";
	root.style.top = root.y + "px";
	root.style.width = root.w + "px";
	root.style.height = root.h + "px";
	if (layout.color) root.style.borderColor = layout.color;
	if (layout.bs) root.style.borderStyle = layout.bs;
	if (layout.square == "bottom") {
		root.style.WebkitBorderBottomLeftRadius = 0;
		root.style.WebkitBorderBottomRightRadius = 0;
	}
	if (layout.square == "top") {
		root.style.WebkitBorderTopLeftRadius = 0;
		root.style.WebkitBorderTopRightRadius = 0;
	}
}

function getParent(layout) {
	return layout.parent ? document.getElementById(layout.parent) : document.body;
}

function createReadout(elemName, layout) {
	var root = document.createElement("div");
	getParent(layout).appendChild(root);

	root.className = "readout";
	root.style.color = "white";
	applyLayout(root, layout);
	root.id = elemName + "_readout";

	var span = document.createElement("td");
	root.appendChild(span);
	root.update = function(v) { span.innerHTML = v; };

	return root;
}

function createSlider(elemName, layout, range, onchange) {
	var root = document.createElement("div");
	var box = document.createElement("div");
	root.appendChild(box);
	root.id = elemName;
	getParent(layout).appendChild(root);

	root.className = "sliderv";	

	var boxh = layout.box ? layout.box : 50;

	if (layout.horizontal) {
		box.style.width = boxh + "px";
	} else {
		box.style.height = boxh + "px";
	}

	applyLayout(root, layout);
	if (layout.color) box.style.background = layout.color;

	var rel = (layout.horizontal ? root.x : root.y) + 1;
	var rangeDiff = range[0] - range[1];
	var posLimit = (layout.horizontal ? root.w : root.h) - boxh;

	function setPos(position, trigger) {
		if (position < 0) {
			position = 0;
		}
		if (position > posLimit) {
			position = posLimit;
		}

		if (layout.horizontal) {
			box.style.left = position + "px";
		} else {
			box.style.top = position + "px";
		}

		root.value = (position / posLimit) * rangeDiff + range[1];
		if (trigger) {
			onchange(root.value);
		}
	}

	function move(e) {
		setPos((layout.horizontal ? e.pageX : e.pageY) - rel - (boxh / 2), true);
	}	

	function up() {
		document.onmouseup = null;
		document.onmousemove = null;
	}

	function barDown(e) {
		document.onmouseup = up;
		document.onmousemove = move;
		move(e);
	}

	root.setValue = function(v) {
		var pos = (v - range[1]) * posLimit / rangeDiff;
		setPos(pos, false);
		root.value = v;
	}

	root.pushUpdate = function() { if (onchange) onchange(root.value); }

	root.getValue = function() { return root.value; }

	function barTouch(e) {
		e.preventDefault();
		move(e.changedTouches[0]);
	}

	root.onmousedown = barDown;
	root.ontouchstart = barTouch;
	root.ontouchmove = barTouch;

	return root;
}

function roundRect(ctx, x1, y1, x2, y2, r) {
	var pi = Math.PI;
	ctx.beginPath();
	ctx.moveTo(x1 + r, y1);
    	ctx.lineTo(x2 - r, y1);
	ctx.arc(x2 - r, y1 + r, r, pi * 3/2, pi * 2, false);
	ctx.lineTo(x2, y2 - r);
	ctx.arc(x2 - r, y2 - r, r, 0, pi / 2, false);
	ctx.lineTo(x1 + r, y2);
	ctx.arc(x1 + r, y2 - r, r, pi / 2, pi, false);
	ctx.lineTo(x1, y1 + r);
	ctx.arc(x1 + r, y1 + r, r, pi, pi * 3/2, false);
	ctx.closePath();
}

function createQuadrilateral(elemName, layout) {
	/* Set up a canvas */
	var root = document.createElement("canvas");
	document.body.appendChild(root);
	var ctx = root.getContext('2d');

	root.className = "quadrilateral";

	root.width = layout.w;
	root.height = layout.h;
	applyLayout(root, layout);

	/* Local variables */
	var corners = [ [ 0, 0 ], [ 0, 1 ], [ 1, 1 ], [ 1, 0 ] ];
	var cornerProxies = [];
	var boxh = 50;
	var proxyh = 70;

	function coords(corner) {
		var c = corners[corner];
		return {
			x : c[0] * (layout.w - boxh) + (boxh / 2),
			y : c[1] * (layout.h - boxh) + (boxh / 2)
		};
	}

	function draw() {
		ctx.clearRect(0, 0, layout.w, layout.h);
		ctx.beginPath();
		for (var i = 0; i < 4; i++) {
			var b = coords(i);
			if (i) ctx.lineTo(b.x, b.y);
			else ctx.moveTo(b.x, b.y);
		}
		ctx.closePath();
		ctx.lineWidth = 1;
		ctx.strokeStyle = "white";
		ctx.stroke();
		for (var i = 0; i < 4; i++) {
			var b = coords(i);
			roundRect(ctx, b.x - (boxh/2), b.y - (boxh/2), b.x + (boxh/2), b.y + (boxh/2), 4); 
			ctx.fillStyle = "blue";
			ctx.fill();
		}
	}

	function setProxyPos(cp, b) {
		cp.style.left = (b.x + layout.x - proxyh/2 + 1) + "px";
		cp.style.top = (b.y + layout.y - proxyh/2 + 1) + "px";
	}

	/* Drag handling */
	function setPos(i, x, y) {
		if (x < 0) x = 0;
		if (x > 1) x = 1;
		if (y < 0) y = 0;
		if (y > 1) y = 1;

		var limit = proxyh / (layout.w - boxh - 2);
		var kicks;

		do {
			kicks = false;
			for (var j = 0; j < 4; j++) {
				if (j == i) continue;
				var diffX = x - corners[j][0];
				var diffY = y - corners[j][1];
	
				if (Math.abs(diffX) >= limit || Math.abs(diffY) >= limit)
					continue;
	
			//	kicks = true;

				if (Math.abs(diffX) > Math.abs(diffY)) {
					if (diffX < 0) {
						x = corners[j][0] - limit;
					} else {
						x = corners[j][0] + limit;
					}
				} else {
					if (diffY < 0) {
						y = corners[j][1] - limit;
					} else {
						y = corners[j][1] + limit;
					}
				}
			}
		} while (kicks);


		var cp = cornerProxies[i];
		corners[i][0] = x;
		corners[i][1] = y;
		setProxyPos(cp, coords(i));
		draw();
	}
		
	function move(i, e) {
		setPos(i,
			(e.pageX - layout.x - boxh/2 - 1) / (layout.w - boxh),
			(e.pageY - layout.y - boxh/2 - 1) / (layout.h - boxh)
		);
	}	

	function up() {
		document.onmouseup = null;
		document.onmousemove = null;
	}

	/* Make some proxy elements for the corners */
	for (var i = 0; i < 4; i++) {
		var cp = document.createElement("div");
		var b = coords(i);
		cp.className = "quadrilateralproxy";
		document.body.appendChild(cp);
		setProxyPos(cp, coords(i));
		cp.style.width = proxyh + "px";
		cp.style.height = proxyh + "px";
		cp.proxyIndex = i;
		cp.onmousedown = (function(i) {
			return function(e) {
				document.onmouseup = up;
				document.onmousemove = function(e) { move(i, e) };
				move(i, e);
			}
		})(i);

		touch = (function(i) {
			return function(e) {
				e.preventDefault();
				move(i, e.changedTouches[0]);
			};
		})(i);

		cp.ontouchstart = touch;
		cp.ontouchmove = touch;

		cornerProxies.push(cp);
	}

	draw();
}

function createMultiplier(elemName, layout, onchange) {
	var root = document.createElement("div");
	getParent(layout).appendChild(root);
	root.id = elemName;

	var ubtn = document.createElement("span");
	ubtn.className = "ubtn ns " + layout.color;
	ubtn.innerHTML = "&#8593;";
	var dbtn = document.createElement("span");
	dbtn.className = "dbtn ns " + layout.color;
	dbtn.innerHTML = "&#8595;";

	var rtext = document.createElement("span");

	root.appendChild(rtext);
	root.appendChild(ubtn);
	root.appendChild(dbtn);

	root.className = "multiplier";	

	applyLayout(root, layout);

	var strs = [ "1/4", "1/3", "1/2", "1", "3/2", "2", "3", "4",
		"5", "6", "7", "8", "9", "10" ];
	var vals = [ 1/4, 1/3, 1/2, 1, 3/2, 2, 3, 4, 5, 6, 7, 8, 9, 10 ];

	root.getVals = function() { return vals; }
	root.getStr = function() { return strs[root.state]; }

	root.state = 3;

	clickify(ubtn, function() {
		if (root.state == strs.length - 1) return;
		root.state++
		rtext.innerHTML = strs[root.state];
		if (onchange) onchange();
	});

	clickify(dbtn, function() {
		if (root.state == 0) return;
		root.state--;
		rtext.innerHTML = strs[root.state];
		if (onchange) onchange();
	});

	root.setState = function(s) {
		root.state = s;
		rtext.innerHTML = strs[root.state];
	}

	function f() { return false; }
	ubtn.onmousemove = f;
	dbtn.onmousemove = f;

	rtext.innerHTML = strs[root.state];

	return root;
}

function createButton(elemName, layout, buttonModifier) {
	var root = document.createElement("div");
	if (elemName)
		root.id = elemName;

	getParent(layout).appendChild(root);
	applyLayout(root, layout);

	root.className = "button " + layout.color;

	var td = document.createElement("td");
	root.appendChild(td);

	if (buttonModifier) buttonModifier(root, layout, td);

	return root;
}

function createMultistateButton(elemName, layout, states, modifier) {
	return createButton(elemName, layout, function(root, layout, td) {
		modifier(root, layout, td);
		root.state = 0;
		root.states = states;
		td.innerHTML = states[0];
		clickify(root, function() {
			root.state = root.state + 1;
			if (root.state >= states.length)
				root.state = 0;
			td.innerHTML = states[root.state];
			root.updateCallback();
		});
	});
}

function lockToggle(elem, layout) {
	var c = false;
	elem.style.color = layout.color ? layout.color : "#800";
	elem.style.fontSize = "50px";
	elem.style.lineHeight = "50%";
	clickify(elem, function() {
		c = !c;
		elem.innerHTML = c ? "\u2248" : "";
	});
}

addEventListener("load", function() {
	setTimeout(function() { 
		window.scrollTo(0, 1); 
	}, 100);
});
