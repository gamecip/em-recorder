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
	int w, h, framerate;
	
	AVStream *st;
	AVCodecContext *enc;

	unsigned long next_pts;

	AVFrame *frame;

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
	ost->frame = NULL;
	sws_freeContext(ost->sws_ctx);
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

	c->bit_rate = 400000;

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

void add_video_frame(int recording, long frame, unsigned char*rgba, int len) {
	assert(!recordings[recording].finished);
	assert(recordings[recording].encoding_video);
	recordings[recording].video_st.next_pts = frame;
	recordings[recording].encoding_video = !write_video_frame(recordings[recording].oc, &recordings[recording].video_st, rgba);
}

void add_audio_frame(int recording, unsigned char*bytes, int len) {
	assert(!recordings[recording].finished);
	assert(recordings[recording].encoding_audio);
	// (!encode_audio || av_compare_ts(video_st.next_pts, video_st.enc->time_base,
	// audio_st.next_pts, audio_st.enc->time_base) <= 0)
	//else: encode_audio = !process_audio_stream(oc, &audio_st);
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
			//todo
		}
	}
	av_write_trailer(r.oc);

	close_stream(r.oc, &r.video_st);

	if (!(r.fmt->flags & AVFMT_NOFILE)) {
		avio_close(r.oc->pb);
	}
	avformat_free_context(r.oc);
	r.oc = NULL;
	recordings[recording] = r;
}

int start_recording(int w, int h, int fps) {
	if(next_recording >= recording_count) {
		recording_count *= 2;
		recordings = realloc(recordings, recording_count*sizeof(Recording));
	}
	Recording r = {0};
	r.recordingID = next_recording;
	r.encoding_video = 1;
	r.encoding_audio = 0;
	r.finished = 0;
	r.video_st.w = w;
	r.video_st.h = h;
	r.video_st.framerate = fps;
	
	av_register_all();
	r.fmt = av_guess_format("mp4", NULL, NULL);
	r.oc = avformat_alloc_context();
	r.oc->oformat = r.fmt;
	snprintf(r.oc->filename, sizeof(r.oc->filename), "recording-%d.mp4", r.recordingID);
	add_video_stream(&r.video_st, r.oc, r.fmt->video_codec);
	open_video(r.oc, &r.video_st);
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
	printf("Starting!\n");
	recordings = calloc(recording_count, sizeof(Recording));
	emscripten_exit_with_live_runtime();
	return 0;
}