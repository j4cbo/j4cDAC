// Platform detection
var pointerApi = "portable";
if (window.navigator.msPointerEnabled) {
    pointerApi = "mspointer";
}

var socketApi = "socket.io";
if ("Windows" in window) {
    socketApi = "winrt";
}
