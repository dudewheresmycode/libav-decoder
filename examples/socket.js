const path = require("path")
const Decoder = require("../index.js")
const express = require("express")

const HTTP_PORT = 3130
const app = express()
const expressWs = require("express-ws")(app);


app.use(express.static(process.cwd() + "/examples/html"))

app.ws("/socket", function(ws, req) {
  ws.on("message", function(data) {
    var payload = JSON.parse(data);
    console.log("message!", payload)
    switch (payload.type) {
      case "decode.start":
        Decoder.open(path.resolve(payload.file), (err, fileinfo) => {
          console.log("start decoding!", )
          if(err){ return sendToClient(ws, "decode.error", {error:err.toString()}); }
          sendToClient(ws, "decode.opened", fileinfo)
          Decoder.decode(() => onDecodeReady(ws), (err) => onDecodeFinished(ws, err));
        });
        break;
      case "decode.stop":
        break;
      default:
        return;
    }
  });
});

function sendToClient(ws, type, data){
  var payload = Object.assign({}, data, {type:type})
  ws.send(JSON.stringify(payload))
}
app.listen(HTTP_PORT, () => console.log("Running on port http://localhost:%s", HTTP_PORT))

function onDecodeReady(ws){
  readVideo(ws)
  readAudio(ws)
}
function onDecodeFinished(ws, err){
  if(err){ console.log("Error: %s", err); return; }
  //finished decoding file...
  console.log('Decoding finished');
}

function readVideo(ws){
  Decoder.readVideo((frame) => {

    switch(frame.type){
      case "frame":
        /*
          Do somewith with the YUV frame data
          renderFrame(frame)
        */
        sendToClient(ws, "decode.video_frame", frame);
        return setTimeout(() => readVideo(ws), 10);
      case "finished":
        //video decoding has completed
        console.log("no more video frames");
        return;
    }
  });
}
function readAudio(ws){
  Decoder.readAudio((frame) => {
    switch(frame.type){
      case 'samples':
        console.log("Audio Samples", frame);
        // setImmediate(() => { readAudio(event); })
        sendToClient(ws, "decode.audio_frame", frame);
        return setTimeout(() => readAudio(ws), 10);
        // return setTimeout(readAudio, 10);
      case 'finished':
        console.log("no more audio frams");
        break;
    }
  });
}


// let _readTime, _readATime
// let shouldQuit = false
// let isReady = false
//
// let inputFile = "/Users/addroid/Movies/sample.mpg"
// // let inputFile = __dirname + "/assets/sintel_trailer-480p.mp4"
// //open file
// Decoder.open(path.resolve(inputFile), (err, fileinfo) => {
//
//   if(err){ console.log("Error: %s", err); return; }
//   //will return info like width, height, framerate, etc..
//   console.log(fileinfo)
//
//   //start the decode process
//   Decoder.decode(onDecodeReady, (err, response) => {
//     if(err){ console.log("Error: %s", err); return; }
//     //finished decoding file...
//     console.log('Finished')
//   });
//
// });
//
//
// function onDecodeReady(){
//   console.log('ready!');
//   readVideo();
//   readAudio();
// }
//
