#include <stdio.h>
#include <assert.h>
#include <emscripten.h>
#include "libavcodec/avcodec.h"
#include "libavutil/channel_layout.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavformat/avformat.h"
#include "libavresample/avresample.h"
#include "libswscale/swscale.h"

#define OUT_FMT	AV_PIX_FMT_YUV420P

typedef struct OutputStream {
	int w, h, framerate, bitrate;
	
	AVStream *st;
	AVCodecContext *enc;

	unsigned long next_pts;

	AVFrame *frame, *tmp_frame;

	float t, tincr, tincr2;

	struct SwsContext *sws_ctx;
	AVAudioResampleContext *avr;
} OutputStream;

typedef struct Recording {
	int recordingID;
	OutputStream video_st, audio_st;
	AVOutputFormat *fmt;
	AVFormatContext *oc;
	int encoding_video;
	int encoding_audio;
	int finished;
} Recording;

Recording *recordings;
int next_recording = 0;
int recording_count = 10;

//audio

static void add_audio_stream(OutputStream *ost, AVFormatContext *oc,
                             enum AVCodecID codec_id)
{
    AVCodecContext *c;
    AVCodec *codec;
    int ret;
     // find the audio encoder
    codec = avcodec_find_encoder(codec_id);
    if (!codec) {
        fprintf(stderr, "codec not found\n");
        exit(1);
    }
    ost->st = avformat_new_stream(oc, NULL);
    if (!ost->st) {
        fprintf(stderr, "Could not alloc stream\n");
        exit(1);
    }
    c = avcodec_alloc_context3(codec);
    if (!c) {
        fprintf(stderr, "Could not alloc an encoding context\n");
        exit(1);
    }
    ost->enc = c;
		c->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
     // put sample parameters
    c->sample_fmt     = codec->sample_fmts           ? codec->sample_fmts[0]           : AV_SAMPLE_FMT_S16;
    c->sample_rate    = codec->supported_samplerates ? codec->supported_samplerates[0] : 16000;
    c->channel_layout = codec->channel_layouts       ? codec->channel_layouts[0]       : AV_CH_LAYOUT_STEREO;
    c->channels       = av_get_channel_layout_nb_channels(c->channel_layout);
    c->bit_rate       = 32000;
    ost->st->time_base = (AVRational){ 1, c->sample_rate };
    // some formats want stream headers to be separate
    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
     // initialize sample format conversion;
     // * to simplify the code, we always pass the data through lavr, even
     // * if the encoder supports the generated format directly -- the price is
     // * some extra data copying;
     //
    ost->avr = avresample_alloc_context();
    if (!ost->avr) {
        fprintf(stderr, "Error allocating the resampling context\n");
        exit(1);
    }
    av_opt_set_int(ost->avr, "in_sample_fmt",      AV_SAMPLE_FMT_FLT,   0);
    av_opt_set_int(ost->avr, "in_sample_rate",     ost->framerate,      0);
    av_opt_set_int(ost->avr, "in_channel_layout",  AV_CH_LAYOUT_STEREO, 0);
    av_opt_set_int(ost->avr, "out_sample_fmt",     c->sample_fmt,       0);
    av_opt_set_int(ost->avr, "out_sample_rate",    c->sample_rate,      0);
    av_opt_set_int(ost->avr, "out_channel_layout", c->channel_layout,   0);
    ret = avresample_open(ost->avr);
    if (ret < 0) {
        fprintf(stderr, "Error opening the resampling context\n");
        exit(1);
    }
}
static AVFrame *alloc_audio_frame(enum AVSampleFormat sample_fmt,
                                  uint64_t channel_layout,
                                  int sample_rate, int nb_samples)
{
    AVFrame *frame = av_frame_alloc();
    int ret;
    if (!frame) {
        fprintf(stderr, "Error allocating an audio frame\n");
        exit(1);
    }
    frame->format = sample_fmt;
    frame->channel_layout = channel_layout;
    frame->sample_rate = sample_rate;
    frame->nb_samples = nb_samples;
    if (nb_samples) {
        ret = av_frame_get_buffer(frame, 0);
        if (ret < 0) {
            fprintf(stderr, "Error allocating an audio buffer\n");
            exit(1);
        }
    }
    return frame;
}
static void open_audio(AVFormatContext *oc, OutputStream *ost)
{
    AVCodecContext *c;
    int nb_samples, ret;
    c = ost->enc;
     // open it
		ret = avcodec_open2(c, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "could not open codec: -%x : %d\n",-ret,ret);
        exit(1);
    }
    if (c->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE)
        nb_samples = 10000;
    else
        nb_samples = c->frame_size;
    fprintf(stderr,"Open codec with sample rate %d sample count %d\n",(int)c->sample_rate,nb_samples);
    ost->frame     = alloc_audio_frame(c->sample_fmt, c->channel_layout,
                                       c->sample_rate, nb_samples);
    ost->tmp_frame = alloc_audio_frame(AV_SAMPLE_FMT_FLT, AV_CH_LAYOUT_STEREO,
                                       ost->framerate, ost->framerate);
     // copy the stream parameters to the muxer
    ret = avcodec_parameters_from_context(ost->st->codecpar, c);
    if (ret < 0) {
        fprintf(stderr, "Could not copy the stream parameters\n");
        exit(1);
    }
} 
 // Prepare a 32 bit dummy audio frame of 'frame_size' samples and
 // * 'nb_channels' channels.
