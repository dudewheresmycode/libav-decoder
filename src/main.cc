#ifndef _WIN32
#include <unistd.h>
#define Sleep(x) usleep((x)*1000)
#endif
#include <time.h>

#include <nan.h>

using namespace v8;
using namespace node;
using namespace Nan;

extern "C" {
  #include <libavcodec/avcodec.h>
  #include <libavformat/avformat.h>
  #include <libavformat/avio.h>
  #include <libswscale/swscale.h>
  #include <libswresample/swresample.h>
  #include <libavutil/avstring.h>
  #include <libavutil/time.h>
  #include <libavutil/opt.h>
  #include <libavutil/imgutils.h>

}

#include "time_value.h"
#include "packet_queue.h"

#define MAX_VIDEOQ_SIZE (5 * 1024 * 1024)
#define MAX_AUDIOQ_SIZE (5 * 16 * 1024)
#define VIDEO_PICTURE_QUEUE_SIZE 1
#define MAX_AUDIO_FRAME_SIZE 192000

typedef struct YUVImage {
  int w;
  int h;
  int planes;

  uint32_t pitchY;
  uint32_t pitchU;
  uint32_t pitchV;

  uint8_t *avY;
  uint8_t *avU;
  uint8_t *avV;

  size_t size_y;
  size_t size_u;
  size_t size_v;

  double pts;

};
typedef struct RGBAImage {
  size_t size;
  uint8_t *pixels;
  int stride;
};

typedef struct RawAudio {
  uint8_t *data;
  size_t size;
  double pts;
};

AVFormatContext *pFormatCtx = NULL;
AVCodecContext  *pCodecCtxInput = NULL;
AVCodecContext  *pCodecCtx = NULL;
AVCodec         *pCodec = NULL;

AVCodecContext  *aCodecCtxInput = NULL;
AVCodecContext  *aCodecCtx = NULL;
AVCodec         *aCodec = NULL;

uint8_t			*out_buffer;

int videoStream, audioStream;

int frameIndex, frameDecoded, hasDecodedFrame, frameFinished, queueReady=0;
int isFinished = 0;
int audioIsFinished = 0;
int videoIsFinished = 0;
AVFrame         *pFrame = NULL;
AVFrame         *pFrameOut = NULL;
AVFrame         *pFrameCopy = NULL;
AVFrame         *aFrame = NULL;

// AVPixelFormat   pix_fmt = AV_PIX_FMT_YUV420P; //AV_PIX_FMT_YUV420P; //AV_PIX_FMT_RGB24;
AVPixelFormat   pix_fmt = AV_PIX_FMT_RGBA; //AV_PIX_FMT_YUV420P; //AV_PIX_FMT_RGB24;

int             shouldQuit;
uint8_t *buffer;
AVPacket        packet;
struct SwsContext   *sws_ctx            = NULL;
struct SwrContext *swr;
YUVImage *yuv;
RGBAImage *rgb;
RawAudio *wav;
int64_t in_channel_layout;

double vpts;
static AVPacket pkt;

PacketQueue     videoq;
PacketQueue     audioq;

char *timeStr(){
  time_t rawtime;
  struct tm * timeinfo;
  char buffer [80];

  time (&rawtime);
  timeinfo = localtime (&rawtime);
  // return timeinfo;
  strftime(buffer, 80, "%I:%M%p %r", timeinfo);
  return buffer;

}

void cleanup(){
  avformat_close_input(&pFormatCtx);
}
void extractRGBA(){
  rgb->stride = pFrameOut->linesize[0];
  rgb->size = pFrameOut->linesize[0] * pCodecCtxInput->coded_height;
  rgb->pixels = pFrameOut->data[0];
}
void extractYUV(){

  yuv->w = pFrameOut->width;
  yuv->h = pFrameOut->height;

  yuv->pitchY = pFrameOut->linesize[0];
  yuv->pitchU = pFrameOut->linesize[1];
  yuv->pitchV = pFrameOut->linesize[2];

  yuv->avY = pFrameOut->data[0];
  yuv->avU = pFrameOut->data[1];
  yuv->avV = pFrameOut->data[2];

  // yuv->size_y = (yuv->pitchY * pCodecCtxInput->coded_height);
  // yuv->size_u = (yuv->pitchU * pCodecCtxInput->coded_height / 2);
  // yuv->size_v = (yuv->pitchV * pCodecCtxInput->coded_height / 2);
  yuv->size_y = (yuv->pitchY * pCodecCtxInput->height);
  yuv->size_u = (yuv->pitchU * pCodecCtxInput->height / 2);
  yuv->size_v = (yuv->pitchV * pCodecCtxInput->height / 2);

}


