/***************************************************
This is the main Grabber code.  It uses ffmpeg
to capture video and audio from video and audio files.

The code supports any number of audio or video streams and
is a cross platform solution to replace DDGrab.cpp.

This code was intended to be used inside of a matlab interface,
but can be used as a generic grabber class for anyone who needs
one.

Copyright 2018 Micah Richert

This file is part of mmread.
**************************************************/

#ifdef MATLAB_MEX_FILE
#include "mex.h"
#define FFprintf(...) mexPrintf(__VA_ARGS__)
#else
#define FFprintf(...) printf(__VA_ARGS__)
#endif

//#ifndef mwSize
//#define mwSize int
//#endif

#define DEBUG 0

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
//#include <libavutil/avutil.h>
//#include <libavutil/dict.h>
//#include <libavutil/mathematics.h>
#include <libswscale/swscale.h>

enum AVStreamType {
  AV_STREAM_TYPE_UNKNOWN = 0,
  AV_STREAM_TYPE_VIDEO = 1,
  AV_STREAM_TYPE_AUDIO = 2
};

struct AVPacket2 {
  size_t structure_size;
  int64_t timestamp;
  uint32_t stream_index;

  uint8_t *data;
  size_t size;
};

struct AVFile {
  AVFormatContext *context;
  AVPacket *packet;
};

struct AVStream2 {
  int32_t type;
  AVFormatContext *format_context;
  AVCodecContext *codec_context;
  AVFrame *frame;
};

struct AVFileInfo {
  size_t structure_size;
  int32_t n_streams;
  int64_t start_time;
  int64_t duration;

  /**
   * @name Metadata fields
   *
   * File metadata.
   *
   * Strings are NUL-terminated and may be omitted (the first character
   * NUL) if the file does not contain appropriate information.  The
   * encoding of the strings is unspecified.
   */
  /*@{*/
  char title[512];
  char author[512];
  char copyright[512];
  char comment[512];
  char album[512];
  int32_t year;
  int32_t track;
  char genre[32];
  /*@}*/
};

struct AVStreamInfo {
  size_t structure_size;
  AVStreamType type;

  union {
    struct {
      uint32_t width;
      uint32_t height;
      uint32_t sample_aspect_num;
      uint32_t sample_aspect_den;

      uint32_t frame_rate_num;
      uint32_t frame_rate_den;
    } video;

    struct {
      AVSampleFormat sample_format;
      uint32_t sample_rate;
      uint32_t sample_bits;
      uint32_t channels;
    } audio;
  };
};
}

#include <map>
#include <stdlib.h>
#include <string.h>
#include <vector>
using namespace std;

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

AVStream2 *av_open_stream(AVFile *file, int32_t index) {
  AVCodecContext *codec_context;
  AVCodec *codec;

  if (index < 0 || index >= file->context->nb_streams)
    return NULL;

  codec_context = file->context->streams[index]->codec;
  codec = avcodec_find_decoder(codec_context->codec_id);
  if (!codec)
    return NULL;

  codec_context->thread_count = 1;

  if (avcodec_open2(codec_context, codec, NULL) < 0)
    return NULL;

  AVStream2 *stream = (AVStream2 *)malloc(sizeof(AVStream2));
  stream->format_context = file->context;
  stream->codec_context = codec_context;
  stream->type = codec_context->codec_type;
  stream->frame = avcodec_alloc_frame();

  return stream;
}

void av_close_stream(AVStream2 *stream) {
  if (stream->frame)
    avcodec_free_frame(&stream->frame);
  avcodec_close(stream->codec_context);
  free(stream);
}

AVFile *av_open_filename_with_format(const char *filename, char *format) {
  AVFile *file = (AVFile *)malloc(sizeof(AVFile));
  AVInputFormat *avformat = NULL;
  if (format)
    avformat = av_find_input_format(format);

  file->context = NULL; // Zero-initialize
  if (avformat_open_input(&file->context, filename, avformat, NULL) != 0)
    goto error;

  if (avformat_find_stream_info(file->context, NULL) < 0)
    goto error;

  file->packet = NULL;
  return file;

error:
  free(file);
  return NULL;
}

void av_close_file(AVFile *file) {
  if (file->packet) {
    av_free_packet(file->packet);
    free(file->packet);
  }

  avformat_close_input(&file->context);
  free(file);
}

int av_file_info(AVFile *file, AVFileInfo *info) {
  if (info->structure_size < sizeof *info)
    return -1;

  info->n_streams = file->context->nb_streams;
  info->start_time = file->context->start_time;
  info->duration = file->context->duration;

  // Zero-initialize fields first
  memset(info->title, 0, sizeof(info->title));
  memset(info->author, 0, sizeof(info->author));
  memset(info->copyright, 0, sizeof(info->copyright));
  memset(info->comment, 0, sizeof(info->comment));
  memset(info->album, 0, sizeof(info->album));
  memset(info->genre, 0, sizeof(info->genre));
  info->year = 0;
  info->track = 0;

  AVDictionaryEntry *entry;
  if ((entry = av_dict_get(file->context->metadata, "title", NULL, 0)) !=
      NULL) {
    strncpy(info->title, entry->value, sizeof(info->title));
  }

  if (((entry = av_dict_get(file->context->metadata, "artist", NULL, 0)) !=
       NULL) ||
      (entry = av_dict_get(file->context->metadata, "album_artist", NULL, 0)) !=
          NULL) {
    strncpy(info->author, entry->value, sizeof(info->author));
  }
  if ((entry = av_dict_get(file->context->metadata, "copyright", NULL, 0)) !=
      NULL) {
    strncpy(info->copyright, entry->value, sizeof(info->copyright));
  }
  if ((entry = av_dict_get(file->context->metadata, "comment", NULL, 0)) !=
      NULL) {
    strncpy(info->comment, entry->value, sizeof(info->comment));
  }
  if ((entry = av_dict_get(file->context->metadata, "album", NULL, 0)) !=
      NULL) {
    strncpy(info->album, entry->value, sizeof(info->album));
  }
  if ((entry = av_dict_get(file->context->metadata, "date", NULL, 0)) != NULL) {
    info->year = atoi(entry->value);
  }
  if ((entry = av_dict_get(file->context->metadata, "track", NULL, 0)) !=
      NULL) {
    info->track = atoi(entry->value);
  }
  if ((entry = av_dict_get(file->context->metadata, "genre", NULL, 0)) !=
      NULL) {
    strncpy(info->genre, entry->value, sizeof(info->genre));
  }

  return 0;
}

