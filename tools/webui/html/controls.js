/* j4cDAC abstract generator UI
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

var inhibit = true;
var controls = [];
var b = document.body;

/* Master frequency */
var masterreadout = new JLabel(b, {
	"bounds": [ .01, .01, .08, .08 ]
});
var master = new JSlider(b, {
	"bounds": [ .01, .11, .08, .72 ], "boxh": .05/.75,
	"range": [ 0, 250 ],
	"change" : function(v) {
                masterreadout.update(v.toFixed(1) + " Hz");
	}
});

/* X and Y rotation controls */
var xrotlabel = new JLabel(b, {
	"bounds": [ .41, .01, .08, .08 ], "color": "white"
});
var xrot = new JSlider(b, {
	"bounds": [ .41, .11, .08, .72 ], "boxh": .0625,
	"color": "white", "range": [ -90, 90 ],
	"change": function(v) {
		xrotlabel.update("X: " + v.toFixed(1) + "&deg;");
		if (!inhibit) post({ "xrot": (v * 2147483647 / 180).toFixed() });
	}
});
var yrotlabel = new JLabel(b, {
	"bounds": [ .51, .01, .08, .08 ], "color": "white"
});
var yrot = new JSlider(b, {
	"bounds": [ .51, .11, .08, .72 ], "boxh": .0625,
	"color": "white", "range": [ -90, 90 ],
	"change": function(v) {
		yrotlabel.update("Y: " + v.toFixed(1) + "&deg;");
		if (!inhibit) post({ "yrot": (v * 2147483647 / 180).toFixed() });
	}
});

/* Go/stop button */
var gostop = new JLabel(b, {
	"bounds": [ .01, .85, .08, .14 ],
	"divstyle": { "border": "8px solid green" },
	"tdstyle": { "fontFamily": "sans-serif" },
	"color": "blue", "modifier": function(obj) {
		obj.state = false;
		function update() {
			obj.container.style.borderColor = obj.state ? "red" : "green";
			obj.container.firstChild.innerHTML = obj.state ? "LASER ON" : " ";
		}
		clickify(obj.container, function() {
			obj.state = !obj.state;
			/* XXX temporary */
			window.location.reload();
			update();
		});
		update();
	}
});

/* Mode */
var mode = new JToggle(b, {
	"bounds": [ .41, .85, .18, .06 ],
	"tdstyle": { "fontFamily": "sans-serif" },
	"color": "white",
	"options": [ "MODE 1", "MODE 2", "MODE 3", "MODE 4", "MODE 5", "MODE 6" ],
	"onchange": function(v) { post({ "mode": v }); }
});

master.setValue(100);
master.pushUpdate();
xrot.setValue(0);
xrot.pushUpdate();
yrot.setValue(0);
yrot.pushUpdate();