class AudioDecodeReader : public AsyncWorker {
 public:
  AudioDecodeReader(Callback *callback)
    : AsyncWorker(callback) {}
  ~AudioDecodeReader() {}

  void Execute () {
    // AVPacket pkt1, *packet = &pkt1;
    // int audio_size;
    // fprintf(stderr, "audio: %d\n", audioq.nb_packets);
    // if(audioq.nb_packets > 0){
      AVPacket pkt1, *packet = &pkt1;
      // static uint8_t *audio_pkt_data = NULL;
      // static int audio_pkt_size = 0;
      wav = new RawAudio;
      wav->size = 0;

      aFrame = av_frame_alloc();

      // fprintf(stderr, "wav size: %d\n", sizeof(wav->data));
      // audio_size = audio_decode_frame(aCodecCtx, wav->data, sizeof(wav->data));
      // wav->size = audio_size;


      swr = swr_alloc();
      av_opt_set_int(swr, "in_channel_count",  aCodecCtx->channels, 0);
      av_opt_set_int(swr, "out_channel_count", aCodecCtx->channels, 0);
      av_opt_set_int(swr, "in_channel_layout",  aCodecCtx->channel_layout, 0);
      av_opt_set_int(swr, "out_channel_layout", aCodecCtx->channel_layout, 0);
      av_opt_set_int(swr, "in_sample_rate", aCodecCtx->sample_rate, 0);
      av_opt_set_int(swr, "out_sample_rate", aCodecCtx->sample_rate, 0);
      av_opt_set_sample_fmt(swr, "in_sample_fmt",  aCodecCtx->sample_fmt, 0);
      av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_S16,  0);
      swr_init(swr);
      if (!swr_is_initialized(swr)) {
          fprintf(stderr, "Resampler has not been properly initialized\n");
          return;
      }

      // double** data = NULL;
      int ret;
      int gotFrame;
      for(;;){
          // decode one frame
          if(shouldQuit==1){
            SetErrorMessage("Quitting...");
            fprintf(stderr, "quit ivoked!\n");
            break;
          }

          int pret = packet_queue_get(&audioq, packet, 1);
          if(pret < 0) {
            if(isFinished > 0){
              // means we quit getting packets
              audioIsFinished = 1;
              fprintf(stderr, "no more audio packets!\n");
              break;
            }else{
              // fprintf(stderr, "not finished decoding.. wait and try again\n");
              Sleep(25);
              continue;
            }
          }

          // if(packet_queue_get(&audioq, packet, 1) < 0 && isFinished==1) {
          //   // means we quit getting packets
          //   audioIsFinished = 1;
          //   fprintf(stderr, "quit getting audio packets!\n");
          //   break;
          // }

          if (avcodec_decode_audio4(aCodecCtx, aFrame, &gotFrame, packet) < 0) {
            fprintf(stderr, "Error decoding...\n");
            break;
          }

          if (gotFrame) {
            // fprintf(stderr, "Got a frame\n");
            wav->size = av_samples_get_buffer_size(NULL, aCodecCtx->channels, aFrame->nb_samples, AV_SAMPLE_FMT_S16, 1);
            wav->data = (uint8_t *)malloc(wav->size);
            // av_samples_alloc(&wav->data, NULL, 1, aFrame->nb_samples, AV_SAMPLE_FMT_S16, 0);
            int frame_count = swr_convert(swr, &wav->data, aFrame->nb_samples, (const uint8_t **)aFrame->data, aFrame->nb_samples);

            double pts = 0;
            if((pts = av_frame_get_best_effort_timestamp(aFrame)) != AV_NOPTS_VALUE) {
              pts *= av_q2d(pFormatCtx->streams[audioStream]->time_base);
            }
            wav->pts = pts;

            break;
          }
      }

