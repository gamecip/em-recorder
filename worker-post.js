Module.postRun = [function() {
    postMessage({type:"ready"});
}]
onmessage = function(e) {
    var data = e.data;
    if(data.type == "start") {
        var rid = Module.startRecording(data.width, data.height, data.fps);
        postMessage({
            type:"started",
            recordingID:rid,
            nonce:data.nonce
        });
    } else if(data.type == "data") {
        var messages = data.messages;
        console.log("Add frames",messages[0].frame,messages[messages.length-1].frame);
        for(var i = 0; i < messages.length; i++) {
            var datum = messages[i];
            if(datum.videoBuffer) {
                Module.addVideoFrame(
                    datum.recordingID,
                    datum.frame,
                    new Uint8Array(datum.videoBuffer)
                );
            }
            // if(datum.audioBuffer) {
            //     Recorder.addAudioFrame(datum.recordingID, new Uint8Array(datum.audioBuffer));
            // }
        }
        console.log("Done!");
    } else if(data.type == "finish") {
        var result = Module.finishRecording(data.recordingID);
        postMessage({
            type:"finished", 
            recordingID:data.recordingID, 
            data:result.buffer
        }, [result.buffer]);
    }
}