int av_stream_info(AVFile *file, int32_t stream_index, AVStreamInfo *info) {
  AVCodecContext *context = file->context->streams[stream_index]->codec;

  /* Error if not large enough for version 1 */
  if (info->structure_size < sizeof *info)
    return -1;

  switch (context->codec_type) {
  case AVMEDIA_TYPE_VIDEO:
    info->type = AV_STREAM_TYPE_VIDEO;
    info->video.width = context->width;
    info->video.height = context->height;
    info->video.sample_aspect_num = context->sample_aspect_ratio.num;
    info->video.sample_aspect_den = context->sample_aspect_ratio.den;

    break;
  case AVMEDIA_TYPE_AUDIO:
    info->type = AV_STREAM_TYPE_AUDIO;
    info->audio.sample_rate = context->sample_rate;
    info->audio.channels = context->channels;
    switch (context->sample_fmt) {
    case AV_SAMPLE_FMT_U8:
      info->audio.sample_format = AV_SAMPLE_FMT_U8;
      info->audio.sample_bits = 8;
      break;
    case AV_SAMPLE_FMT_S16:
      info->audio.sample_format = AV_SAMPLE_FMT_S16;
      info->audio.sample_bits = 16;
      break;
    case AV_SAMPLE_FMT_S32:
      info->audio.sample_format = AV_SAMPLE_FMT_S32;
      info->audio.sample_bits = 32;
      break;
    case AV_SAMPLE_FMT_FLT:
      info->audio.sample_format = AV_SAMPLE_FMT_FLT;
      info->audio.sample_bits = 32;
      break;
    case AV_SAMPLE_FMT_DBL:
      info->audio.sample_format = AV_SAMPLE_FMT_DBL;
      info->audio.sample_bits = 64;
      break;
    default:
      // Unknown sample format
      info->audio.sample_format = AV_SAMPLE_FMT_NONE;
      info->audio.sample_bits = -1;
      break;

      // TODO: support planar formats
    }
    break;

  default:
    info->type = AV_STREAM_TYPE_UNKNOWN;
    break;
  }

  return 0;
}

int32_t av_read(AVFile *file, AVPacket2 *packet) {
  if (packet->structure_size < sizeof *packet)
    return -1;

  if (file->packet)
    av_free_packet(file->packet);
  else
    file->packet = (AVPacket *)malloc(sizeof(AVPacket));

  if (av_read_frame(file->context, file->packet) < 0)
    return -1;

  packet->timestamp = av_rescale_q(
      file->packet->dts,
      file->context->streams[file->packet->stream_index]->time_base,
      AV_TIME_BASE_Q);
  packet->stream_index = file->packet->stream_index;
  packet->data = file->packet->data;
  packet->size = file->packet->size;

  return 0;
}

int32_t av_decode_audio(AVStream2 *stream, uint8_t *data_in, size_t size_in,
                        uint8_t *data_out, int *size_out) {
  int bytes_used;
  if (stream->type != AVMEDIA_TYPE_AUDIO)
    return -1;

  // Some decoders read big chunks at a time, so you have to make a bigger
  // buffer
  uint8_t inbuf[size_in + FF_INPUT_BUFFER_PADDING_SIZE];
  // Set the padding portion of the buffer to all zeros
  memset(inbuf + size_in, 0, FF_INPUT_BUFFER_PADDING_SIZE);
  // Copy the data into the padded buffer
  memcpy(inbuf, data_in, size_in);

  AVPacket packet;
  av_init_packet(&packet);
  packet.data = inbuf;
  packet.size = size_in;

  int got_frame = 0;
  bytes_used = avcodec_decode_audio4(stream->codec_context, stream->frame,
                                     &got_frame, &packet);

  if (bytes_used < 0)
    return -1;

  // TODO: support planar formats
  if (got_frame) {
    int plane_size;
    int data_size = av_samples_get_buffer_size(
        &plane_size, stream->codec_context->channels, stream->frame->nb_samples,
        stream->codec_context->sample_fmt, 1);
    if (*size_out < data_size) {
      av_log(stream->codec_context, AV_LOG_ERROR,
             "Output audio buffer is too small for current audio frame!");
      return -1;
    }

    memcpy(data_out, stream->frame->extended_data[0], data_size);
    *size_out = data_size;
  } else {
    *size_out = 0;
  }

  return bytes_used;
}

int32_t av_decode_video(AVStream2 *stream, uint8_t *data_in, size_t size_in,
                        uint8_t *data_out) {
  AVPicture picture_rgb;
  int got_picture;
  int width = stream->codec_context->width;
  int height = stream->codec_context->height;
  int bytes_used;

  if (stream->type != AVMEDIA_TYPE_VIDEO)
    return -1;

  // Some decoders read big chunks at a time, so you have to make a bigger
  // buffer
  uint8_t inbuf[size_in + FF_INPUT_BUFFER_PADDING_SIZE];
  // Set the padding portion of the buffer to all zeros
  memset(inbuf + size_in, 0, FF_INPUT_BUFFER_PADDING_SIZE);
  // Copy the data into the padded buffer
  memcpy(inbuf, data_in, size_in);

  AVPacket packet;
  av_init_packet(&packet);
  packet.data = inbuf;
  packet.size = size_in;

  bytes_used = avcodec_decode_video2(stream->codec_context, stream->frame,
                                     &got_picture, &packet);

  if (!got_picture)
    return -1;

  avpicture_fill(&picture_rgb, data_out, PIX_FMT_RGB24, width, height);
  static struct SwsContext *img_convert_ctx = NULL;
  img_convert_ctx = sws_getCachedContext(
      img_convert_ctx, width, height, stream->codec_context->pix_fmt, width,
      height, PIX_FMT_RGB24, SWS_FAST_BILINEAR, NULL, NULL, NULL);
  sws_scale(img_convert_ctx, (const uint8_t *const *)stream->frame->data,
            stream->frame->linesize, 0, height, picture_rgb.data,
            picture_rgb.linesize);

  return bytes_used;
}