    // }else{
    //   audioIsFinished = 1;
    //   fprintf(stderr, "No more audio packs\n");
    // }
  }
  void HandleOKCallback(){
    Nan::HandleScope scope;
    Local<Object> obj = Nan::New<Object>();
    if(audioIsFinished > 0){
      obj->Set(Nan::New<String>("type").ToLocalChecked(), Nan::New<String>("finished").ToLocalChecked());
    }else{
      if(wav->size > 0){
        obj->Set(Nan::New<String>("pts").ToLocalChecked(), Nan::New<Number>(wav->pts));
        obj->Set(Nan::New<String>("buffer").ToLocalChecked(), Nan::CopyBuffer((char *)wav->data, wav->size).ToLocalChecked());
        obj->Set(Nan::New<String>("type").ToLocalChecked(), Nan::New<String>("samples").ToLocalChecked());
      }else{
        obj->Set(Nan::New<String>("type").ToLocalChecked(), Nan::New<String>("skip").ToLocalChecked());
      }
    }
    v8::Local<v8::Value> argv[] = {obj};
    callback->Call(1, argv);
  }
  void HandleErrorCallback() {
    v8::Local<v8::Value> argv[] = {Nan::New<String>(ErrorMessage()).ToLocalChecked()};
    callback->Call(1, argv);
  }

  void Destroy(){
    // fprintf(stderr, "destroy audio reader\n");
    // free(wav->data);
    // av_free(aFrame);
    // swr_free(&swr);
    // avcodec_close(aCodecCtx);

  }


};
class DecodeReader : public AsyncWorker {
 public:
  DecodeReader(Callback *callback)
    : AsyncWorker(callback) {}
  ~DecodeReader() {}

  void Execute () {
    hasDecodedFrame=false;
    // fprintf(stderr, "video: %d\n", videoq.nb_packets);
    // if(videoq.nb_packets > 0){

      AVPacket pkt1, *packet = &pkt1;
      int frameFinished;

      pFrame = av_frame_alloc();
      // if ((av_image_alloc(pFrameOut->data, pFrameOut->linesize, pCodecCtxInput->coded_width, pCodecCtxInput->coded_height, pix_fmt, 1)) < 0) {
      //     fprintf(stderr, "Could not allocate destination image\n");
      //     return;
      // }

      yuv = new YUVImage;
      rgb = new RGBAImage;

      int i=0;
      for(;;) {
        if(shouldQuit==1){
          SetErrorMessage("Quitting...");
          fprintf(stderr, "quit ivoked!\n");
          break;
        }
        int pret = packet_queue_get(&videoq, packet, 1);
        if(pret < 0) {
          if(isFinished > 0){
            // means we quit getting packets
            videoIsFinished = 1;
            fprintf(stderr, "no more video packets!\n");
            break;
          }else{
            fprintf(stderr, "not finished decoding.. wait and try again\n");
            Sleep(25);
            continue;
          }
        }
        // fprintf(stderr, "decode...%d\n", packet);


        avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, packet);

        double pts=0;
        if(packet->dts != AV_NOPTS_VALUE){
          pts = av_frame_get_best_effort_timestamp(pFrame);
        }else{
          pts = 0;
        }
        pts *= av_q2d(pFormatCtx->streams[videoStream]->time_base);
        yuv->pts = pts;
        // fprintf(stderr, "pts: %f\n", pts);
        if(frameFinished) {
          sws_scale(sws_ctx, (const uint8_t * const*)pFrame->data, pFrame->linesize, 0, pCodecCtxInput->height, pFrameOut->data, pFrameOut->linesize);
          frameDecoded++;
          hasDecodedFrame=true;
          // extractYUV();
          extractRGBA();
          break;
        }
        //no frame found.. free this packet and loop again ...
        av_free_packet(packet);
        i++;
      }
      av_frame_free(&pFrame);