function createChannel(prefix, xb, yb) {
	var isAxis = (prefix == "x" || prefix == "y" || prefix == "z");
	var bcolor = (isAxis || prefix == "blank") ? "darkgrey" : prefix;
	var bs = (prefix == "blank") ? "dashed" : "solid";
	var cw = .08;

	var absSlider, relSlider, phaseSlider, readout;
	var absButton, phaseButton, multiplier, wfmButton;

	var isEnabled = true;

	var mulRangeConst = 0.2;

	function relMunge(v) {
	        var m = (mulRangeConst * v * v) + 1;
	        if (v < 0) m = 1/m;
	        return m;
	}

	function relUnMunge(v) {
		if (v > 1) {
			return Math.sqrt((v - 1) / mulRangeConst);
		} else {
			return -Math.sqrt((1 / v - 1) / mulRangeConst);
		}
	}

	function writeToDict(d) {
	        d[prefix] = (phaseButton.state
	                ? (multiplier.getState() + "=" + (phaseSlider.getValue() * 2147483647 / 180).toFixed())
	                : (absSlider.getValue() * 65536).toFixed());
	}

	function updateReadouts() {
		if (!(absSlider && relSlider)) return;

		var ast = absButton.state;
		absSlider.container.style.display = (isEnabled && ast) ? "block" : "none";
		relSlider.container.style.display = (isEnabled && !ast && !phaseButton.state) ? "block" : "none";
		phaseSlider.container.style.display = (isEnabled && !ast && phaseButton.state) ? "block" : "none";
		phaseButton.container.style.display = (isEnabled && !ast) ? "table" : "none";
		multiplier.container.style.display = (isEnabled && !ast) ? "block" : "none";
		absButton.container.style.display = isEnabled ? "table" : "none";
		wfmButton.container.style.display = isEnabled ? "table" : "none";

		var str = (absSlider.getValue().toFixed(1) + " Hz<br>"
			+ "<small>" + multiplier.getStr() + "<i>f</i>");

		if (phaseButton.state && !absButton.state) {
			var deg = phaseSlider.getValue();
			if (deg >= 0) {
				str += " + " + deg.toFixed(0) + "&deg;";
			} else {
				str += " - " + (-deg).toFixed(0) + "&deg;";
			}   
		} else {
			str += " * " + (100*relMunge(relSlider.getValue())).toFixed(1) + "%</small>";
		}

		if (!isEnabled)
			str = "Disabled";

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
		var bv = absSlider.getValue();
		var rv = master.getValue();
		if (!rv) return;

		var vratio = bv/rv;

		var vals = multiplier.vals;
		var bestRatio = 99999, besti = 0;
		for (var i = 0; i < vals.length; i++) {
			var r = vals[i] / vratio;
			var ratio = (r > 1 ? r : 1/r);
			if (ratio < bestRatio) {
				bestRatio = ratio;
				besti = i;
			}
		}

		multiplier.setState(besti);
		var ratio = bv / (rv * vals[besti]);
		relSlider.setValue(relUnMunge(ratio));
		updateReadouts();
	}

	function updateFromRel(v) {
	        var m = relMunge(v);
	        var vv = m * multiplier.getVal() * master.getValue();
	        absSlider.setValue(vv);
	        updateReadouts();
	}

        function updateFromPhase(v) {
                absSlider.setValue(master.getValue() * multiplier.getVal());
                updateReadouts();
        }

	/* Readout */
	readout = new JLabel(b, {
		"bounds": [ xb, yb, cw, .08 ], "bs": bs,
		"color": isAxis ? "white" : bcolor,
		"modifier": function(obj) {
			if (isAxis) return;
			obj.container.firstChild.innerHTML = "&nbsp;";
			clickify(obj.container, function() {
				isEnabled = !isEnabled;
				updateReadouts();
			});
		},
		"tdstyle": { "fontSize": "100%" }
	});

	/* Absolute-mode slider */
	absSlider = new JSlider(b, {
		"style": { "display": "none" },
		"range": [ 0, 250 ],
		"bounds": [ xb, yb + .1, cw, .72 ],
		"boxh": .05/.75,
		"bs": bs, "color": bcolor, "change": updateFromAbs
	});

	/* Relative-mode slider */
	relSlider = new JSlider(b, {
		"bounds": [ xb, yb + .18, cw, .46 ],
		"range": [ -1, 1 ],
		"boxh": .05/.45,
		"bs": bs, "color": bcolor,
		"change": updateFromRel
	});

	/* Relative-mode multiplier */
	multiplier = new JMultiplier(b, {
		"bounds": [ xb, yb + .66, cw, .16 ],
		"bs": bs, "color": bcolor,
		"change": function() { updateFromRel(relSlider.getValue()); }
	});

	/* Phase slider */
	phaseSlider = new JSlider(b, {
		"bounds": [ xb, yb + .18, cw, .46 ],
		"boxh": .05/.45,
		"range": [ -180, 180 ],
		"style": { "display": "none" },
		"bs": bs, "color": bcolor, "change": updateFromPhase
	});

	/* ABS/REL button */
	absButton = new JToggle(b, {
		"bounds": [ xb, yb + .84, .08, .06 ],
		"tdstyle": { "fontFamily": "sans-serif" },
		"color": bcolor, "bs": bs,
		"options": [ "REL", "ABS" ],
		"onchange": function() { updateReadouts(); }
	});

	/* Phase button */
	phaseButton = new JToggle(b, {
		"bounds": [ xb, yb + .1, .08, .06 ],
		"tdstyle": { "fontFamily": "serif", "fontSize": "30px" },
		"color": bcolor, "bs": bs,
		"options": [ " <i>f</i> &times;", "&#8709;" ],
		"onchange": function(v) {
			if (v.state) updateFromPhase(phaseSlider.getValue());
			else updateFromRel(relSlider.getValue());
		}
	});

	/* Waveform button */
	wfmButton = new JToggle(b, {
		"bounds": [ xb, yb + .92, .08, .06 ],
		"tdstyle": { "fontFamily": "sans-serif" },
		"bs": bs, "color": bcolor,
		"options": [ "sin", "saw", "tri", "sq25", "sq50", "sq75" ]
	});

	updateFromRel(0);
}

createChannel("x", .11, .01);
createChannel("y", .21, .01);
createChannel("z", .31, .01);
createChannel("red", .61, .01);
createChannel("green", .71, .01);
createChannel("blue", .81, .01);
createChannel("blank", .91, .01);
inhibit = false;