map<unsigned int, double> keyframes;
unsigned int startDecodingAt;

class Grabber {
public:
  Grabber(bool isAudio, AVStream2 *stream, bool trySeeking, double rate,
          int bytesPerWORD, AVStreamInfo info, int64_t start_time) {
    this->stream = stream;
    frameNr = 0;
    packetNr = 0;
    done = false;
    this->bytesPerWORD = bytesPerWORD;
    this->rate = rate;
    startTime = 0;
    stopTime = 0;
    this->isAudio = isAudio;
    this->info = info;
    this->trySeeking = trySeeking;
    this->start_time = start_time > 0 ? start_time : 0;
  };

  ~Grabber() {
    // clean up any remaining memory...
    if (DEBUG)
      FFprintf("freeing frame data...\n");
    for (vector<uint8_t *>::iterator i = frames.begin(); i != frames.end(); i++)
      free(*i);
  }

  AVStream2 *stream;
  AVStreamInfo info;
  int64_t start_time;

  vector<uint8_t *> frames;
  vector<unsigned int> frameBytes;
  vector<double> frameTimes;

  vector<unsigned int> frameNrs;

  unsigned int frameNr;
  unsigned int packetNr;
  bool done;
  bool isAudio;
  bool trySeeking;

  int bytesPerWORD;
  double rate;
  double startTime, stopTime;

  int Grab(AVPacket2 *packet) {
    if (done)
      return 0;
    if (!packet->data)
      return 1;

    frameNr++;
    packetNr++;
    if (DEBUG)
      FFprintf("frameNr %d %d %d\n", frameNr, packetNr, packet->size);
    int offset = 0, len = 0;
    double timestamp = (packet->timestamp - start_time) / 1000.0 / 1000.0;
    if (DEBUG)
      FFprintf("time %lld %lld %lf\n", packet->timestamp, start_time,
               timestamp);

    // either no frames are specified (capture all), or we have time specified
    if (stopTime) {
      if (isAudio) {
        // time is being used...
        if (timestamp >= startTime) {
          // if we've reached the start...
          offset =
              max(0, ((int)((startTime - timestamp) * rate)) * bytesPerWORD);
          len = ((int)((stopTime - timestamp) * rate)) * bytesPerWORD;
          // if we have gone past our stop time...

          done = len < 0;
        }
      } else {
        done = stopTime <= timestamp;
        len = (startTime <= timestamp) ? 0x7FFFFFFF : 0;
        if (DEBUG)
          FFprintf("startTime: %lf, stopTime: %lf, current: %lf, done: %d, "
                   "len: %d\n",
                   startTime, stopTime, timestamp, done, len);
      }
    } else {
      // capture everything... video or audio
      len = 0x7FFFFFFF;
    }

    if (isAudio) {
      if (trySeeking && (len <= 0 || done))
        return 0;

      uint8_t audiobuf[1024 * 1024];
      int uint8_tsleft = sizeof(audiobuf);
      int uint8_tsout = uint8_tsleft;
      int uint8_tsread;
      uint8_t *audiodata = audiobuf;
      if (DEBUG)
        FFprintf("av_decode_audio\n");
      while ((uint8_tsread = av_decode_audio(stream, packet->data, packet->size,
                                             audiodata, &uint8_tsout)) > 0) {
        packet->data += uint8_tsread;
        packet->size -= uint8_tsread;
        audiodata += uint8_tsout;
        uint8_tsleft -= uint8_tsout;
        uint8_tsout = uint8_tsleft;
      }

      int nrBytes = audiodata - audiobuf;
      len = min(len, nrBytes);
      offset = min(offset, nrBytes);

      uint8_t *tmp = (uint8_t *)malloc(len);
      if (!tmp)
        return 2;

      memcpy(tmp, audiobuf + offset, len);

      frames.push_back(tmp);
      frameBytes.push_back(len);
      frameTimes.push_back(timestamp);

    } else {
      bool skip = false;
      if (frameNrs.size() > 0) {
        // frames are being specified
        // check to see if the frame is in our list
        bool foundNr = false;
        unsigned int lastFrameNr = 0;
        for (int i = 0; i < frameNrs.size(); i++) {
          if (frameNrs.at(i) == frameNr)
            foundNr = true;
          if (frameNrs.at(i) > lastFrameNr)
            lastFrameNr = frameNrs.at(i);
        }

        done = frameNr > lastFrameNr;
        if (!foundNr) {
          if (DEBUG)
            FFprintf("Skipping frame %d\n", frameNr);
          skip = true;
        }
      }
      if ((trySeeking && skip && packetNr < startDecodingAt && packetNr != 1) ||
          done)
        return 0;

      if (DEBUG)
        FFprintf("allocate frame %d\n", frames.size());
      uint8_t *videobuf = (uint8_t *)malloc(bytesPerWORD);
      if (!videobuf)
        return 2;
      if (DEBUG)
        FFprintf("av_decode_video\n");

      if (av_decode_video(stream, packet->data, packet->size, videobuf) <= 0) {
        if (DEBUG)
          FFprintf("av_decode_video FAILED!!!\n");
        // silently ignore decode errors
        frameNr--;
        free(videobuf);
        return 3;
      }

      if (stream->frame->key_frame) {
        keyframes[packetNr] = timestamp;
      }

      if (skip || len == 0) {
        free(videobuf);
        return 0;
      }
      frames.push_back(videobuf);
      frameBytes.push_back(min(len, bytesPerWORD));
      frameTimes.push_back(timestamp);
    }

    return 0;
  }
};

typedef map<int, Grabber *> streammap;

class FFGrabber {
public:
  FFGrabber();

  int build(char *filename, char *format, bool disableVideo, bool disableAudio,
            bool tryseeking);
  int doCapture();

