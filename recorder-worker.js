var Recorder = {
    worker:null,
    ready:false,
    nonce:0,
    pendingCalls:[],
    pendingStarts:{},
    pendingFinishes:{}
};

Recorder.spawnRecorder = function() {
    if(Recorder.worker) { return; }
    Recorder.worker = new Worker("recorder.js");
    Recorder.worker.onmessage = function(e) {
        var data = e.data;
        if(data.type == "ready") {
            Recorder.ready = true;
            for(var i = 0; i < Recorder.pendingCalls.length; i++) {
                Recorder.pendingCalls[i]();
            }
            Recorder.pendingCalls = [];
        } else if(data.type == "started") {
            if(data.nonce in Recorder.pendingStarts) {
                Recorder.pendingStarts[data.nonce](data.recordingID);
                delete Recorder.pendingStarts[data.nonce];
            }
        } else if(data.type == "finished") {
            if(data.recordingID in Recorder.pendingFinishes) {
                Recorder.pendingFinishes[data.recordingID](new Uint8Array(data.data));
                delete Recorder.pendingFinishes[data.recordingID];
            }
        } else if(data.type == "print") {
            console.log(data.message);
        } else if(data.type == "printErr") {
            console.error(data.message);
        }
    }
};

Recorder.startRecording = function(w,h,cb) {
    if(!Recorder.worker || !Recorder.ready) {
        Recorder.spawnRecorder();
        Recorder.pendingCalls.push(function() {
            Recorder.startRecording(w,h,cb);
        });
        return;
    }
    var nonce = Recorder.nonce++;
    Recorder.pendingStarts[nonce] = cb;
    Recorder.worker.postMessage({
        type:"start",
        width:w,
        height:h,
        nonce:nonce
    });
}

Recorder.addVideoFrame = function(recordingID, imageData) {
    if(!Recorder.worker || !Recorder.ready) {
        console.error("Calling addVideoFrame too early");
    }
    Recorder.worker.postMessage({
        type:"data",
        recordingID:recordingID,
        videoBuffer:imageData.buffer
    }, [imageData.buffer]);
}

Recorder.finishRecording = function(recordingID, cb) {
    if(!Recorder.worker || !Recorder.ready) {
        console.error("Calling finishRecording too early");
    }
    Recorder.pendingFinishes[recordingID] = cb;
    Recorder.worker.postMessage({
        type:"finish",
        recordingID:recordingID
    });
}