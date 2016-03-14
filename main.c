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

	int64_t next_pts;

	AVFrame *frame;

	float t, tincr, tincr2;

	struct SwsContext *sws_ctx;
//	AVAudioResampleContext *avr;
} OutputStream;

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
	av_frame_free(&ost->frame);
	sws_freeContext(ost->sws_ctx);
	//todo: put back
	//avresample_free(&ost->avr);
}

static void add_video_stream(OutputStream *ost, AVFormatContext *oc, enum AVCodecID codec_id)
{
	printf("Adding stream\n");
	AVCodecContext *c;
	AVCodec *codec;

	codec = avcodec_find_encoder(codec_id);

	ost->st = avformat_new_stream(oc, NULL);

	c = avcodec_alloc_context3(codec);

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

	ost->frame->pts = ost->next_pts++;

	return ost->frame;
}
	
static int write_video_frame(AVFormatContext *oc, OutputStream *ost, unsigned char *rgba)
{
	int ret;
	AVCodecContext *c;
	AVFrame *frame;
	AVPacket pkt   = { 0 };
	int got_packet = 0;

	c = ost->enc;

	frame = rgba_to_yuv(ost,rgba);

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

AVOutputFormat *fmt;
AVFormatContext *oc;
OutputStream video_st = {0};
int encoding_video = 1;
int encoding_audio = 0;
int finished = 0;

void main_loop() {
	if(finished) {
		emscripten_cancel_main_loop();
		emscripten_force_exit(0);
	}
}

void add_video_frame(unsigned char*rgba, int len) {
	assert(!finished);
	encoding_video = write_video_frame(oc, &video_st, rgba);
}

void add_audio_frame(unsigned char*bytes, int len) {
	assert(!finished);
	//assert(encoding_audio);
	// (!encode_audio || av_compare_ts(video_st.next_pts, video_st.enc->time_base,
	// audio_st.next_pts, audio_st.enc->time_base) <= 0)
	//else: encode_audio = !process_audio_stream(oc, &audio_st);
}

void end_recording() {
	finished = 1;
	while(encoding_video) {
		encoding_video = write_video_frame(oc, &video_st, NULL);
	}
	av_write_trailer(oc);

	close_stream(oc, &video_st);

	if (!(fmt->flags & AVFMT_NOFILE)) {
		avio_close(oc->pb);
	}
	avformat_free_context(oc);
}

int main(int argc, char *argv[]) {
	int w = 320;
	int h = 240;
	video_st.w = w;
	video_st.h = h;
	video_st.framerate = 30;
	
	av_register_all();
	fmt = av_guess_format("mp4", NULL, NULL);
	oc = avformat_alloc_context();
	oc->oformat = fmt;
	char *filename = "recording-1.mp4";
	snprintf(oc->filename, sizeof(oc->filename), "%s", filename);
	add_video_stream(&video_st, oc, fmt->video_codec);
	open_video(oc, &video_st);
	av_dump_format(oc, 0, filename, 1);
	
	if (!(fmt->flags & AVFMT_NOFILE)) {
		if (avio_open(&oc->pb, filename, AVIO_FLAG_WRITE) < 0) {
			fprintf(stderr, "Could not open '%s'\n", filename);
			return 1;
		}
	}
	
	avformat_write_header(oc, NULL);
	emscripten_set_main_loop(main_loop, video_st.framerate, 1);
	return 0;
}