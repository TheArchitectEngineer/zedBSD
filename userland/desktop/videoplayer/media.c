/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The media thread of the player: FFmpeg's libavformat reads the file,
 * libavcodec decodes its best video stream and its best audio stream.  The
 * pictures wait in a small ring for the window to take them at their time;
 * the sound is converted (libswresample) to 16-bit stereo at audiod's rate
 * and written into the stream's ring, which paces the reading.  A seek
 * drops what is decoded and queued, and starts the clock again at the
 * time sought.
 */

#include "videoplayer.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>

#include <errno.h>
#include <string.h>
#include <time.h>

/* The longest a full ring of sound or pictures is waited on before looking at the requests again (ms). */
#define MEDIA_WAIT_MS		10

/* The sound converted at once at most (frames). */
#define MEDIA_SOUND_FRAMES	8192

/*
 * What the media thread holds: the file, the two decoders, the converter,
 * the packet and frame being worked on, and the converted sound.
 */
struct media_reader {
	struct vp_media *media;
	AVFormatContext *format;
	AVCodecContext *video;
	AVCodecContext *sound;
	SwrContext *converter;
	AVPacket *packet;
	AVFrame *frame;
	int video_stream;
	int sound_stream;
	double video_base;
	double sound_base;
	double skip_before;
	int16_t samples[MEDIA_SOUND_FRAMES * 2];
};

static void *media_run(void *argument);
static int media_reader_open(struct media_reader *reader, const char *path);
static void media_reader_close(struct media_reader *reader);
static int media_decoder(AVFormatContext *format, enum AVMediaType type, AVCodecContext **result, int *stream);
static int media_feed(struct media_reader *reader, AVCodecContext *decoder, const AVPacket *packet);
static int media_picture(struct media_reader *reader, AVFrame *frame);
static int media_sound(struct media_reader *reader, AVFrame *frame);
static int media_seek(struct media_reader *reader);
static void media_drop_pictures(struct vp_media *media);
static int media_stopping(struct vp_media *media);
static uint64_t media_now_us(void);
static void media_sleep_ms(unsigned ms);

/*
 * Makes the media empty, with the sound it plays to (NULL or a stream that
 * is not made: no sound).
 */
void
vp_media_init(
	struct vp_media *media,
	struct vp_audio *audio)
{
	/* Nothing open. */
	memset(media, 0, sizeof(*media));
	media->audio = audio;
	(void)pthread_mutex_init(&media->lock, NULL);
	(void)pthread_cond_init(&media->wake, NULL);
}

/*
 * Opens a file and starts reading it, paused at its start.  Returns 0, or
 * an errno value (the file is not one FFmpeg reads, or has no video).
 */
int
vp_media_open(
	struct vp_media *media,
	const char *path)
{
	struct media_reader *reader;
	int error;

	/* What was open goes. */
	vp_media_close(media);

	/* The reader, opened here so that a file that cannot be played is told at once. */
	reader = av_mallocz(sizeof(*reader));
	if (reader == NULL)
		return ENOMEM;
	reader->media = media;
	error = media_reader_open(reader, path);
	if (error != 0) {
		media_reader_close(reader);
		av_free(reader);
		return error;
	}

	/* What is known of the file, paused at its start. */
	(void)pthread_mutex_lock(&media->lock);
	(void)snprintf(media->path, sizeof(media->path), "%s", path);
	media->width = reader->video->width;
	media->height = reader->video->height;
	media->duration = 0.0;
	if (reader->format->duration > 0)
		media->duration = (double)reader->format->duration / (double)AV_TIME_BASE;
	media->has_audio = reader->sound != NULL;
	media->state = VP_PAUSED;
	media->eof = 0;
	media->quit = 0;
	media->seek_wanted = 0;
	media->clock_time = 0.0;
	media->clock_frames = vp_audio_write_position(media->audio);
	media->clock_us = media_now_us();
	(void)pthread_mutex_unlock(&media->lock);

	/* The thread reads ahead from here. */
	error = pthread_create(&media->thread, NULL, media_run, reader);
	if (error != 0) {
		media_reader_close(reader);
		av_free(reader);
		return error;
	}

	/* Succeeded: the thread owns the reader. */
	media->thread_started = 1;
	return 0;
}

/*
 * Ends the reading and forgets the file.
 */