static AVFrame *get_audio_frame(OutputStream *ost, float_t *samples)
{
    AVFrame *frame = ost->tmp_frame;
    int j, i, v;
    float_t *q = (float_t*)frame->extended_data[0];
    memcpy(q, samples, frame->nb_samples*sizeof(float_t)*2);
    frame->pts = ost->next_pts;
    return frame;
}
 // if a frame is provided, send it to the encoder, otherwise flush the encoder;
 // * return 1 when encoding is finished, 0 otherwise
 //
static int encode_audio_frame(AVFormatContext *oc, OutputStream *ost,
                              AVFrame *frame)
{
    AVPacket pkt = { 0 }; // data and size must be 0;
    int got_packet;
    av_init_packet(&pkt);
    avcodec_encode_audio2(ost->enc, &pkt, frame, &got_packet);
    if (got_packet) {
        pkt.stream_index = ost->st->index;
        av_packet_rescale_ts(&pkt, ost->enc->time_base, ost->st->time_base);
         // Write the compressed frame to the media file.
        if (av_interleaved_write_frame(oc, &pkt) != 0) {
            fprintf(stderr, "Error while writing audio frame\n");
            exit(1);
        }
    }
    return (frame || got_packet) ? 0 : 1;
}


 //
 // * encode one audio frame and send it to the muxer
 // * return 1 when encoding is finished, 0 otherwise
 //
static int write_audio_frame(AVFormatContext *oc, OutputStream *ost, float_t* samples)
{
    AVFrame *frame = NULL;
    int got_output = 0;
    int ret;
    if(samples) {
        frame = get_audio_frame(ost,samples);        
    }
    got_output |= !!frame;
     // feed the data to lavr
    if (frame) {
        ret = avresample_convert(ost->avr, NULL, 0, 0,
                                 frame->extended_data, frame->linesize[0],
                                 frame->nb_samples);
        if (ret < 0) {
            fprintf(stderr, "Error feeding audio data to the resampler\n");
            exit(1);
        }
    }
    while ((frame && avresample_available(ost->avr) >= ost->frame->nb_samples && ost->frame->nb_samples > 0) ||
           (!frame && avresample_get_out_samples(ost->avr, 0))) {
         // when we pass a frame to the encoder, it may keep a reference to it
         // * internally;
         // * make sure we do not overwrite it here
         //
        ret = av_frame_make_writable(ost->frame);
        if (ret < 0)
            exit(1);
         // the difference between the two avresample calls here is that the
         // * first one just reads the already converted data that is buffered in
         // * the lavr output buffer, while the second one also flushes the
         // * resampler
        if (frame) {
            ret = avresample_read(ost->avr, ost->frame->extended_data,
                                  ost->frame->nb_samples);
        } else {
            ret = avresample_convert(ost->avr, ost->frame->extended_data,
                                     ost->frame->linesize[0], ost->frame->nb_samples,
                                     NULL, 0, 0);
        }
        if (ret < 0) {
            fprintf(stderr, "Error while resampling\n");
            exit(1);
        } else if (frame && ret != ost->frame->nb_samples) {
            fprintf(stderr, "Too few samples returned from lavr\n");
            exit(1);
        }
        ost->frame->nb_samples = ret;
        ost->frame->pts        = ost->next_pts;
        ost->next_pts         += ost->frame->nb_samples;
        got_output |= encode_audio_frame(oc, ost, ret ? ost->frame : NULL);
        if(!got_output && ret == 0) { 
            fprintf(stderr,"finished, next pts %d\n",(int)ost->next_pts);
            break; 
        }
    }
    return !got_output;
}