  int getVideoInfo(unsigned int id, int *width, int *height, double *rate,
                   int *nrFramesCaptured, int *nrFramesTotal,
                   double *totalDuration);
  int getAudioInfo(unsigned int id, int *nrChannels, double *rate, int *bits,
                   int *nrFramesCaptured, int *nrFramesTotal, int *subtype,
                   double *totalDuration);
  void getCaptureInfo(int *nrVideo, int *nrAudio);
  // data must be freed by caller
  int getVideoFrame(unsigned int id, unsigned int frameNr, uint8_t **data,
                    unsigned int *nrBytes, double *time);
  // data must be freed by caller
  int getAudioFrame(unsigned int id, unsigned int frameNr, uint8_t **data,
                    unsigned int *nrBytes, double *time);
  void setFrames(unsigned int *frameNrs, int nrFrames);
  void setTime(double startTime, double stopTime);
  void disableVideo();
  void disableAudio();
  void cleanUp(); // must be called at the end, in order to render anything
                  // afterward.

#ifdef MATLAB_MEX_FILE
  void setMatlabCommand(char *matlabCommand);
  void setMatlabCommandHandle(mxArray *matlabCommandHandle);
  void runMatlabCommand(Grabber *G);
#endif
private:
  streammap streams;
  vector<Grabber *> videos;
  vector<Grabber *> audios;

  AVFile *file;
  AVFileInfo fileinfo;

  bool stopForced;
  bool tryseeking;
  vector<unsigned int> frameNrs;
  double startTime, stopTime;

  char *filename;
  struct stat filestat;

#ifdef MATLAB_MEX_FILE
  char *matlabCommand;
  mxArray *matlabCommandHandle;
  mxArray *prhs[6];
#endif
};

FFGrabber::FFGrabber() {
  stopForced = false;
  tryseeking = true;
  file = NULL;
  filename = NULL;

  av_register_all();
  avcodec_register_all();

  av_log_set_level(AV_LOG_QUIET);
}

void FFGrabber::cleanUp() {
  if (!file)
    return; // nothing to cleanup.

  for (streammap::iterator i = streams.begin(); i != streams.end(); i++) {
    av_close_stream(i->second->stream);
    delete i->second;
  }

  streams.clear();
  videos.clear();
  audios.clear();

  av_close_file(file);
  file = NULL;

#ifdef MATLAB_MEX_FILE
  if (matlabCommand)
    free(matlabCommand);
  matlabCommand = NULL;
#endif
}

int FFGrabber::getVideoInfo(unsigned int id, int *width, int *height,
                            double *rate, int *nrFramesCaptured,
                            int *nrFramesTotal, double *totalDuration) {
  if (!width || !height || !nrFramesCaptured || !nrFramesTotal)
    return -1;

  if (id >= videos.size())
    return -2;
  Grabber *CB = videos.at(id);

  if (!CB)
    return -1;

  *width = CB->info.video.width;
  *height = CB->info.video.height;
  *rate = CB->rate;
  *nrFramesCaptured = CB->frames.size();
  *nrFramesTotal = CB->frameNr;

  if (fileinfo.duration > 0) {
    *totalDuration = fileinfo.duration / 1000.0 / 1000.0;
    if (stopForced)
      *nrFramesTotal = (int)(-(*rate) * (*totalDuration));
  } else {
    *totalDuration = (*nrFramesCaptured) * (*rate);
  }

  return 0;
}

int FFGrabber::getAudioInfo(unsigned int id, int *nrChannels, double *rate,
                            int *bits, int *nrFramesCaptured,
                            int *nrFramesTotal, int *subtype,
                            double *totalDuration) {
  if (!nrChannels || !rate || !bits || !nrFramesCaptured || !nrFramesTotal)
    return -1;

  if (id >= audios.size())
    return -2;
  Grabber *CB = audios.at(id);

  if (!CB)
    return -1;

  *nrChannels = CB->info.audio.channels;
  *rate = CB->info.audio.sample_rate;
  *bits = CB->info.audio.sample_bits;
  *subtype = CB->info.audio.sample_format;
  *nrFramesCaptured = CB->frames.size();
  *nrFramesTotal = CB->frameNr;

  *totalDuration = fileinfo.duration / 1000.0 / 1000.0;

  return 0;
}

void FFGrabber::getCaptureInfo(int *nrVideo, int *nrAudio) {
  if (!nrVideo || !nrAudio)
    return;

  *nrVideo = videos.size();
  *nrAudio = audios.size();
}

// data must be freed by caller
int FFGrabber::getVideoFrame(unsigned int id, unsigned int frameNr,
                             uint8_t **data, unsigned int *nrBytes,
                             double *time) {
  if (DEBUG)
    FFprintf("getting Video frame %d\n", frameNr);

  if (!data || !nrBytes)
    return -1;

  if (id >= videos.size())
    return -2;
  Grabber *CB = videos[id];
  if (!CB)
    return -1;
  if (CB->frameNr == 0)
    return -2;
  if (frameNr < 0 || frameNr >= CB->frames.size())
    return -2;

  uint8_t *tmp = CB->frames[frameNr];
  if (!tmp)
    return -2;

  *nrBytes = CB->frameBytes[frameNr];
  *time = CB->frameTimes[frameNr];

  *data = tmp;
  CB->frames[frameNr] = NULL;

  return 0;
}

// data must be freed by caller
int FFGrabber::getAudioFrame(unsigned int id, unsigned int frameNr,
                             uint8_t **data, unsigned int *nrBytes,
                             double *time) {
  if (!data || !nrBytes)
    return -1;

  if (id >= audios.size())
    return -2;
  Grabber *CB = audios[id];
  if (!CB)
    return -1;
  if (CB->frameNr == 0)
    return -2;
  if (frameNr < 0 || frameNr >= CB->frames.size())
    return -2;

  uint8_t *tmp = CB->frames[frameNr];
  if (!tmp)
    return -2;

  *nrBytes = CB->frameBytes[frameNr];
  *time = CB->frameTimes[frameNr];

  *data = tmp;
  CB->frames[frameNr] = NULL;

  return 0;
}

