Module = Module || {};

var _startRecording, _addVideoFrame, _endRecording;

Module.preRun = Module.preRun || [];
Module.preRun.push(function() {
    //startRecording(w, h);
    _startRecording = Module.cwrap("start_recording", "number", ["number", "number", "number", "number"]);
    _addVideoFrame = Module.cwrap("add_video_frame", null, ["number", "number", "array"]);
    _addAudioFrame = Module.cwrap("add_audio_frame", null, ["number", "number", "array"]);
    _endRecording = Module.cwrap("end_recording", null, ["number"]);
});

Module["startRecording"] = function(w, h, fps, sps) {
    return _startRecording(w,h,fps,sps);
}

Module["addVideoFrame"] = function(recordingID, frame, imageData) {
    _addVideoFrame(recordingID, frame, imageData);
}

//assumes one second of floats, however many samples that is. frame = starting sample #
Module["addAudioFrame"] = function(recordingID, frame, audioData) {
    _addAudioFrame(recordingID, frame, audioData);
}

Module["finishRecording"] = function(recordingID) {
    _endRecording(recordingID);
    return FS.readFile("recording-"+recordingID+".mp4", {encoding:"binary"});
}