Module.arguments = [
    "red",
    "blue",
    "red", 
    "green"
];

var _addVideoFrame, _endRecording;

Module.preRun = [function() {
    _addVideoFrame = Module.cwrap("add_video_frame", null, ["array", "number"]);
    _endRecording = Module.cwrap("end_recording", null, []);
}];

Module.postRun = [function() {
    setTimeout(function() {
        Module.addColorFrame("red");
        setTimeout(function() {
            Module.addColorFrame("green");
            setTimeout(function() {
                Module.addColorFrame("blue");
                Module.showRecording(Module.finishRecording());
            }, 0);
        }, 0);
    }, 0);
}];

Module["addColorFrame"] = function(color) {
    var dim = 320*240*4;
    for(var t = 0; t < 30; t++) {
        var clr = new Uint8Array(dim);
        for(var i = 0; i < clr.length; i+=4) {
            if(color == "red") {
                clr[i+0] = 0xff;
                clr[i+1] = 0;
                clr[i+2] = 0;
            } else if(color == "green") {
                clr[i+0] = 0;
                clr[i+1] = 0xff;
                clr[i+2] = 0;
            } else if(color == "blue") {
                clr[i+0] = 0;
                clr[i+1] = 0;
                clr[i+2] = 0xff;
            }
            clr[i+3] = 0xff;
        }
        Module["addVideoFrame"](clr);
    }
}

Module["addVideoFrame"] = function(imageData) {
    _addVideoFrame(imageData, imageData.length);
}

Module["finishRecording"] = function() {
    _endRecording();
    return FS.readFile("recording-1.mp4", {encoding:"binary"});
}

Module["showRecording"] = function(data) {
    var v = document.createElement("video");
    v.src = "data:video/mp4;base64,"+base64js.fromByteArray(data);
    v.controls = true;
    v.style.width = "256px";
    v.style.height = "256px";
    document.body.appendChild(v);
    var a = document.createElement("a");
    a.text = "Download video";
    a.setAttribute("download", "recording-1.mp4");
    a.href = v.src;
    document.body.appendChild(a);
}