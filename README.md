# libav-decoder

A basic audio/video decoder for nodejs using libav.

The module provides basic asynchronous audio / video decoding in javascript using [libav](https://www.libav.org/). It outputs raw planar YUV (YCbCr) data for video frames, and raw PCM data.


To display the YUV data, checkout @brion/yuv-buffer and @samirkumardas's [pcm-player](https://github.com/samirkumardas/pcm-player)

Here's an basic example using those two libraries.
```javascript
let probe
//init
const pcm_audio = new PCMPlayer({
    encoding: '16bitInt', //outputs 16-bit raw PCM
    channels: probe.audio.channels,
    sampleRate: probe.audio.sample_rate,
    flushingTime: 2000
});

decoder.open(inputFile, (err, result) => {
  probe = result
  decoder.decode(onReady, onComplete)
})

function onReady(){
}
function onComplete(){
  console.log('Done!');
}

function readVideo(){
  decoder.readVideo( frame => {
    var yuv_format = YUVBuffer.format({
      // Encoded size
      width: probe.video.coded_width,
      height: probe.video.coded_height,
      // 4:2:0, so halve the chroma dimensions.
      chromaWidth: probe.video.coded_width/2,
      chromaHeight: probe.video.coded_height/2,

      // Full frame is visible. ?
      cropLeft: 0,
      cropTop: 0,
      cropWidth: probe.video.width,
      cropHeight: probe.video.height,

      // Final display size stretches back out to display width x height:
      displayWidth: probe.video.width,
      displayHeight: probe.video.height
    })
    var yuv_frame = YUVBuffer.frame(
      yuv_format,
      YUVBuffer.lumaPlane(yuv_format, frame.avY, frame.pitchY, 0),
      YUVBuffer.chromaPlane(yuv_format, frame.avU, frame.pitchU, 0),
      YUVBuffer.chromaPlane(yuv_format, frame.avV, frame.pitchV, 0)
    )

    //draw to yuv_frame canvas
    //sync with audio

    setTimeout(readVideo, 10);
  })
}  

function readAudio(){
  decoder.readAudio( frame => {
    pcm_audio.feed(frame.buffer);
    setTimeout(readAudio, 10);
  })
}
```
