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

var channels = [];
var cw = 0.08;
var master_frequency;

function relMunge(v) {
	var m = (0.45 * v * v) + 1;
	if (v < 0) m = 1/m;
	return m;
}

function relUnMunge(v) {
	var nv = (v > 1) ? v : 1/v;
	var out = Math.sqrt((nv - 1) / 0.45);
	if (v < 1) return -out;
	else return out;
}

var layout = {
	masterReadout:
		{ x: 0.01, y: 0.01, w: 0.08, h: 0.08, color: "white" },
	masterSlider:
		{ x: 0.01, y: 0.11, w: 0.08, h: 0.78, box: 34, color: "white" },
	xrotReadout:
		{ x: 0.41, y: 0.01, w: cw, h: 0.08, color: "white" },
	xrotSlider:
		{ x: 0.41, y: 0.11, w: cw, h: 0.78, box: 34, color: "white" },
	yrotReadout:
		{ x: 0.51, y: 0.01, w: cw, h: 0.08, color: "white" },
	yrotSlider:
		{ x: 0.51, y: 0.11, w: cw, h: 0.78, box: 34, color: "white" },
	modeButton:
		{ x: 0.41, y: 0.91, w: 0.18, h: 0.08, color: "white" },
	xyz: function(prefix, xb) {
		var yb = 0.01;
		var q = (prefix == "x" || prefix == "y" || prefix == "z");
		var bcolor = q ? "darkgrey" : prefix;
		var bsb = "solid";
		if (prefix == "blank") {
			bcolor = "darkgrey";
			bsb = "dashed";
		}

		return { 
			readout: {
				x: xb, y: yb, w: cw, h: 0.08,
				bs: bsb, color: q ? "white" : bcolor
			},
			absSlider: {
				x: xb, y: yb + 0.1, w: cw, h: 0.78, box: 34,
				bs: bsb, color: bcolor
			}, 
			phaseButton: {
				x: xb, y: yb + 0.1, w: cw, h: 0.08,
					bs: bsb, color: bcolor
			},
			relSlider: {
				x: xb, y: yb + 0.18, w: cw, h: 0.5, box: 34,
				bs: bsb, color: bcolor
			},
			multiplier: {
				x: xb, y: yb + 0.7, w: cw, h: 0.18,
				bs: bsb, color: bcolor
			},
			waveformButton : {
				x: xb, y: yb + 0.9, w: cw, h: 0.08,
				bs: bsb, color: bcolor
			},
			range : [ 0.1, 250 ]
		};
	},
}