    // }else{
      // fprintf(stderr, "no more packets...\n");
      // videoIsFinished = 1;
      // avformat_close_input(&pFormatCtx);
    // }


  }

  void HandleOKCallback(){
    fprintf(stderr, "finished video read loop\n");
    Nan::HandleScope scope;
    Local<Object> obj = Nan::New<Object>();
    if(videoIsFinished > 0){
      obj->Set(Nan::New<String>("type").ToLocalChecked(), Nan::New<String>("finished").ToLocalChecked());
    }else if(hasDecodedFrame){
      obj->Set(Nan::New<String>("type").ToLocalChecked(), Nan::New<String>("frame").ToLocalChecked());


      obj->Set(Nan::New<String>("stride").ToLocalChecked(), Nan::New<Number>(rgb->stride).ToLocalChecked());
      obj->Set(Nan::New<String>("pixels").ToLocalChecked(), Nan::CopyBuffer((char *)rgb->pixels, rgb->size).ToLocalChecked());

      // obj->Set(Nan::New<String>("avY").ToLocalChecked(), Nan::CopyBuffer((char *)yuv->avY, yuv->size_y).ToLocalChecked());
      // obj->Set(Nan::New<String>("avU").ToLocalChecked(), Nan::CopyBuffer((char *)yuv->avU, yuv->size_u).ToLocalChecked());
      // obj->Set(Nan::New<String>("avV").ToLocalChecked(), Nan::CopyBuffer((char *)yuv->avV, yuv->size_v).ToLocalChecked());
      //
      // obj->Set(Nan::New<String>("pitchY").ToLocalChecked(), Nan::New<Integer>(yuv->pitchY));
      // obj->Set(Nan::New<String>("pitchU").ToLocalChecked(), Nan::New<Integer>(yuv->pitchU));
      // obj->Set(Nan::New<String>("pitchV").ToLocalChecked(), Nan::New<Integer>(yuv->pitchV));

      obj->Set(Nan::New<String>("width").ToLocalChecked(), Nan::New<Number>(yuv->w));
      obj->Set(Nan::New<String>("height").ToLocalChecked(), Nan::New<Number>(yuv->h));

      obj->Set(Nan::New<String>("frame").ToLocalChecked(), Nan::New<Number>(frameDecoded));
      obj->Set(Nan::New<String>("pts").ToLocalChecked(), Nan::New<Number>(yuv->pts));
      // obj->Set(Nan::New<String>("hasFrame").ToLocalChecked(), Nan::New<Number>(1));
      if(yuv){
        delete yuv;
      }
    }else{
      obj->Set(Nan::New<String>("type").ToLocalChecked(), Nan::New<String>("empty").ToLocalChecked());
      // obj->Set(Nan::New<String>("hasFrame").ToLocalChecked(), Nan::New<Number>(0));
    }

    v8::Local<v8::Value> argv[] = {obj};
    callback->Call(1, argv);
  }
  void HandleErrorCallback() {
    v8::Local<v8::Value> argv[] = {Nan::New<String>(ErrorMessage()).ToLocalChecked()};
    callback->Call(1, argv);
  }

  void Destroy(){
    // fprintf(stderr, "destroy reader\n");
  }


};

class DecodeWorker : public AsyncProgressWorker {
 public:
  DecodeWorker(
      Callback *callback
    , Callback *progress)
    : AsyncProgressWorker(callback), progress(progress) {}
  ~DecodeWorker() { delete progress; }
//
// class DecodeVideoWorker : public AsyncWorker {
//  public:
//   DecodeVideoWorker(Callback *callback)
//     : AsyncWorker(callback) {}
//   ~DecodeVideoWorker() {}

