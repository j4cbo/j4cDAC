/* j4cDAC HTML5 UI library
 *
 * Copyright 2011 Jacob Potter
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

/* post
 *
 * Post a set of values back to the server.
 *
 * XXX: Currently only works for iPad app; will need to be changed.
 */
function post(dict) {
	var cmds = [];
	for (var k in dict) {
		cmds.push(k + "=" + dict[k]);
	}

	var cmd = "ed:" + cmds.join("_");
//	window.location = cmd;
}

/* isDescendant
 *
 * Is n1 a descendant of n2?
 */
function isDescendant(n1, n2) {
	while (n1) {
		if (n1 == n2) return true;
		n1 = n1.parentNode;
	}
	return false;
}

/* fakeActive
 *
 * Set up fake "active" semantics on el: when mouse goes down, call dn(); when
 * released, call up(); if an actual click happens, call clk().
 */
function fakeActive(el, dn, up, clk) {
	up();

	if (!window.Touch) {
		el.onmousedown = function() {
			dn();
			document.onmouseup = function(e) {
				document.onmouseup = null;
				up();
				if (isDescendant(e.target, el)) clk();
			}
		}
	}

	el.ontouchstart = function() { dn(); }
	el.ontouchcancel = function() { up(); }

	el.ontouchend = function(e) {
		if (e.targetTouches.length > 0) return false;
		up();
		var te = document.elementFromPoint(e.changedTouches[0].clientX,
		                                   e.changedTouches[0].clientY);
		if (isDescendant(te, el)) clk();
	}
}

/* clickify
 *
 * A helper function for "clickable" elements.
 */
function clickify(obj, f) {
	fakeActive(obj,
		function() { obj.style.background = "#333"; },
		function() { obj.style.background = "transparent"; },
		f
	);
}

/* applyStyle
 *
 * Apply a set of CSS attributes to an element. If "html" is set, also
 * set that element's innerHTML.
 */
function applyStyle(elem, style) {
	for (var key in style) elem.style[key] = style[key];
	if (style.html) elem.innerHTML = style.html;
}

/* makeBaseElement
 *
 * Create an HTML Element for a control; set up basic properties.
 */
function makeBaseElement(elemType, obj, props, moreStyle) {
	var elem = document.createElement(elemType);
	var color = props.color ? props.color : "#f0f";
	applyStyle(elem, {
		"position": "absolute",
		"left": obj.x + "px",
		"top": obj.y + "px",
		"width": obj.width + "px",
		"height": obj.height + "px",
		"color": color,
		"border": "1px solid " + color,
		"webkitBorderRadius": "5px",
		"margin": "0",
		"padding": "0"
	});
	if (props.bs) elem.style.borderStyle = props.bs;
	if (moreStyle) applyStyle(elem, moreStyle);
	obj.ctx.appendChild(elem);
	obj.container = elem;
	return elem;
}

/* makeChildElement
 *
 * Set up a child element for a control.
 */
function makeChildElement(parentElem, elem, props) {
	elem = document.createElement(elem);
	applyStyle(elem, props);
	parentElem.appendChild(elem);
	return elem;
}

/* Widget
 *
 * Base class for widgets.
 */
function Widget(ctx, props) {
	this.ctx = ctx;

	var topBar = 40;
	var h = window.innerHeight - topBar;
	this.x = props.bounds[0] * window.innerWidth;
	this.y = props.bounds[1] * h + topBar;
	this.width = props.bounds[2] * window.innerWidth;
	this.height = props.bounds[3] * h;

	if (props.range) {
		this.min = props.range[0];
		this.max = props.range[1];
	} else {
		this.min = 0;
		this.max = 1;
	}
}

/* JSlider
 *
 * A basic vertical slider.
 */
JSlider = function(ctx, props) {
	this.__proto__ = new Widget(ctx, props);
	var root = makeBaseElement("div", this, props);
	var boxHeight = props.boxh * this.height;
	var box = makeChildElement(root, "div", {
		"height": boxHeight + "px",
		"position": "absolute",
		"left": "0px",
		"width": "100%",
		"webkitBorderRadius": "4px",
		"border": "0px none",
		"background": root.style.color
	});

	var rel = this.y + 1;
	var rangeDiff = this.max - this.min;
	var posLimit = this.height - boxHeight + 1;
	var min = this.min;
	var value = 0;

	function setBoxPosition(position, trigger) {
		if (position < 0) position = 0;
		if (position > posLimit) position = posLimit;
		box.style.top = position + "px";

		value = (position / posLimit) * rangeDiff + min;

		if (trigger && props.change) props.change(value);
	}

	function move(e) {
		setBoxPosition(e.pageY - rel - (boxHeight / 2), true);
	}      

	root.onmousedown = function(e) {
		document.onmouseup = function() {
			document.onmouseup = null;
			document.onmousemove = null;
		}
		document.onmousemove = move;
		move(e);
	}

	if (props.style) applyStyle(root, props.style);

	this.setValue = function(v) {
		var pos = (v - min) * posLimit / rangeDiff;
		setBoxPosition(pos, false);
	}

	this.setValue(0);

	this.pushUpdate = function() { if (props.change) props.change(value); }
	this.getValue = function() { return value; }

	function barTouch(e) {
		e.preventDefault();
		move(e.changedTouches[0]);
	}

	root.ontouchstart = barTouch;
	root.ontouchmove = barTouch;

	return this;
}