void
vp_media_close(
	struct vp_media *media)
{
	/* The thread is asked to end, and waited for. */
	if (media->thread_started) {
		(void)pthread_mutex_lock(&media->lock);
		media->quit = 1;
		(void)pthread_cond_broadcast(&media->wake);
		(void)pthread_mutex_unlock(&media->lock);
		(void)pthread_join(media->thread, NULL);
		media->thread_started = 0;
	}

	/* The sound stops and is dropped. */
	if (media->audio != NULL) {
		(void)vp_audio_stop(media->audio);
		(void)vp_audio_flush(media->audio);
	}

	/* The pictures and the state. */
	(void)pthread_mutex_lock(&media->lock);
	media_drop_pictures(media);
	media->state = VP_EMPTY;
	media->path[0] = '\0';
	media->eof = 0;
	(void)pthread_mutex_unlock(&media->lock);
}

/*
 * Plays from where it is (from the start again after the end).
 */
void
vp_media_play(
	struct vp_media *media)
{
	unsigned state;

	/* From the start again after the end. */
	(void)pthread_mutex_lock(&media->lock);
	state = media->state;
	(void)pthread_mutex_unlock(&media->lock);
	if (state == VP_ENDED)
		vp_media_seek(media, 0.0);
	if (state == VP_EMPTY || state == VP_PLAYING)
		return;

	/* The clock goes on from where it stood. */
	(void)pthread_mutex_lock(&media->lock);
	media->clock_time = vp_media_clock(media);
	media->clock_frames = vp_audio_read_position(media->audio);
	media->clock_us = media_now_us();
	media->state = VP_PLAYING;
	(void)pthread_cond_broadcast(&media->wake);
	(void)pthread_mutex_unlock(&media->lock);

	/* And the sound with it. */
	if (media->has_audio)
		(void)vp_audio_start(media->audio);
}

/*
 * Pauses where it is.
 */
void
vp_media_pause(
	struct vp_media *media)
{
	double now;

	/* Only while playing. */
	(void)pthread_mutex_lock(&media->lock);
	if (media->state != VP_PLAYING) {
		(void)pthread_mutex_unlock(&media->lock);
		return;
	}

	/* The clock stands where it is. */
	now = vp_media_clock(media);
	media->state = VP_PAUSED;
	media->clock_time = now;
	(void)pthread_mutex_unlock(&media->lock);

	/* And the sound with it. */
	if (media->has_audio)
		(void)vp_audio_stop(media->audio);
}

/*
 * Asks for a seek to a time (seconds, kept within the file).
 */
void
vp_media_seek(
	struct vp_media *media,
	double seconds)
{
	/* Within the file. */
	if (seconds < 0.0)
		seconds = 0.0;
	if (media->duration > 0.0 && seconds > media->duration)
		seconds = media->duration;

	/* The request; the clock stands at the time sought until the thread starts it again. */
	(void)pthread_mutex_lock(&media->lock);
	media->seek_wanted = 1;
	media->seek_to = seconds;
	media->clock_time = seconds;
	media->clock_us = media_now_us();
	if (media->state == VP_ENDED)
		media->state = VP_PAUSED;
	media_drop_pictures(media);
	(void)pthread_cond_broadcast(&media->wake);
	(void)pthread_mutex_unlock(&media->lock);
}

/*
 * Reports the clock (seconds): the sound's while playing with sound, the
 * monotonic clock's while playing without, standing still otherwise.
 * Called with or without the lock (it reads the anchors only).
 */
double
vp_media_clock(
	struct vp_media *media)
{
	uint64_t frames;
	uint64_t now;

	/* Standing still. */
	if (media->state != VP_PLAYING || media->seek_wanted)
		return media->clock_time;

	/* The sound read since the anchor. */
	if (media->has_audio && media->audio != NULL && media->audio->created) {
		frames = vp_audio_read_position(media->audio);
		if (frames < media->clock_frames)
			return media->clock_time;
		return media->clock_time + (double)(frames - media->clock_frames) / (double)media->audio->rate;
	}

	/* The time since the anchor. */
	now = media_now_us();
	return media->clock_time + (double)(now - media->clock_us) / 1000000.0;
}

/*
 * Takes the latest picture whose time has come (the older ones are
 * dropped): its reference with its time, or NULL.  next is the time of the
 * picture after it, or -1 when none waits.
 */