  void Execute (const AsyncProgressWorker::ExecutionProgress& progress) {
    //Find the decoder for the video stream
    hasDecodedFrame=false;
    pCodec=avcodec_find_decoder(pCodecCtxInput->codec_id);
    if(pCodec==NULL) {
      fprintf(stderr, "Unsupported codec!\n");
      return SetErrorMessage("Unable to find a suitable decoder");
    }
    // Copy context
    pCodecCtx = avcodec_alloc_context3(pCodec);
    if(avcodec_copy_context(pCodecCtx, pCodecCtxInput) != 0) {
      return SetErrorMessage("Could not copy video codec context");
    }

    // Open video codec
    if(avcodec_open2(pCodecCtx, pCodec, NULL)<0){
      return SetErrorMessage("Could not open video codec");
    }

    //copy audio context
    aCodec=avcodec_find_decoder(aCodecCtxInput->codec_id);

    // aCodec = avcodec_find_decoder(aCodecCtxInput->codec_id);
    if(!aCodec) {
      return SetErrorMessage("Unsupported audio codec");
    }
    // Copy context
    // aCodecCtx = avcodec_alloc_context3(aCodec);
    aCodecCtx = avcodec_alloc_context3(aCodec);
    if(avcodec_copy_context(aCodecCtx, aCodecCtxInput) != 0) {
      return SetErrorMessage("Couldn't copy audio codec context");
    }
    if(avcodec_open2(aCodecCtx, aCodec, NULL) < 0){
      return SetErrorMessage("Could not open audio codec");
    }
    // aCodecCtx->request_sample_fmt = AV_SAMPLE_FMT_S16;

    // pFrameCopy = av_frame_alloc();
    pFrameOut = av_frame_alloc();
    pFrame = av_frame_alloc();
    yuv = new YUVImage;
    rgb = new RGBAImage;
    wav = new RawAudio;
    wav->size = 0;
    if (!pFrame) {
        // fprintf(stderr, "Could not allocate frame\n");
        // ret = AVERROR(ENOMEM);
        return SetErrorMessage("Could not allocate frame");
        // goto end;
    }


    AVPacket pkt1, *packet = &pkt1;
    // AVPacket packet;
    // av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    // int size = avpicture_get_size(pix_fmt, pCodecCtxInput->coded_width, pCodecCtxInput->coded_height);
    // uint8_t* buffer = (uint8_t*)av_malloc(size);
    //
    int sizeout = avpicture_get_size(pix_fmt, pCodecCtxInput->coded_width, pCodecCtxInput->coded_height);
    uint8_t* bufferout = (uint8_t*)av_malloc(sizeout);

    // avpicture_fill((AVPicture *)pFrameCopy, buffer, pix_fmt, pCodecCtxInput->coded_width, pCodecCtxInput->coded_height);
    avpicture_fill((AVPicture *)pFrameOut, bufferout, pix_fmt, pCodecCtxInput->coded_width, pCodecCtxInput->coded_height);

    sws_ctx = sws_getContext(
         pCodecCtxInput->width,
         pCodecCtxInput->height,
         pCodecCtx->pix_fmt,
         pCodecCtxInput->width,
         pCodecCtxInput->height,
         pix_fmt,
         SWS_BILINEAR,
         NULL,
         NULL,
         NULL
    );

    int readFrame;

    for(;;) {
      if(shouldQuit == 1){
        fprintf(stderr, "quit reading\n");
        break;
      }


      if((videoIsFinished == 0 && videoq.size > MAX_VIDEOQ_SIZE) || (audioIsFinished == 0 && audioq.size > MAX_AUDIOQ_SIZE)){
        Sleep(25); //sleep for a second?
        continue;
      }

      if(queueReady==0){
        queueReady=1;
        fprintf(stderr, "c+: ready...\n");
        progress.Signal();
      }

      if(readFrame = av_read_frame(pFormatCtx, packet) < 0) {
        if(pFormatCtx->pb->error == 0) {
          // fprintf(stderr, "finished?\n");
          isFinished = 1;
           break;
        } else {
           break;
        }
      }

      if(packet->stream_index == videoStream) {
        frameIndex++;
        packet_queue_put(&videoq, packet);
        // fprintf(stderr, "%s: [worker] Finished Decoding\n", timeStr());
        // fprintf(stderr, "videoq.nb_packets: %d\n", videoq.nb_packets);
      } else if(packet->stream_index == audioStream) {
        packet_queue_put(&audioq, packet);
        // fprintf(stderr, "audioq.nb_packets: %d\n", audioq.nb_packets);
      } else {
        fprintf(stderr, "free: %d\n");
        av_free_packet(packet);
      }

    }

    // int ret = av_read_frame(pFormatCtx, &pkt);
    // fprintf(stderr, "read frame: %s\n", ret);
    // while(av_read_frame(pFormatCtx, &pkt) >= 0){
    //   fprintf(stderr, "readed frame\n");
    //   avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &pkt);
    //   if(frameFinished) {
    //     sws_scale(sws_ctx, (uint8_t const * const *)pFrame->data, pFrame->linesize, 0, pCodecCtxInput->coded_height, pFrameOut->data, pFrameOut->linesize);
    //     frameDecoded++;
    //     hasDecodedFrame=true;
    //     extractYUV();
    //     break;
    //   }
    //   // break;
    // }
    //
    // av_free_packet(&pkt);
    // av_frame_free(&pFrame);

    // if(readFrame = av_read_frame(pFormatCtx, packet) < 0) {
    //   fprintf(stderr, "read frame!\n");
    // }else{
    //   fprintf(stderr, "need more\n");
    // }
    // for(;;) {
    //   if(readFrame = av_read_frame(pFormatCtx, packet) < 0) {
    //     // if(pFormatCtx->pb->error == 0) {
    //     fprintf(stderr, "got a frame\n");
    //     break;
    //     // }
    //   }else{
    //     fprintf(stderr, "need more\n");
    //     break;
    //   }
    // }




  }

