Module = Module || {};

var _startRecording, _addVideoFrame, _endRecording;

Module.preRun = Module.preRun || [];
Module.preRun.push(function() {
    console.log("PRERUN");
    //startRecording(w, h);
    Module["startRecording"] = Module.cwrap("start_recording", "number", ["number", "number", "number", "number", "number"]);
    Module["addVideoFrame"] = Module.cwrap("add_video_frame", null, ["number", "number", "array"]);
//assumes one second of floats, however many samples that is. frame = starting sample #
    _addAudioFrame = Module.cwrap("add_audio_frame", null, ["number", "number", "array"]);
    Module._endRecording = Module.cwrap("end_recording", null, ["number"]);
});

Module["addAudioFrame"] = function(recordingID, frame, samples) {
    return _addAudioFrame(recordingID,frame,new Uint8Array(samples.buffer));
}

Module["finishRecording"] = function(recordingID) {
    Module._endRecording(recordingID);
    return FS.readFile("recording-"+recordingID+".mp4", {encoding:"binary"});
}