struct AVFrame *
vp_media_take(
	struct vp_media *media,
	double clock,
	double *time,
	double *next)
{
	AVFrame *taken;
	unsigned slot;

	/* The latest due. */
	taken = NULL;
	*next = -1.0;
	(void)pthread_mutex_lock(&media->lock);
	while (media->picture_count > 0U) {
		/* Not due yet: the next time. */
		slot = media->picture_first;
		if (media->picture_times[slot] > clock) {
			*next = media->picture_times[slot];
			break;
		}

		/* Due: it replaces the one taken before it. */
		av_frame_free(&taken);
		taken = media->pictures[slot];
		*time = media->picture_times[slot];
		media->pictures[slot] = NULL;
		media->picture_first = (slot + 1U) % VP_PICTURES;
		media->picture_count--;
	}

	/* Room for the reader. */
	(void)pthread_cond_broadcast(&media->wake);
	(void)pthread_mutex_unlock(&media->lock);
	return taken;
}

/* The media thread: reads, decodes and queues until it is told to end. */
static void *
media_run(
	void *argument)
{
	struct media_reader *reader;
	struct vp_media *media;
	int status;
	int stop;

	/* Until told to end. */
	reader = argument;
	media = reader->media;
	for (;;) {
		/* The end, or a seek. */
		stop = media_stopping(media);
		if (stop)
			break;
		status = media_seek(reader);
		if (status != 0)
			continue;

		/* The next packet; the end of the file drains the decoders and waits. */
		status = av_read_frame(reader->format, reader->packet);
		if (status < 0) {
			(void)media_feed(reader, reader->video, NULL);
			if (reader->sound != NULL)
				(void)media_feed(reader, reader->sound, NULL);
			(void)pthread_mutex_lock(&media->lock);
			if (!media->eof)
				vp_log("END reached");
			media->eof = 1;
			(void)pthread_mutex_unlock(&media->lock);
			media_sleep_ms(MEDIA_WAIT_MS * 5);
			continue;
		}

		/* Its decoder. */
		if (reader->packet->stream_index == reader->video_stream)
			(void)media_feed(reader, reader->video, reader->packet);
		else if (reader->sound != NULL && reader->packet->stream_index == reader->sound_stream)
			(void)media_feed(reader, reader->sound, reader->packet);
		av_packet_unref(reader->packet);
	}

	/* The reader goes with the thread. */
	media_reader_close(reader);
	av_free(reader);
	return NULL;
}

/* Opens the file, its decoders and the sound's converter; 0 or an errno value. */
static int
media_reader_open(
	struct media_reader *reader,
	const char *path)
{
	AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
	const char *sound;
	long long length;
	int status;

	/* The container. */
	status = avformat_open_input(&reader->format, path, NULL, NULL);
	if (status < 0)
		return ENOENT;
	status = avformat_find_stream_info(reader->format, NULL);
	if (status < 0)
		return EINVAL;

	/* The video, which a player needs. */
	status = media_decoder(reader->format, AVMEDIA_TYPE_VIDEO, &reader->video, &reader->video_stream);
	if (status != 0)
		return status;
	reader->video_base = av_q2d(reader->format->streams[reader->video_stream]->time_base);

	/* The sound, when there is a stream audiod can play. */
	if (reader->media->audio != NULL && reader->media->audio->created) {
		status = media_decoder(reader->format, AVMEDIA_TYPE_AUDIO, &reader->sound, &reader->sound_stream);
		if (status != 0)
			reader->sound = NULL;
	}

	/* Its converter to 16-bit stereo at audiod's rate. */
	if (reader->sound != NULL) {
		reader->sound_base = av_q2d(reader->format->streams[reader->sound_stream]->time_base);
		status = swr_alloc_set_opts2(&reader->converter, &stereo, AV_SAMPLE_FMT_S16, (int)reader->media->audio->rate,
		    &reader->sound->ch_layout, reader->sound->sample_fmt, reader->sound->sample_rate, 0, NULL);
		if (status == 0)
			status = swr_init(reader->converter);
		if (status < 0) {
			swr_free(&reader->converter);
			avcodec_free_context(&reader->sound);
		}
	}

	/* The packet and the frame worked on. */
	reader->packet = av_packet_alloc();
	reader->frame = av_frame_alloc();
	if (reader->packet == NULL || reader->frame == NULL)
		return ENOMEM;

	/* Succeeded: the log line the tests read. */
	length = -1;
	if (reader->format->duration > 0)
		length = (long long)(reader->format->duration / 1000);
	sound = "none";
	if (reader->sound != NULL)
		sound = reader->sound->codec->name;
	vp_log("OPEN path=%s width=%d height=%d duration_ms=%lld video=%s audio=%s",
	    path, reader->video->width, reader->video->height, length, reader->video->codec->name, sound);
	return 0;
}

