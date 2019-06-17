const path = require('path')
const Decoder = require('../index.js')

let _readTime, _readATime
let shouldQuit = false
let isReady = false

//open file
Decoder.open(path.resolve(__dirname + "/assets/sintel_trailer-480p.mp4"), (err, fileinfo) => {
  if(err){ console.log("Error: %s", err); return; }
  //will return info like width, height, framerate, etc..
  console.log(fileinfo)

  //start the decode process
  Decoder.decode(onDecodeReady, (err, response) => {
    if(err){ console.log("Error: %s", err); return; }
    //finished decoding file...
    console.log('Finished')
  });

});


function onDecodeReady(){
  readVideo();
  readAudio();
}

function readVideo(){
  Decoder.readVideo((frame) => {

    switch(frame.type){
      case "frame":
        /*
          Do somewith with the YUV frame data
          renderFrame(frame)
        */
        console.log("Video Frame", frame);
        // read another frame as fast as possible
        // setImmediate(() => { readVideo(event); });
        return setTimeout(readVideo, 10);
      case "finished":
        //video decoding has completed
        console.log("finished decoding video");
        return;
    }
  });
}
function readAudio(){
  Decoder.readAudio((frame) => {
    switch(frame.type){
      case 'samples':
        console.log("Audio Samples", frame);
        // setImmediate(() => { readAudio(event); })
        return setTimeout(readAudio, 10);
      case 'finished':
        console.log("finished decoding audio");
        break;
    }
  });
}