JMultiplier = function(ctx, props) {
	this.__proto__ = new Widget(ctx, props);
	var root = makeBaseElement("div", this, props, {
		"display": "block",
		"lineHeight": this.height - 3 + "px", "textAlign": "center",
		"fontFamily": "sans-serif", "fontSize": this.height * .25 + "px",
		"color": "white"
	});
	var ubtn = makeChildElement(root, "span", { "top": "0",
		"webkitBorderRadius": "4px 4px 0 0", "html": "&#8593;" });
	var dbtn = makeChildElement(root, "span", { "bottom": "0",
		"webkitBorderRadius": "0 0 4px 4px", "html": "&#8595;" });
	var btnStyle = {
		"width": "100%", "height": "50%", "background": "transparent",
		"position": "absolute", "webkitBoxSizing": "border-box",
		"left": "0", "color": "white",
		"fontFamily": "verdana sans-serif", "fontWeight": "bold",
		"fontSize": this.height * .25 + "px", "textAlign": "center", "lineHeight":
		this.height * .25 - 3 + "px"
	};
	applyStyle(ubtn, btnStyle);
	applyStyle(dbtn, btnStyle);
	var rtext = document.createElement("span");
        root.appendChild(rtext);

        var strs = [ "1/4", "1/3", "1/2", "1", "3/2", "2", "3", "4",
                "5", "6", "7", "8", "9", "10" ];
        this.vals = [ 1/4, 1/3, 1/2, 1, 3/2, 2, 3, 4, 5, 6, 7, 8, 9, 10 ];
	var state = 3;
	this.getStr = function() { return strs[state]; }
	this.getVal = function() { return this.vals[state]; }
	this.getState = function() { return state; }
	var onchange = props.change;

	fakeActive(ubtn,
		function() { applyStyle(ubtn, {
			"border": "5px solid " + props.color, "borderBottom": "15px solid transparent" }); },
		function() { applyStyle(ubtn, {
			"border": "5px solid transparent", "borderBottom": "15px solid transparent" }); },
		function() {
		        if (state == strs.length - 1) return;
		        state++;
	        	rtext.innerHTML = strs[state];
			if (onchange) onchange();
		}
	);

	fakeActive(dbtn,
		function() { applyStyle(dbtn, {
			"border": "5px solid " + props.color, "borderTop": "15px solid transparent" }); },
		function() { applyStyle(dbtn, {
			"border": "5px solid transparent", "borderTop": "15px solid transparent" }); },
		function() {
	        	if (state == 0) return;
	        	state--;
	        	rtext.innerHTML = strs[state];
	        	if (onchange) onchange();
		}
	);

        this.setState = function(s) {
                state = s;
                rtext.innerHTML = strs[state];
        }

        rtext.innerHTML = strs[state];

	return this;
}

JLabel = function(ctx, props) {
	this.__proto__ = new Widget(ctx, props);
	this.width += 2;
	this.height += 2;
	var root = makeBaseElement("div", this, props, {
		"display": "table",
		"textAlign": "center",
		"borderSpacing": "0",
	});
	var td = makeChildElement(root, "td", {
		"border": "0px none",
		"margin": "0", "padding": "0",
		"verticalAlign": "middle",
		"fontFamily": "verdana sans-serif",
		"fontSize": "18px",
		"color": "white"
	});

	if (props.divstyle) applyStyle(root, props.divstyle);
	if (props.tdstyle) applyStyle(td, props.tdstyle);

	this.update = function(v) { td.innerHTML = v; };
	this.update(props.value ? props.value : "");
	if (props.modifier) props.modifier(this);

	return this;
}

function JToggle(ctx, props) {
	this.__proto__ = new JLabel(ctx, props);
	this.state = 0;
	var obj = this;
	function update() {
		obj.container.firstChild.innerHTML = props.options[obj.state];
	}
	clickify(this.container, function() {
		obj.state++;
		if (obj.state == props.options.length) obj.state = 0;
		update();
		if (props.onchange) props.onchange(obj.state);
	});
	update();
}