/* Closes what the reader opened. */
static void
media_reader_close(
	struct media_reader *reader)
{
	/* Each part, when it was made. */
	av_frame_free(&reader->frame);
	av_packet_free(&reader->packet);
	swr_free(&reader->converter);
	avcodec_free_context(&reader->video);
	avcodec_free_context(&reader->sound);
	avformat_close_input(&reader->format);
}

/* Opens the decoder of a file's best stream of a type; 0, or ENOENT or EINVAL. */
static int
media_decoder(
	AVFormatContext *format,
	enum AVMediaType type,
	AVCodecContext **result,
	int *stream)
{
	const AVCodec *codec;
	AVCodecContext *context;
	int index;
	int status;

	/* The best stream and its decoder. */
	codec = NULL;
	index = av_find_best_stream(format, type, -1, -1, &codec, 0);
	if (index < 0 || codec == NULL)
		return ENOENT;

	/* The decoder, with the stream's parameters, on every processor. */
	context = avcodec_alloc_context3(codec);
	if (context == NULL)
		return ENOMEM;
	status = avcodec_parameters_to_context(context, format->streams[index]->codecpar);
	context->thread_count = 0;
	if (status >= 0)
		status = avcodec_open2(context, codec, NULL);
	if (status < 0) {
		avcodec_free_context(&context);
		return EINVAL;
	}

	/* Succeeded. */
	*result = context;
	*stream = index;
	return 0;
}

/* Gives a decoder a packet (NULL drains it) and handles every frame that comes out; nonzero when a seek or the end interrupted. */
static int
media_feed(
	struct media_reader *reader,
	AVCodecContext *decoder,
	const AVPacket *packet)
{
	int status;
	int again;

	/* The packet (a full decoder still gives its frames). */
	again = AVERROR(EAGAIN);
	status = avcodec_send_packet(decoder, packet);
	if (status < 0 && status != again && status != AVERROR_EOF)
		return 0;

	/* Each frame. */
	for (;;) {
		status = avcodec_receive_frame(decoder, reader->frame);
		if (status < 0)
			return 0;
		if (decoder == reader->video)
			status = media_picture(reader, reader->frame);
		else
			status = media_sound(reader, reader->frame);
		av_frame_unref(reader->frame);
		if (status != 0)
			return status;
	}
}

/* Queues a picture, waiting for room; nonzero when a seek or the end came meanwhile. */
static int
media_picture(
	struct media_reader *reader,
	AVFrame *frame)
{
	struct vp_media *media;
	struct timespec until;
	AVFrame *copy;
	int64_t stamp;
	double time;
	unsigned slot;

	/* Its time; a picture before the time sought is passed over. */
	media = reader->media;
	stamp = frame->best_effort_timestamp;
	if (stamp == AV_NOPTS_VALUE)
		stamp = frame->pts;
	time = 0.0;
	if (stamp != AV_NOPTS_VALUE)
		time = (double)stamp * reader->video_base;
	if (time < reader->skip_before)
		return 0;

	/* A reference of its own. */
	copy = av_frame_clone(frame);
	if (copy == NULL)
		return 0;

	/* Room in the ring, unless a seek or the end comes first. */
	(void)pthread_mutex_lock(&media->lock);
	while (media->picture_count == VP_PICTURES && !media->quit && !media->seek_wanted) {
		(void)clock_gettime(CLOCK_REALTIME, &until);
		until.tv_nsec += MEDIA_WAIT_MS * 1000000L;
		if (until.tv_nsec >= 1000000000L) {
			until.tv_sec++;
			until.tv_nsec -= 1000000000L;
		}

		/* Woken by a picture taken or a request, or after the wait. */
		(void)pthread_cond_timedwait(&media->wake, &media->lock, &until);
	}

	/* Interrupted. */
	if (media->quit || media->seek_wanted) {
		(void)pthread_mutex_unlock(&media->lock);
		av_frame_free(&copy);
		return 1;
	}

	/* Queued. */
	slot = (media->picture_first + media->picture_count) % VP_PICTURES;
	media->pictures[slot] = copy;
	media->picture_times[slot] = time;
	media->picture_count++;
	(void)pthread_mutex_unlock(&media->lock);
	return 0;
}

