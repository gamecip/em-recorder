Module = Module || {};

var _startRecording, _addVideoFrame, _endRecording;

Module.preRun = [function() {
    //startRecording(w, h);
    _startRecording = Module.cwrap("start_recording", "number", ["number", "number", "number"]);
    _addVideoFrame = Module.cwrap("add_video_frame", null, ["number", "number", "array", "number"]);
    _endRecording = Module.cwrap("end_recording", null, ["number"]);
}];

Module["startRecording"] = function(w, h, fps) {
    return _startRecording(w,h,fps);
}

Module["addVideoFrame"] = function(recordingID, frame, imageData) {
    _addVideoFrame(recordingID, frame, imageData, imageData.length);
}

Module["finishRecording"] = function(recordingID) {
    _endRecording(recordingID);
    return FS.readFile("recording-"+recordingID+".mp4", {encoding:"binary"});
}