void FFGrabber::setFrames(unsigned int *frameNrs, int nrFrames) {
  if (!frameNrs)
    return;

  unsigned int minFrame = nrFrames > 0 ? frameNrs[0] : 0;

  this->frameNrs.clear();
  for (int i = 0; i < nrFrames; i++)
    this->frameNrs.push_back(frameNrs[i]);

  for (int j = 0; j < videos.size(); j++) {
    Grabber *CB = videos.at(j);
    if (CB) {
      CB->frames.clear();
      CB->frameNrs.clear();
      for (int i = 0; i < nrFrames; i++) {
        CB->frameNrs.push_back(frameNrs[i]);
        minFrame = frameNrs[i] < minFrame ? frameNrs[i] : minFrame;
      }
      CB->frameNr = 0;
      CB->packetNr = 0;
    }
  }

  if (tryseeking && nrFrames > 0) {
    startDecodingAt = 0;
    for (map<unsigned int, double>::const_iterator it = keyframes.begin();
         it != keyframes.end(); it++) {
      if (it->first <= minFrame && it->first > startDecodingAt)
        startDecodingAt = it->first;
      if (DEBUG)
        FFprintf("%d %d\n", it->first, startDecodingAt);
    }
  }

  // the meaning of frames doesn't make much sense for audio...
}

void FFGrabber::setTime(double startTime, double stopTime) {
  this->startTime = startTime;
  this->stopTime = stopTime;
  frameNrs.clear();

  for (int i = 0; i < videos.size(); i++) {
    Grabber *CB = videos.at(i);
    if (CB) {
      CB->frames.clear();
      CB->frameNrs.clear();
      CB->frameNr = 0;
      CB->packetNr = 0;
      CB->startTime = startTime;
      CB->stopTime = stopTime;
    }
  }

  for (int i = 0; i < audios.size(); i++) {
    Grabber *CB = audios.at(i);
    if (CB) {
      CB->frames.clear();
      CB->frameNrs.clear();
      CB->startTime = startTime;
      CB->stopTime = stopTime;
    }
  }
}

#ifdef MATLAB_MEX_FILE
void FFGrabber::setMatlabCommand(char *matlabCommand) {
  delete this->matlabCommand;
  this->matlabCommand = matlabCommand;
  mxDestroyArray(matlabCommandHandle);
  matlabCommandHandle = NULL;
}

void FFGrabber::setMatlabCommandHandle(mxArray *matlabCommandHandle) {
  mxDestroyArray(this->matlabCommandHandle);
  if (!mxIsClass(matlabCommandHandle, "function_handle"))
    mexErrMsgTxt("blah");
  this->matlabCommandHandle = matlabCommandHandle;
  mexMakeArrayPersistent(matlabCommandHandle);
  delete matlabCommand;
  matlabCommand = NULL;
}

void FFGrabber::runMatlabCommand(Grabber *G) {
  if (matlabCommand || matlabCommandHandle) {
    mwSize dims[2];
    dims[1] = 1;
    int width = G->info.video.width, height = G->info.video.height;
    mxArray *plhs[] = {NULL};
    int ExitCode;

    mexSetTrapFlag(0);

    if (G->frames.size() == 0)
      return;
    vector<uint8_t *>::iterator lastframe = --(G->frames.end());

    if (*lastframe == NULL)
      return;

    dims[0] = G->frameBytes.back();

    if (prhs[0] == NULL) {
      // make matrices to pass to the matlab function
      prhs[0] = mxCreateNumericArray(2, dims, mxUINT8_CLASS,
                                     mxREAL); // empty 2d matrix
      prhs[1] = mxCreateDoubleMatrix(1, 1, mxREAL);
      mxGetPr(prhs[1])[0] = width;
      prhs[2] = mxCreateDoubleMatrix(1, 1, mxREAL);
      mxGetPr(prhs[2])[0] = height;
      prhs[3] = mxCreateDoubleMatrix(1, 1, mxREAL);
      prhs[4] = mxCreateDoubleMatrix(1, 1, mxREAL);
      mexMakeArrayPersistent(prhs[0]);
      mexMakeArrayPersistent(prhs[1]);
      mexMakeArrayPersistent(prhs[2]);
      mexMakeArrayPersistent(prhs[3]);
      mexMakeArrayPersistent(prhs[4]);
    }
    mxGetPr(prhs[3])[0] = G->frameNrs.size() == 0
                              ? G->frameTimes.size()
                              : G->frameNrs[G->frameTimes.size() - 1];
    mxGetPr(prhs[4])[0] = G->frameTimes.back();

    memcpy(mxGetPr(prhs[0]), *lastframe, dims[0]);

    // free the frame memory
    free(*lastframe);
    *lastframe = NULL;

    // call Matlab
    if (matlabCommand)
      ExitCode = mexCallMATLAB(0, plhs, 5, prhs, matlabCommand);
    else {
      for (int i = 4; i >= 0; i--)
        prhs[i + 1] = prhs[i];
      prhs[0] = matlabCommandHandle;
      ExitCode = mexCallMATLAB(0, plhs, 6, prhs, "feval");
      for (int i = 0; i <= 4; i++)
        prhs[i] = prhs[i + 1];
    }
  }
}
#endif