  void HandleOKCallback(){
    // av_free_packet(&pkt);
    Nan::HandleScope scope;
    fprintf(stderr, "%s: [worker] Finished Decoding\n", timeStr());
    // Local<Object> obj = Nan::New<Object>();
    // if(hasDecodedFrame){
    //   obj->Set(Nan::New<String>("avY").ToLocalChecked(), Nan::CopyBuffer((char *)yuv->avY, yuv->size_y).ToLocalChecked());
    //   obj->Set(Nan::New<String>("avU").ToLocalChecked(), Nan::CopyBuffer((char *)yuv->avU, yuv->size_u).ToLocalChecked());
    //   obj->Set(Nan::New<String>("avV").ToLocalChecked(), Nan::CopyBuffer((char *)yuv->avV, yuv->size_v).ToLocalChecked());
    //
    //   obj->Set(Nan::New<String>("pitchY").ToLocalChecked(), Nan::New<Integer>(yuv->pitchY));
    //   obj->Set(Nan::New<String>("pitchU").ToLocalChecked(), Nan::New<Integer>(yuv->pitchU));
    //   obj->Set(Nan::New<String>("pitchV").ToLocalChecked(), Nan::New<Integer>(yuv->pitchV));
    //
    //   obj->Set(Nan::New<String>("frame").ToLocalChecked(), Nan::New<Number>(frameDecoded));
    //   obj->Set(Nan::New<String>("pts").ToLocalChecked(), Nan::New<Number>(vpts));
    //   obj->Set(Nan::New<String>("hasFrame").ToLocalChecked(), Nan::New<Number>(1));
    //   if(yuv){
    //     delete yuv;
    //   }
    // }else{
    //   obj->Set(Nan::New<String>("hasFrame").ToLocalChecked(), Nan::New<Number>(0));
    //
    // }
    v8::Local<v8::Value> argv[] = {Nan::Null()};
    callback->Call(1, argv);

    // Local<Object> obj = Nan::New<Object>();
    //   obj->Set(Nan::New<String>("hello").ToLocalChecked(), Nan::New<String>("from worker").ToLocalChecked());
    //
    // // if(hasDecodedFrame){
    // //   obj->Set(Nan::New<String>("avY").ToLocalChecked(), Nan::CopyBuffer((char *)yuv->avY, yuv->size_y).ToLocalChecked());
    // //   obj->Set(Nan::New<String>("avU").ToLocalChecked(), Nan::CopyBuffer((char *)yuv->avU, yuv->size_u).ToLocalChecked());
    // //   obj->Set(Nan::New<String>("avV").ToLocalChecked(), Nan::CopyBuffer((char *)yuv->avV, yuv->size_v).ToLocalChecked());
    // //
    // //   obj->Set(Nan::New<String>("pitchY").ToLocalChecked(), Nan::New<Integer>(yuv->pitchY));
    // //   obj->Set(Nan::New<String>("pitchU").ToLocalChecked(), Nan::New<Integer>(yuv->pitchU));
    // //   obj->Set(Nan::New<String>("pitchV").ToLocalChecked(), Nan::New<Integer>(yuv->pitchV));
    // //
    // //   obj->Set(Nan::New<String>("frame").ToLocalChecked(), Nan::New<Number>(frameDecoded));
    // //   obj->Set(Nan::New<String>("pts").ToLocalChecked(), Nan::New<Number>(vpts));
    // //   obj->Set(Nan::New<String>("hasFrame").ToLocalChecked(), Nan::New<Number>(1));
    // //   if(yuv){
    // //     delete yuv;
    // //   }
    // // }else{
    // //   obj->Set(Nan::New<String>("hasFrame").ToLocalChecked(), Nan::New<Number>(0));
    // //
    // // }
    //
    // v8::Local<v8::Value> argv[] = {Nan::Null(), obj};
    // callback->Call(2, argv);
  }
  void HandleErrorCallback() {
    v8::Local<v8::Value> argv[] = {Nan::New<String>(ErrorMessage()).ToLocalChecked()};
    callback->Call(1, argv);
  }
  void Destroy(){
    // fprintf(stderr, "destroy reader\n");
  }

