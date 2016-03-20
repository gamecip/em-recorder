onmessage = function(e) {
    var data = e.data;
    if(data.type == "start") {
        var rid = Module.startRecording(data.width, data.height, data.fps, data.sps);
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
            if(datum.audioBuffer) {
                var samples = new Float32Array(datum.audioBuffer);
                Module.addAudioFrame(
                    datum.recordingID, 
                    datum.frame, 
                    samples
                );
            }
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
postMessage({type:"ready"});