//video

static AVFrame *alloc_picture(enum AVPixelFormat pix_fmt, int width, int height)
{
	AVFrame *picture;
	int ret;
	picture = av_frame_alloc();
	if (!picture) {
		return NULL;
	}
	picture->format = pix_fmt;
	picture->width = width;
	picture->height = height;
	/* allocate the buffers for the frame data */
	ret = av_frame_get_buffer(picture, 32);
	if (ret < 0) {
		fprintf(stderr, "Could not allocate frame data.\n");
		exit(1);
	}
	return picture;
}

static void open_video(AVFormatContext *oc, OutputStream *ost)
{
	AVCodecContext *c;
	int ret;
	c = ost->enc;
	/* open the codec */
	if (avcodec_open2(c, NULL, NULL) < 0) {
		fprintf(stderr, "could not open codec\n");
		exit(1);
	}
	/* Allocate the encoded raw picture. */
	ost->frame = alloc_picture(c->pix_fmt, c->width, c->height);
	/* copy the stream parameters to the muxer */
	ret = avcodec_parameters_from_context(ost->st->codecpar, c);
	if (ret < 0) {
		fprintf(stderr, "Could not copy the stream parameters\n");
		exit(1);
	}
}

static void close_stream(AVFormatContext *oc, OutputStream *ost)
{
	avcodec_free_context(&ost->enc);
	ost->enc = NULL;
	av_frame_free(&ost->frame);
	av_frame_free(&ost->tmp_frame);
	ost->frame = NULL;
	if(ost->sws_ctx) {
		sws_freeContext(ost->sws_ctx);
	}
	ost->sws_ctx = NULL;
	if(ost->avr != NULL) {
		avresample_free(&ost->avr);
	}
	ost->avr = NULL;
}

static void add_video_stream(OutputStream *ost, AVFormatContext *oc, enum AVCodecID codec_id)
{
	printf("Adding stream\n");
	AVCodecContext *c;
	AVCodec *codec;

	codec = avcodec_find_encoder(codec_id);

	ost->st = avformat_new_stream(oc, NULL);

	c = avcodec_alloc_context3(codec);
	
	av_opt_set(c->priv_data, "preset", "superfast", 0);
	av_opt_set(c->priv_data, "tune", "animation", 0);

	ost->enc = c;

	c->bit_rate = ost->bitrate;

	c->width = ost->w;
	c->height = ost->h;

	ost->st->time_base = (AVRational){ 1, ost->framerate };
	c->time_base = ost->st->time_base;

	c->gop_size = 12; 
	c->pix_fmt = OUT_FMT;
	
	if (c->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
		c->max_b_frames = 2;
	}
	if (c->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
		c->mb_decision = 2;
	}
	if (oc->oformat->flags & AVFMT_GLOBALHEADER) {
		c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}
}

static AVFrame *rgba_to_yuv(OutputStream *ost, unsigned char *rgba)
{
	AVCodecContext *c = ost->enc;
	if (!ost->sws_ctx) {
		ost->sws_ctx = sws_getContext(c->width, c->height,
									  AV_PIX_FMT_RGBA,
									  c->width, c->height,
									  c->pix_fmt,
									  0, 0, 0, 0);
		if (!ost->sws_ctx) {
			fprintf(stderr,
					"Cannot initialize the conversion context\n");
			exit(1);
		}
	}
	const uint8_t * inData[1] = { rgba };
	const int inLinesize[1] = { 4 * c->width };
	sws_scale(ost->sws_ctx, inData, inLinesize, 0, c->height, 
			  ost->frame->data, ost->frame->linesize);

	return ost->frame;
}
	