function createChannel(prefix, xb, allowShape) {
	var layouts = layout.xyz(prefix, xb, allowShape);
	var relative = true;
	var isXYZ = (prefix == "x" || prefix == "y" || prefix == "z");
	var frequency = 100;

	var absSlider, relSlider, offSlider, phaseSlider, multiplier, phaseButton, wfmButton;

	function updateVisibility() {
		var rel = relative;
	 	absSlider.style.display = (rel || prefix == "blank") ? "none" : "block";
	 	relSlider.style.display = (rel && phaseButton.state == 0) ? "block" : "none";
	 	offSlider.style.display = (rel && phaseButton.state == 1) ? "block" : "none";
	 	phaseSlider.style.display = (rel && phaseButton.state == 2) ? "block" : "none";
	       	multiplier.style.display = rel ? "block" : "none";
		phaseButton.style.display = rel ? "table" : "none";
		wfmButton.style.display = (rel || isXYZ) ? "table" : "none";
	}

	var readout = createButton(null, layouts.readout,
		function(root, layout, td) {
			td.style.fontSize = "14px";
			root.update = function(v) { td.innerHTML = v; };
			root.style.color = "white";

			clickify(root, function() {
				relative = !relative;
				updateVisibility();
				if (relative)
					updateFromRelState();
				else {
					if (isXYZ) frequency = absSlider.value;
					updateReadouts();
				}
			});
		}
	);

	var inhibit = false;

	function writeToDict(d) {
		if (!relative && prefix == "blank")
			d[prefix] = "0";
		else if (!relative && !isXYZ) {
			d[prefix] = 0;
			d[prefix + "mul"] = (absSlider.value * 655.36).toFixed();
		} else if (relative && phaseButton.state == 2) {
			d[prefix] = (multiplier.state + ":" + (phaseSlider.value * 2147483647 / 180).toFixed());
			if (!isXYZ && prefix != "blank") d[prefix + "mul"] = 65536;
		} else {
			d[prefix] = (frequency * 65536).toFixed();
			if (!isXYZ && prefix != "blank") d[prefix + "mul"] = 65536;
		}
	}

	function updateReadouts() {
		var str = (frequency.toFixed(1) + " Hz<br>"
			+ "<span class='fcalc'>" + multiplier.getStr() + "<i>f</i>");

		if (prefix == "blank" && !relative) {
			str = "";
		} else if (!isXYZ && !relative) {
			str = absSlider.value.toFixed(0) + "%</span>";
		} else if (!relative || phaseButton.state == 0) {
			str += " &times; " + (100*relMunge(relSlider.value)).toFixed(1) + "%</span>";
		} else if (phaseButton.state == 1) {
			var v = offSlider.value;
			if (v >= 0)
				str += " + " + v.toFixed(2) + " Hz</span>";
			else
				str += " - " + (-v).toFixed(2) + " Hz</span>";
		} else {
			var deg = phaseSlider.value;
			if (deg >= 0) {
				str += " + " + deg.toFixed(0) + "&deg;</span>";
			} else {
				str += " - " + (-deg).toFixed(0) + "&deg;</span>";
			}
		}

		readout.update(str);

		if (inhibit) {
			writeToDict(inhibit);
		} else {
			var k = {};
			writeToDict(k);
			post(k);
		}
	}

	function updateFromAbs(v) {
		if (!isXYZ) {
			updateReadouts();
			return;
		}

		var mast = master_frequency;
		if (!mast) return;

		frequency = v;
		var vratio = v/mast;

		var vals = multiplier.getVals();
		var bestRatio = 99999, besti = 0;
		for (var i = 0; i < vals.length; i++) {
			var r = vals[i] / vratio;
			var ratio = (r > 1 ? r : 1/r);
			if (ratio < bestRatio) {
				bestRatio = ratio;
				besti = i;
			}
		}

		var r = v / (mast * vals[besti]);

		multiplier.setState(besti);
		relSlider.setValue(relUnMunge(r));
		offSlider.setValue(v - (mast * vals[besti]));
		updateReadouts();
	}

	function updateFromRel(v) {
		var base = master_frequency * multiplier.getVals()[multiplier.state];
		var vv = base * relMunge(v);
		frequency = vv;
		if (isXYZ) absSlider.setValue(vv);	
		offSlider.setValue(vv - base);
		updateReadouts();
	}

	function updateFromOffs(v) {
		var base = master_frequency * multiplier.getVals()[multiplier.state];
		var vv = base + v;
		frequency = vv;
		if (isXYZ) absSlider.setValue(vv);	
		relSlider.setValue(relUnMunge(vv / base));
		updateReadouts();
	}

	function updateFromPhase(v) {
		frequency = master_frequency * multiplier.getVals()[multiplier.state];
		updateReadouts();
	}

	function updateFromRelState() {
		if (phaseButton.state == 0)
			updateFromRel(relSlider.value);
		else if (phaseButton.state == 1)
			updateFromOffs(offSlider.value);
		else
			updateFromPhase(phaseSlider.value);
	}

	absSlider = createSlider(prefix + "abs",
		layouts.absSlider, isXYZ ? layouts.range : [ 100, 0 ], updateFromAbs);
	relSlider = createSlider(prefix + "rel",
		layouts.relSlider, [ -1, 1 ], updateFromRel);
	offSlider = createSlider(prefix + "offs",
		layouts.relSlider, [ -2, 2 ], updateFromOffs);
	phaseSlider = createSlider(prefix + "phase",
		layouts.relSlider, [ -180, 180 ], updateFromPhase);
	multiplier = createMultiplier(prefix + "relm",
		layouts.multiplier, function() { updateFromRel(relSlider.value); }
	);

	phaseButton = createMultistateButton(prefix + "phase", layouts.phaseButton,
		[ " <i>f</i> &times;", " <i>f</i> &pm;", "&#8709;" ],
		function(root, layout, td) {
		root.style.fontSize = "30px";
		root.style.fontFamily = "serif";
		root.updateCallback = function() {
			updateFromRelState();
			updateVisibility();
		}
	});

	var waveforms = isXYZ ? [ "sin", "rtri", "stri", "rsq" ]
	                      : [ "sin", "+tri", "-tri", "10%", "25%", "50%", "75%", "90%" ];

	wfmButton = createMultistateButton(prefix + "waveform", layouts.waveformButton,
		waveforms, function(root, layout, td) {
		td.style.fontSize = "20px";
		root.updateCallback = function() {
			var d = {};
			d[prefix + "wfm"] = root.state;
			post(d);
		}
	});

	channels.push({ updateChannel: function(d) { 
		inhibit = d;
		if (relative) {
			updateFromRel(relSlider.value); 
		} else {
			updateFromAbs(frequency);
		}
		inhibit = false;
	} });

	absSlider.setValue(100);
	relSlider.setValue(prefix == "x" ? relUnMunge(1.001) : 0);
	offSlider.setValue(0);
	phaseSlider.setValue(0);
	updateVisibility();
}