int FFGrabber::build(char *filename, char *format, bool disableVideo,
                     bool disableAudio, bool tryseeking) {
  if (DEBUG)
    FFprintf("av_open_filename\n");
  if (format && strlen(format) > 0)
    file = av_open_filename_with_format(filename, format);
  else
    file = av_open_filename_with_format(filename, NULL);
  if (!file)
    return -4;

  // detect if the file has changed
  struct stat fstat;
  stat(filename, &fstat);

  if (!this->filename || strcmp(this->filename, filename) != 0 ||
      filestat.st_mtime != fstat.st_mtime) {
    free(this->filename);
    this->filename = strdup(filename);
    memcpy(&filestat, &fstat, sizeof(fstat));

    keyframes.clear();
    startDecodingAt = 0xFFFFFFFF;
  }

  fileinfo.structure_size = sizeof(fileinfo);

  if (DEBUG)
    FFprintf("av_file_info\n");
  if (av_file_info(file, &fileinfo))
    return -1;

  // ugly hack but it is sometimes very wrong...
  if (fileinfo.start_time < -1000)
    fileinfo.start_time = 0;
  if (fileinfo.duration < -1000)
    fileinfo.duration = 0;

  for (int stream_index = 0; stream_index < fileinfo.n_streams;
       stream_index++) {
    AVStreamInfo streaminfo;
    streaminfo.structure_size = sizeof(streaminfo);

    if (DEBUG)
      FFprintf("av_stream_info\n");
    av_stream_info(file, stream_index, &streaminfo);

    if (DEBUG)
      FFprintf("start time: %lld\n", streaminfo, fileinfo.start_time);

    AVRational frame_rate = file->context->streams[stream_index]->r_frame_rate;
    streaminfo.video.frame_rate_num = frame_rate.num;
    streaminfo.video.frame_rate_den = frame_rate.den;

    if (DEBUG)
      FFprintf("stream id: %d is %s %d\n", stream_index,
               (streaminfo.type == AV_STREAM_TYPE_VIDEO
                    ? "Video"
                    : (streaminfo.type == AV_STREAM_TYPE_AUDIO ? "Audio"
                                                               : "Unknown")),
               disableVideo);

    if (streaminfo.type == AV_STREAM_TYPE_VIDEO && !disableVideo) {
      AVStream2 *tmp = av_open_stream(file, stream_index);
      if (tmp) {
        double rate = streaminfo.video.frame_rate_num /
                      (0.00001 + streaminfo.video.frame_rate_den);

        if (DEBUG)
          FFprintf("Inserting video stream %d\n", stream_index);
        streams[stream_index] =
            new Grabber(false, tmp, tryseeking, rate,
                        streaminfo.video.height * streaminfo.video.width * 3,
                        streaminfo, fileinfo.start_time);
        videos.push_back(streams[stream_index]);
      } else {
        FFprintf("Could not open video stream\n");
      }
    }
    if (streaminfo.type == AV_STREAM_TYPE_AUDIO && !disableAudio) {
      AVStream2 *tmp = av_open_stream(file, stream_index);
      if (tmp) {
        if (DEBUG)
          FFprintf("Inserting audio stream %d\n", stream_index);
        streams[stream_index] = new Grabber(
            true, tmp, tryseeking, streaminfo.audio.sample_rate,
            streaminfo.audio.sample_bits * streaminfo.audio.channels,
            streaminfo, fileinfo.start_time);
        audios.push_back(streams[stream_index]);
      } else {
        FFprintf("Could not open audio stream\n");
      }
    }
  }
  this->tryseeking = tryseeking;
  stopForced = false;

  if (streams.size() == 0)
    return -10;

  return 0;
}

int FFGrabber::doCapture() {
  AVPacket2 packet;
  packet.structure_size = sizeof(packet);
  streammap::iterator tmp;
  int needseek = 1;

  bool allDone = false;
  while (!av_read(file, &packet)) {
    if ((tmp = streams.find(packet.stream_index)) != streams.end()) {
      Grabber *G = tmp->second;
      G->Grab(&packet);

      if (G->done) {
        allDone = true;
        for (streammap::iterator i = streams.begin();
             i != streams.end() && allDone; i++) {
          allDone = allDone && i->second->done;
        }
      }

#ifdef MATLAB_MEX_FILE
      if (!G->isAudio)
        runMatlabCommand(G);
#endif
    } else if (DEBUG)
      FFprintf("Unknown packet %d\n", packet.stream_index);

    if (tryseeking && needseek) {
      if (stopTime && startTime > 0) {
        if (DEBUG)
          FFprintf("try seeking to %lf\n", startTime);
        av_seek_frame(file->context, -1, (int64_t)(startTime * 1000 * 1000),
                      AVSEEK_FLAG_BACKWARD);
      }
      needseek = 0;
    }

    if (allDone) {
      if (DEBUG)
        FFprintf("stopForced\n");
      stopForced = true;
      break;
    }
  }

#ifdef MATLAB_MEX_FILE
  if (prhs[0]) {
    mxDestroyArray(prhs[0]);
    if (prhs[1])
      mxDestroyArray(prhs[1]);
    if (prhs[2])
      mxDestroyArray(prhs[2]);
    if (prhs[3])
      mxDestroyArray(prhs[3]);
    if (prhs[4])
      mxDestroyArray(prhs[4]);
  }
  prhs[0] = NULL;
#endif

  return 0;
}

#ifdef MATLAB_MEX_FILE
FFGrabber FFG;