  void HandleProgressCallback(const char *data, size_t count) {
    Nan::HandleScope scope;
    // fprintf(stderr, "ProgressCallback\n");
    Local<Value> argv[] = {
      Nan::New<String>("dataAvailable").ToLocalChecked()
    };
    progress->Call(1, argv);
  }

 private:
  Callback *progress;
};


NAN_METHOD(OpenVideo) {
  Nan::HandleScope scope;
  String::Utf8Value cmd(info[0]);
  char *in = (*cmd);

  Callback *callback = new Callback(info[1].As<Function>());
  fprintf(stderr, "Open: %s\n", in);

  // pFormatCtx = NULL;

  if(avformat_open_input(&pFormatCtx, in, NULL, NULL)!=0){
    fprintf(stderr, "[c] Could not open %s\n", in);
    Local<Value> argv[] = {Nan::New<String>("Could not open file").ToLocalChecked()};
    callback->Call(1, argv);
    exit(-1);
  }
  fprintf(stderr, "open success\n");

  // Retrieve stream information
  if(avformat_find_stream_info(pFormatCtx, NULL)<0){
    fprintf(stderr, "avformat_find_stream_info failed\n");
    cleanup();
    exit(-1);
  }

  //dump input stream infos
  av_dump_format(pFormatCtx, 0, in, 0);

  char val_str[128];
  int i;

  videoStream=-1;
  audioStream=-1;
  for(i=0; i<pFormatCtx->nb_streams; i++) {
    if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO && videoStream < 0) {
      videoStream=i;
    }
    if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO && audioStream < 0) {
      audioStream=i;
    }
  }
  if(videoStream==-1){
    fprintf(stderr, "find videoStream failed\n");
  }
  if(audioStream==-1){
    fprintf(stderr, "find audioStream failed\n");
  }

  pCodecCtxInput = pFormatCtx->streams[videoStream]->codec;
  aCodecCtxInput = pFormatCtx->streams[audioStream]->codec;

  packet_queue_init(&videoq);
  packet_queue_init(&audioq);


  Local<Object> obj = Nan::New<Object>();

  obj->Set(Nan::New<String>("filename").ToLocalChecked(), Nan::New<String>(in).ToLocalChecked());
  obj->Set(Nan::New<String>("duration").ToLocalChecked(), Nan::New<Number>(atof(time_value_string(val_str, sizeof(val_str), pFormatCtx->duration))));

  Local<Object> video = Nan::New<Object>();
  // obj->Set(Nan::New<String>("frames").ToLocalChecked(), Nan::New<Number>(pFormatCtx->streams[videoStream]->nb_frames));
  video->Set(Nan::New<String>("width").ToLocalChecked(), Nan::New<Integer>(pCodecCtxInput->width));
  video->Set(Nan::New<String>("height").ToLocalChecked(), Nan::New<Integer>(pCodecCtxInput->height));
  video->Set(Nan::New<String>("coded_width").ToLocalChecked(), Nan::New<Integer>(pCodecCtxInput->coded_width));
  video->Set(Nan::New<String>("coded_height").ToLocalChecked(), Nan::New<Integer>(pCodecCtxInput->coded_height));

  Local<Object> framerate = Nan::New<Object>();
  framerate->Set(Nan::New<String>("num").ToLocalChecked(), Nan::New<Number>(pFormatCtx->streams[videoStream]->avg_frame_rate.num));
  framerate->Set(Nan::New<String>("den").ToLocalChecked(), Nan::New<Number>(pFormatCtx->streams[videoStream]->avg_frame_rate.den));
  video->Set(Nan::New<String>("framerate").ToLocalChecked(), framerate);

  obj->Set(Nan::New<String>("video").ToLocalChecked(), video);

  // obj->Set(Nan::New<String>("coded_height").ToLocalChecked(), Nan::New<Integer>(pCodecCtxInput->coded_height));

  Local<Object> audio = Nan::New<Object>();
  audio->Set(Nan::New<String>("sample_rate").ToLocalChecked(), Nan::New<Integer>(aCodecCtxInput->sample_rate));
  audio->Set(Nan::New<String>("channels").ToLocalChecked(), Nan::New<Integer>(aCodecCtxInput->channels));
  obj->Set(Nan::New<String>("audio").ToLocalChecked(), audio);

  // obj->Set(Nan::New<String>("aspect_ratio").ToLocalChecked(), Nan::New<Number>(aspect_ratio));
  // float fr = pFormatCtx->streams[videoStream]->r_frame_rate.num/pFormatCtx->streams[videoStream]->r_frame_rate.den;
  // fprintf(stderr, "frame rate: %f / %f = %f\n", pFormatCtx->streams[videoStream]->r_frame_rate.num, pFormatCtx->streams[videoStream]->r_frame_rate.den, fr);
  // obj->Set(Nan::New<String>("frame_rate").ToLocalChecked(), Nan::New<Number>((float)fr));
  //
  // obj->Set(Nan::New<String>("frame_rate_n").ToLocalChecked(), Nan::New<Number>(pFormatCtx->streams[videoStream]->r_frame_rate.num));
  // obj->Set(Nan::New<String>("frame_rate_d").ToLocalChecked(), Nan::New<Number>(pFormatCtx->streams[videoStream]->r_frame_rate.den));
  //
  // obj->Set(Nan::New<String>("time_base_n").ToLocalChecked(), Nan::New<Number>(pFormatCtx->streams[videoStream]->time_base.num));
  // obj->Set(Nan::New<String>("time_base_d").ToLocalChecked(), Nan::New<Number>(pFormatCtx->streams[videoStream]->time_base.den));

  Local<Value> argv[] = {Nan::Null(), obj};
  callback->Call(2, argv);
  // cleanup();
}