/* Converts the sound of a frame and writes it, waiting for room; nonzero when a seek or the end came meanwhile. */
static int
media_sound(
	struct media_reader *reader,
	AVFrame *frame)
{
	struct vp_media *media;
	uint8_t *output[1];
	int64_t stamp;
	size_t written;
	size_t done;
	int converted;
	int stop;

	/* Sound before the time sought is passed over. */
	media = reader->media;
	stamp = frame->best_effort_timestamp;
	if (stamp == AV_NOPTS_VALUE)
		stamp = frame->pts;
	if (stamp != AV_NOPTS_VALUE && (double)stamp * reader->sound_base < reader->skip_before)
		return 0;

	/* Converted. */
	output[0] = (uint8_t *)reader->samples;
	converted = swr_convert(reader->converter, output, MEDIA_SOUND_FRAMES, (const uint8_t **)frame->extended_data, frame->nb_samples);
	if (converted <= 0)
		return 0;

	/* Written as room opens. */
	done = 0;
	while (done < (size_t)converted) {
		stop = media_stopping(media);
		if (stop)
			return 1;
		(void)pthread_mutex_lock(&media->lock);
		stop = media->seek_wanted;
		(void)pthread_mutex_unlock(&media->lock);
		if (stop)
			return 1;
		written = vp_audio_write(media->audio, reader->samples + done * 2U, (size_t)converted - done);
		done += written;
		if (done < (size_t)converted)
			media_sleep_ms(MEDIA_WAIT_MS);
	}

	/* Written. */
	return 0;
}

/*
 * Carries out a seek asked for: the file at the time sought, the decoders
 * and the sound emptied, the clock anchored there.  Returns 1 when one was
 * carried out.
 */
static int
media_seek(
	struct media_reader *reader)
{
	struct vp_media *media;
	int64_t target;
	double seconds;
	int wanted;

	/* A seek asked for. */
	media = reader->media;
	(void)pthread_mutex_lock(&media->lock);
	wanted = media->seek_wanted;
	seconds = media->seek_to;
	(void)pthread_mutex_unlock(&media->lock);
	if (!wanted)
		return 0;

	/* The file at the key frame before it, the decoders and the sound emptied. */
	target = (int64_t)(seconds * (double)AV_TIME_BASE);
	(void)avformat_seek_file(reader->format, -1, INT64_MIN, target, target, 0);
	avcodec_flush_buffers(reader->video);
	if (reader->sound != NULL) {
		avcodec_flush_buffers(reader->sound);
		(void)vp_audio_flush(media->audio);
	}

	/* What decodes before the time sought is passed over. */
	reader->skip_before = seconds;

	/* The clock anchored at the time sought; the pictures dropped again (some may have come meanwhile). */
	(void)pthread_mutex_lock(&media->lock);
	media_drop_pictures(media);
	media->clock_time = seconds;
	media->clock_frames = vp_audio_write_position(media->audio);
	media->clock_us = media_now_us();
	media->eof = 0;
	media->seek_wanted = 0;
	(void)pthread_mutex_unlock(&media->lock);
	vp_log("SEEK done to_ms=%lld", (long long)(seconds * 1000.0));

	/* Carried out. */
	return 1;
}

/* Frees the pictures that wait (the lock held). */
static void
media_drop_pictures(
	struct vp_media *media)
{
	unsigned index;

	/* Each. */
	for (index = 0; index < VP_PICTURES; index++)
		av_frame_free(&media->pictures[index]);
	media->picture_first = 0;
	media->picture_count = 0;
}

/* Tells whether the thread is to end. */
static int
media_stopping(
	struct vp_media *media)
{
	int quit;

	/* Read under the lock. */
	(void)pthread_mutex_lock(&media->lock);
	quit = media->quit;
	(void)pthread_mutex_unlock(&media->lock);
	return quit;
}

/* The monotonic clock in microseconds. */
static uint64_t
media_now_us(void)
{
	struct timespec now;

	/* The clock. */
	(void)clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000000U + (uint64_t)now.tv_nsec / 1000U;
}

/* Sleeps a number of milliseconds. */
static void
media_sleep_ms(
	unsigned ms)
{
	struct timespec wait;

	/* The wait. */
	wait.tv_sec = ms / 1000U;
	wait.tv_nsec = (long)(ms % 1000U) * 1000000L;
	(void)nanosleep(&wait, NULL);
}