static int write_video_frame(AVFormatContext *oc, OutputStream *ost, unsigned char *rgba)
{
	int ret;
	AVCodecContext *c;
	AVFrame *frame = NULL;
	AVPacket pkt   = { 0 };
	int got_packet = 0;

	c = ost->enc;
	if(rgba) {
		frame = rgba_to_yuv(ost,rgba);
		ost->frame->pts = frame->pts = ost->next_pts;
		ost->next_pts++;
	}

	av_init_packet(&pkt);
	
	ret = avcodec_encode_video2(c, &pkt, frame, &got_packet);
	if (ret < 0) {
		fprintf(stderr, "Error encoding a video frame\n");
		exit(1);
	}

	if (got_packet) {
		av_packet_rescale_ts(&pkt, c->time_base, ost->st->time_base);
		pkt.stream_index = ost->st->index;
		ret = av_interleaved_write_frame(oc, &pkt);
	}

	if (ret != 0) {
		fprintf(stderr, "Error while writing video frame\n");
		exit(1);
	}

	return (frame || got_packet) ? 0 : 1;
}

void add_video_frame(int recording, long frame, unsigned char*rgba) {
	assert(!recordings[recording].finished);
	assert(recordings[recording].encoding_video);
	recordings[recording].video_st.next_pts = frame;
	recordings[recording].encoding_video = !write_video_frame(recordings[recording].oc, &recordings[recording].video_st, rgba);
}

void add_audio_frame(int recording, long frame, float_t*samples) {
	Recording r = recordings[recording];
	assert(!r.finished);
	assert(r.encoding_audio);
	r.audio_st.next_pts = av_rescale_q(frame,(AVRational){ 1, r.audio_st.framerate },r.audio_st.enc->time_base);
	r.encoding_audio = !write_audio_frame(r.oc, &r.audio_st, samples);
	recordings[recording] = r;
}

void end_recording(int recording) {
	Recording r = recordings[recording];
	assert(!r.finished);
	r.finished = 1;
	//write the delayed frames
	while(r.encoding_video || r.encoding_audio) {
		if(r.encoding_video) {
			r.encoding_video = !write_video_frame(r.oc, &r.video_st, NULL);
		} else if(r.encoding_audio) {
			r.encoding_audio = !write_audio_frame(r.oc, &r.audio_st, NULL);
		}
	}
	av_write_trailer(r.oc);

	close_stream(r.oc, &r.video_st);
	close_stream(r.oc, &r.audio_st);

	if (!(r.fmt->flags & AVFMT_NOFILE)) {
		avio_close(r.oc->pb);
	}
	avformat_free_context(r.oc);
	r.oc = NULL;
	recordings[recording] = r;
}

int start_recording(int w, int h, int fps, int sps, int br) {
	printf("Started %d!\n",next_recording);
	if(next_recording >= recording_count) {
		recording_count *= 2;
		recordings = realloc(recordings, recording_count*sizeof(Recording));
	}
	Recording r = {0};
	r.recordingID = next_recording;
	r.encoding_video = 1;
	r.encoding_audio = 1;
	r.finished = 0;
	r.video_st.w = w;
	r.video_st.h = h;
	r.video_st.framerate = fps;
	r.video_st.bitrate = br;
	r.audio_st.framerate = sps;
	
	av_register_all();
	r.fmt = av_guess_format("mp4", NULL, NULL);
	r.oc = avformat_alloc_context();
	r.oc->oformat = r.fmt;
	snprintf(r.oc->filename, sizeof(r.oc->filename), "recording-%d.mp4", r.recordingID);
	add_video_stream(&r.video_st, r.oc, r.fmt->video_codec);
	add_audio_stream(&r.audio_st, r.oc, r.fmt->audio_codec);
	open_video(r.oc, &r.video_st);
	open_audio(r.oc, &r.audio_st);
	av_dump_format(r.oc, 0, r.oc->filename, 1);
	
	if (!(r.fmt->flags & AVFMT_NOFILE)) {
		if (avio_open(&r.oc->pb, r.oc->filename, AVIO_FLAG_WRITE) < 0) {
				fprintf(stderr, "Could not open '%s'\n", r.oc->filename);
				return 1;
		}
	}
	avformat_write_header(r.oc, NULL);
	recordings[next_recording] = r;
	next_recording++;
	return r.recordingID;
}

int main(int argc, char *argv[]) {
	recordings = calloc(recording_count, sizeof(Recording));
	emscripten_exit_with_live_runtime();
	return 0;
}