NAN_METHOD(DecodeVideo) {
  Nan::HandleScope scope;
  // Callback *callback = new Callback(info[0].As<Function>());

  // Callback *callback = new Callback(info[0].As<v8::Function>());
  // AsyncQueueWorker(new DecodeVideoWorker(callback));

  // Callback *progress = new Callback(info[0].As<Function>());
  Callback *progress = new Callback(info[0].As<Function>());
  Callback *callback = new Callback(info[1].As<Function>());

  AsyncQueueWorker(new DecodeWorker(callback, progress));

  // Local<Value> argv[] = {Nan::Null(), Nan::Null()};
  // callback->Call(2, argv);
}
NAN_METHOD(ReadVideo) {
  Callback *callback = new Callback(info[0].As<v8::Function>());
  AsyncQueueWorker(new DecodeReader(callback));
}

NAN_METHOD(ReadAudio) {
  Callback *callback = new Callback(info[0].As<v8::Function>());
  AsyncQueueWorker(new AudioDecodeReader(callback));
}

NAN_METHOD(TearDown) {
  fprintf(stderr, "TEAR DOWN\n");
  shouldQuit = 1;
  cleanup();
}

NAN_MODULE_INIT(InitModule) {

  av_register_all();

  Nan::Set(target, New<String>("open").ToLocalChecked(), GetFunction(New<FunctionTemplate>(OpenVideo)).ToLocalChecked());
  Nan::Set(target, New<String>("decode").ToLocalChecked(), GetFunction(New<FunctionTemplate>(DecodeVideo)).ToLocalChecked());
  Nan::Set(target, New<String>("decode").ToLocalChecked(), GetFunction(New<FunctionTemplate>(DecodeVideo)).ToLocalChecked());
  Nan::Set(target, New<String>("readVideo").ToLocalChecked(), GetFunction(New<FunctionTemplate>(ReadVideo)).ToLocalChecked());
  Nan::Set(target, New<String>("readAudio").ToLocalChecked(), GetFunction(New<FunctionTemplate>(ReadAudio)).ToLocalChecked());
  Nan::Set(target, New<String>("destroy").ToLocalChecked(), GetFunction(New<FunctionTemplate>(TearDown)).ToLocalChecked());

}

NODE_MODULE(addon, InitModule);