char *message(int err) {
  switch (err) {
  case 0:
    return "";
  case -1:
    return "Unable to initialize";
  case -2:
    return "Invalid interface";
  case -4:
    return "Unable to open file";
  case -5:
    return "AV version 8 or greater is required!";
  case -10:
    return "No input streams available.  Make sure you are not disabling audio "
           "or video.";
  default:
    return "Unknown error";
  }
}

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]) {
  if (nrhs < 1 || !mxIsChar(prhs[0]))
    mexErrMsgTxt("First parameter must be the command (a string)");

  char cmd[100];
  mxGetString(prhs[0], cmd, 100);

  if (!strcmp("build", cmd)) {
    if (nrhs < 6 || !mxIsChar(prhs[1]))
      mexErrMsgTxt("build: parameters must be the filename (as a string), "
                   "disableVideo, disableAudio, trySeeking");
    if (nlhs > 0)
      mexErrMsgTxt("build: there are no outputs");
    int filenamelen = mxGetN(prhs[1]) + 1;
    char *filename = new char[filenamelen];
    if (!filename)
      mexErrMsgTxt("build: out of memory");
    int formatlen = mxGetN(prhs[2]) + 1;
    char *format = new char[formatlen];
    mxGetString(prhs[1], filename, filenamelen);
    mxGetString(prhs[2], format, formatlen);

    if (strlen(format) == 0) {
      delete[] format;
      format = NULL;
    }

    char *errmsg =
        message(FFG.build(filename, format, mxGetScalar(prhs[3]),
                          mxGetScalar(prhs[4]), mxGetScalar(prhs[5])));
    delete[] format;
    delete[] filename;

    if (strcmp("", errmsg))
      mexErrMsgTxt(errmsg);
  } else if (!strcmp("doCapture", cmd)) {
    if (nlhs > 0)
      mexErrMsgTxt("doCapture: there are no outputs");
    char *errmsg = message(FFG.doCapture());
    if (strcmp("", errmsg))
      mexErrMsgTxt(errmsg);
  } else if (!strcmp("getVideoInfo", cmd)) {
    if (nrhs < 2 || !mxIsNumeric(prhs[1]))
      mexErrMsgTxt("getVideoInfo: second parameter must be the video stream id "
                   "(as a number)");
    if (nlhs > 6)
      mexErrMsgTxt("getVideoInfo: there are only 5 output values: width, "
                   "height, rate, nrFramesCaptured, nrFramesTotal");

    unsigned int id = (unsigned int)mxGetScalar(prhs[1]);
    int width, height, nrFramesCaptured, nrFramesTotal;
    double rate, totalDuration;
    char *errmsg =
        message(FFG.getVideoInfo(id, &width, &height, &rate, &nrFramesCaptured,
                                 &nrFramesTotal, &totalDuration));

    if (strcmp("", errmsg))
      mexErrMsgTxt(errmsg);

    if (nlhs >= 1) {
      plhs[0] = mxCreateDoubleMatrix(1, 1, mxREAL);
      mxGetPr(plhs[0])[0] = width;
    }
    if (nlhs >= 2) {
      plhs[1] = mxCreateDoubleMatrix(1, 1, mxREAL);
      mxGetPr(plhs[1])[0] = height;
    }
    if (nlhs >= 3) {
      plhs[2] = mxCreateDoubleMatrix(1, 1, mxREAL);
      mxGetPr(plhs[2])[0] = rate;
    }
    if (nlhs >= 4) {
      plhs[3] = mxCreateDoubleMatrix(1, 1, mxREAL);
      mxGetPr(plhs[3])[0] = nrFramesCaptured;
    }
    if (nlhs >= 5) {
      plhs[4] = mxCreateDoubleMatrix(1, 1, mxREAL);
      mxGetPr(plhs[4])[0] = nrFramesTotal;
    }
    if (nlhs >= 6) {
      plhs[5] = mxCreateDoubleMatrix(1, 1, mxREAL);
      mxGetPr(plhs[5])[0] = totalDuration;
    }
  } else if (!strcmp("getAudioInfo", cmd)) {
    if (nrhs < 2 || !mxIsNumeric(prhs[1]))
      mexErrMsgTxt("getAudioInfo: second parameter must be the audio stream id "
                   "(as a number)");
    if (nlhs > 7)
      mexErrMsgTxt("getAudioInfo: there are only 6 output values: nrChannels, "
                   "rate, bits, nrFramesCaptured, nrFramesTotal, subtype");

    unsigned int id = (unsigned int)mxGetScalar(prhs[1]);
    int nrChannels, bits, nrFramesCaptured, nrFramesTotal, subtype;
    double rate, totalDuration;
    char *errmsg = message(FFG.getAudioInfo(id, &nrChannels, &rate, &bits,
                                            &nrFramesCaptured, &nrFramesTotal,
                                            &subtype, &totalDuration));

    if (strcmp("", errmsg))
      mexErrMsgTxt(errmsg);

    if (nlhs >= 1) {
      plhs[0] = mxCreateDoubleMatrix(1, 1, mxREAL);
      mxGetPr(plhs[0])[0] = nrChannels;
    }
    if (nlhs >= 2) {
      plhs[1] = mxCreateDoubleMatrix(1, 1, mxREAL);
      mxGetPr(plhs[1])[0] = rate;
    }
    if (nlhs >= 3) {
      plhs[2] = mxCreateDoubleMatrix(1, 1, mxREAL);
      mxGetPr(plhs[2])[0] = bits;
    }
    if (nlhs >= 4) {
      plhs[3] = mxCreateDoubleMatrix(1, 1, mxREAL);
      mxGetPr(plhs[3])[0] = nrFramesCaptured;
    }
    if (nlhs >= 5) {
      plhs[4] = mxCreateDoubleMatrix(1, 1, mxREAL);
      mxGetPr(plhs[4])[0] = nrFramesTotal;
    }
    if (nlhs >= 6) {
      plhs[5] = mxCreateDoubleMatrix(1, 1, mxREAL);
      mxGetPr(plhs[5])[0] = subtype == AV_SAMPLE_FMT_FLT ? 1 : 0;
    }
    if (nlhs >= 7) {
      plhs[6] = mxCreateDoubleMatrix(1, 1, mxREAL);
      mxGetPr(plhs[6])[0] = totalDuration;
    }
  } else if (!strcmp("getCaptureInfo", cmd)) {
    if (nlhs > 2)
      mexErrMsgTxt(
          "getCaptureInfo: there are only 2 output values: nrVideo, nrAudio");

    int nrVideo, nrAudio;
    FFG.getCaptureInfo(&nrVideo, &nrAudio);

    if (nlhs >= 1) {
      plhs[0] = mxCreateDoubleMatrix(1, 1, mxREAL);
      mxGetPr(plhs[0])[0] = nrVideo;
    }
    if (nlhs >= 2) {
      plhs[1] = mxCreateDoubleMatrix(1, 1, mxREAL);
      mxGetPr(plhs[1])[0] = nrAudio;
    }
  } else if (!strcmp("getVideoFrame", cmd)) {
    if (nrhs < 3 || !mxIsNumeric(prhs[1]) || !mxIsNumeric(prhs[2]))
      mexErrMsgTxt("getVideoFrame: second parameter must be the audio stream "
                   "id (as a number) and third parameter must be the frame "
                   "number");
    if (nlhs > 2)
      mexErrMsgTxt("getVideoFrame: there are only 2 output value: data");

    unsigned int id = (unsigned int)mxGetScalar(prhs[1]);
    unsigned int frameNr = (unsigned int)mxGetScalar(prhs[2]);
    uint8_t *data;
    unsigned int nrBytes;
    double time;
    mwSize dims[2];
    dims[1] = 1;
    char *errmsg =
        message(FFG.getVideoFrame(id, frameNr, &data, &nrBytes, &time));

    if (strcmp("", errmsg))
      mexErrMsgTxt(errmsg);

    dims[0] = nrBytes;
    plhs[0] =
        mxCreateNumericArray(2, dims, mxUINT8_CLASS, mxREAL); // empty 2d matrix
    memcpy(mxGetPr(plhs[0]), data, nrBytes);
    free(data);
    if (nlhs >= 2) {
      plhs[1] = mxCreateDoubleMatrix(1, 1, mxREAL);
      mxGetPr(plhs[1])[0] = time;
    }
  } else if (!strcmp("getAudioFrame", cmd)) {
    if (nrhs < 3 || !mxIsNumeric(prhs[1]) || !mxIsNumeric(prhs[2]))
      mexErrMsgTxt("getAudioFrame: second parameter must be the audio stream "
                   "id (as a number) and third parameter must be the frame "
                   "number");
    if (nlhs > 2)
      mexErrMsgTxt("getAudioFrame: there are only 2 output value: data");

    unsigned int id = (unsigned int)mxGetScalar(prhs[1]);
    unsigned int frameNr = (unsigned int)mxGetScalar(prhs[2]);
    uint8_t *data;
    unsigned int nrBytes;
    double time;
    mwSize dims[2];
    dims[1] = 1;
    mxClassID mxClass;
    char *errmsg =
        message(FFG.getAudioFrame(id, frameNr, &data, &nrBytes, &time));

    if (strcmp("", errmsg))
      mexErrMsgTxt(errmsg);

    int nrChannels, bits, nrFramesCaptured, nrFramesTotal, subtype;
    double rate, totalDuration;
    FFG.getAudioInfo(id, &nrChannels, &rate, &bits, &nrFramesCaptured,
                     &nrFramesTotal, &subtype, &totalDuration);

    switch (bits) {
    case 8: {
      dims[0] = nrBytes;
      mxClass = mxUINT8_CLASS;
      break;
    }
    case 16: {
      mxClass = mxINT16_CLASS;
      dims[0] = nrBytes / 2;
      break;
    }
    case 24: {
      int *tmpdata = (int *)malloc(nrBytes / 3 * 4);
      int i;

      // I don't know how 24bit float data is organized...
      for (i = 0; i < nrBytes / 3; i++) {
        tmpdata[i] =
            (((0x80 & data[i * 3 + 2]) ? -1 : 0) & 0xFF000000) |
            ((data[i * 3 + 2] << 16) + (data[i * 3 + 1] << 8) + data[i * 3]);
      }

      free(data);
      data = (uint8_t *)tmpdata;

      mxClass = mxINT32_CLASS;
      dims[0] = nrBytes / 3;
      nrBytes = nrBytes / 3 * 4;
      break;
    }
    case 32: {
      mxClass =
          subtype == AV_SAMPLE_FMT_S32
              ? mxINT32_CLASS
              : subtype == AV_SAMPLE_FMT_FLT ? mxSINGLE_CLASS : mxUINT32_CLASS;
      dims[0] = nrBytes / 4;
      break;
    }
    default: {
      dims[0] = nrBytes;
      mxClass = mxUINT8_CLASS;
      break;
    }
    }

    plhs[0] = mxCreateNumericArray(2, dims, mxClass, mxREAL); // empty 2d matrix
    memcpy(mxGetPr(plhs[0]), data, nrBytes);
    free(data);
    if (nlhs >= 2) {
      plhs[1] = mxCreateDoubleMatrix(1, 1, mxREAL);
      mxGetPr(plhs[1])[0] = time;
    }
  } else if (!strcmp("setFrames", cmd)) {
    if (nrhs < 2 || !mxIsDouble(prhs[1]))
      mexErrMsgTxt(
          "setFrames: second parameter must be the frame numbers (as doubles)");
    if (nlhs > 0)
      mexErrMsgTxt("setFrames: has no outputs");
    int nrFrames = mxGetN(prhs[1]) * mxGetM(prhs[1]);
    unsigned int *frameNrs = new unsigned int[nrFrames];
    if (!frameNrs)
      mexErrMsgTxt("setFrames: out of memory");
    double *data = mxGetPr(prhs[1]);
    for (int i = 0; i < nrFrames; i++)
      frameNrs[i] = (unsigned int)data[i];

    FFG.setFrames(frameNrs, nrFrames);

    delete[] frameNrs;
  } else if (!strcmp("setTime", cmd)) {
    if (nrhs < 3 || !mxIsDouble(prhs[1]) || !mxIsDouble(prhs[2]))
      mexErrMsgTxt("setTime: start and stop time are required (as doubles)");
    if (nlhs > 0)
      mexErrMsgTxt("setTime: has no outputs");

    FFG.setTime(mxGetScalar(prhs[1]), mxGetScalar(prhs[2]));
  } else if (!strcmp("setMatlabCommand", cmd)) {
    if (nrhs < 2 ||
        !(mxIsChar(prhs[1]) || mxIsClass(prhs[1], "function_handle")))
      mexErrMsgTxt("setMatlabCommand: the command must be passed as a string "
                   "or function handle");
    if (nlhs > 0)
      mexErrMsgTxt("setMatlabCommand: has no outputs");

    if (mxIsChar(prhs[1])) {
      int len = mxGetN(prhs[1]) + 1;
      char *matlabCommand = new char[len];
      ;
      mxGetString(prhs[1], matlabCommand, len);

      if (strlen(matlabCommand) == 0) {
        FFG.setMatlabCommand(NULL);
        free(matlabCommand);
      } else
        FFG.setMatlabCommand(matlabCommand);
    } else {
      FFG.setMatlabCommandHandle(mxDuplicateArray(prhs[1]));
    }

  } else if (!strcmp("cleanUp", cmd)) {
    if (nlhs > 0)
      mexErrMsgTxt("cleanUp: there are no outputs");
    FFG.cleanUp();
  }
}
#endif

#ifdef TEST_FFGRAB
int main(int argc, char **argv) {
  FFGrabber FFG;
  printf("%s\n", argv[1]);
  FFG.build(argv[1], NULL, false, false, true);
  FFG.doCapture();
  int nrVideo, nrAudio;
  FFG.getCaptureInfo(&nrVideo, &nrAudio);

  printf("there are %d video streams, and %d audio.\n", nrVideo, nrAudio);
}
#endif
