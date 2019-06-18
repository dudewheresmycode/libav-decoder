const files = {
  mp4: "examples/assets/sintel_trailer-480p.mp4",
  avi: "examples/assets/sintel_trailer-480p.avi"
}
let socket
let pcm_player
console.log("PCMPlayer", window.PCMPlayer);

document.addEventListener("DOMContentLoaded", () => {
  console.log('content loaded!');
  var buttons = document.querySelectorAll(".play-button")
  buttons.forEach( b => {
    b.addEventListener('click', handleButtonClick)
  })

  socket = new WebSocket("ws://localhost:3130/socket")
  socket.addEventListener('open', () => {
    console.log('connected!')
  })
  socket.addEventListener('close', () => {
    console.log('closed!')
  })
  socket.addEventListener('message', (msg) => {
    var payload = JSON.parse(msg.data);
    // console.log('message!', payload)
    switch (payload.type) {
      case 'decode.opened':
        console.log("OPENED!", payload);
        pcm_player = new PCMPlayer({
            encoding: '16bitInt',
            channels: payload.audio.channels,
            sampleRate: payload.audio.sampleRate,
            flushingTime: 2000
        });
        break;
      case 'decode.ready':
        console.log("start decoding!")
        break;
      case 'decode.video_frame':
        console.log("video_frame")
        break;
      case 'decode.audio_frame':
        //payload.buffer
        var buffer = new Uint8Array(payload.buffer.data);
        console.log("audio_frame", buffer)
        pcm_player.feed(buffer);
        break;
      default:
        return;
    }

  })


})
function sendToServer(payload){
  if(socket){ socket.send(JSON.stringify(payload)) }
}
function handleButtonClick(e){
  var key = this.getAttribute('data-key')
  var file = files[key]
  console.log(file)
  sendToServer({type:"decode.start", file:file});
}
