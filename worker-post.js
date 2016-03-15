Module.postRun = [function() {
    postMessage({type:"ready"});
}]
onmessage = function(e) {
    var data = e.data;
    if(data.type == "start") {
        var rid = Module.startRecording(data.width, data.height);
        postMessage({
            type:"started",
            recordingID:rid,
            nonce:data.nonce
        });
    } else if(data.type == "data") {
        if(data.videoBuffer) {
            Module.addVideoFrame(
                data.recordingID, 
                new Uint8Array(data.videoBuffer)
            );
        }
        // if(data.audioBuffer) {
        //     Recorder.addAudioFrame(data.recording, new Uint8Array(data.audioBuffer));
        // }
    } else if(data.type == "finish") {
        var result = Module.finishRecording(data.recordingID);
        postMessage({
            type:"finished", 
            recordingID:data.recordingID, 
            data:result.buffer
        }, [result.buffer]);
    }
}