var modeButton = createButton("mode",
	layout.modeButton, function(root, layout, td) {

	root.state = 1;
	root.style.fontSize = "20px";
	function update() {
		td.innerHTML = "MODE " + root.state;
		post({mode: root.state});
	}
	clickify(root, function() {
		root.state = root.state + 1;
		if (root.state == 7) root.state = 1;
		update();
		});
	update();
});

createChannel("x",     0.11, false);
createChannel("y",     0.21, false);
createChannel("z",     0.31, false);
createChannel("red",   0.61, true);
createChannel("green", 0.71, true);
createChannel("blue",  0.81, true);
createChannel("blank", 0.91, true);

var xrotReadout = createReadout("xrot", layout.xrotReadout);
var yrotReadout = createReadout("yrot", layout.yrotReadout);
var xrotSlider = createSlider("xrots", layout.xrotSlider, [ -90, 90 ],
	function(v) {
		xrotReadout.update("X: " + v.toFixed(1) + "&deg;");
		post({xrot: (v * 2147483647 / 180).toFixed()});
	}
);
var yrotSlider = createSlider("yrots", layout.yrotSlider, [ -90, 90 ],
	function(v) {
		yrotReadout.update("Y: " + v.toFixed(1) + "&deg;");
		post({yrot: (v * 2147483647 / 180).toFixed()});
	}
);
xrotSlider.setValue(0);
yrotSlider.setValue(0);
xrotSlider.pushUpdate();
yrotSlider.pushUpdate();

var masterReadout;

function set_master(f) {
	master_frequency = f;
	var d = { master: (f * 65536).toFixed() };
	//masterReadout.update(f.toFixed(1) + " Hz");
	for (var i = 0; i < channels.length; i++) {
		channels[i].updateChannel(d);
	}
	post(d);
}

var freqs = [ 75, 100, 50 ];
masterReadout = createMultistateButton("master", layout.masterReadout,
	[ "75 Hz", "100 Hz", "50 Hz" ],
	function(root, layout, td) {
		root.style.color = "white";
		root.updateCallback = function() {
			set_master(freqs[root.state]);
		}
	}
);

set_master(75);

//createQuadrilateral("image", { x : 250, y: 120, w: 450, h: 450 });
