/* Integration check for the restricted shared libraries. Not part of the runtime package. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <vorbis/vorbisfile.h>

static void check(int result, const char *operation)
{
    if (result < 0) {
        char message[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(result, message, sizeof(message));
        fprintf(stderr, "%s: %s\n", operation, message);
        exit(1);
    }
}

static void require(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "%s\n", message);
        exit(1);
    }
}

static void inventory(void)
{
    void *cursor = NULL;
    const AVCodec *codec;
    const AVInputFormat *input;
    const AVOutputFormat *output;
    int count = 0;
    while ((codec = av_codec_iterate(&cursor))) {
        require((av_codec_is_decoder(codec) && !strcmp(codec->name, "wmav2")) ||
                (av_codec_is_encoder(codec) && !strcmp(codec->name, "libvorbis")), "Unexpected codec");
        count++;
    }
    require(count == 2, "Expected exactly WMA v2 decoder and libvorbis encoder");
    cursor = NULL;
    count = 0;
    while ((input = av_demuxer_iterate(&cursor))) {
        require(!strcmp(input->name, "asf"), "Unexpected demuxer");
        count++;
    }
    require(count == 1, "Expected exactly ASF demuxer");
    cursor = NULL;
    count = 0;
    while ((output = av_muxer_iterate(&cursor))) {
        require(!strcmp(output->name, "ogg"), "Unexpected muxer");
        count++;
    }
    require(count == 1, "Expected exactly Ogg muxer");
    for (int direction = 0; direction < 2; direction++) {
        const char *protocol;
        cursor = NULL;
        count = 0;
        while ((protocol = avio_enum_protocols(&cursor, direction))) {
            require(!strcmp(protocol, "file"), "Unexpected I/O protocol");
            count++;
        }
        require(count == 1, "Expected exactly file protocol");
    }
}

typedef struct Conversion {
    AVCodecContext *decoder, *encoder;
    AVFormatContext *output;
    AVStream *stream;
    AVAudioFifo *fifo;
    AVPacket *packet;
    int64_t samples;
    float *reference;
    size_t reference_size;
    size_t reference_capacity;
} Conversion;

static void packets(Conversion *c)
{
    int result;
    while ((result = avcodec_receive_packet(c->encoder, c->packet)) >= 0) {
        av_packet_rescale_ts(c->packet, c->encoder->time_base, c->stream->time_base);
        c->packet->stream_index = c->stream->index;
        check(av_interleaved_write_frame(c->output, c->packet), "write Ogg packet");
        av_packet_unref(c->packet);
    }
    require(result == AVERROR(EAGAIN) || result == AVERROR_EOF, "Vorbis packet encoding failed");
}

static void encode(Conversion *c, int final)
{
    int frame_size = c->encoder->frame_size;
    require(frame_size > 0, "Expected fixed encoder input frame size");
    while (av_audio_fifo_size(c->fifo) >= frame_size || (final && av_audio_fifo_size(c->fifo))) {
        AVFrame *frame = av_frame_alloc();
        require(frame != NULL, "allocate encoder frame");
        frame->nb_samples = final && av_audio_fifo_size(c->fifo) < frame_size ? av_audio_fifo_size(c->fifo) : frame_size;
        frame->format = c->encoder->sample_fmt;
        frame->sample_rate = c->encoder->sample_rate;
        check(av_channel_layout_copy(&frame->ch_layout, &c->encoder->ch_layout), "copy channel layout");
        check(av_frame_get_buffer(frame, 0), "allocate sample buffer");
        require(av_audio_fifo_read(c->fifo, (void **)frame->data, frame->nb_samples) == frame->nb_samples, "read audio FIFO");
        /* Source timestamps are discontinuous in some original tracks. Count decoded samples. */
        frame->pts = c->samples;
        c->samples += frame->nb_samples;
        check(avcodec_send_frame(c->encoder, frame), "encode Vorbis frame");
        av_frame_free(&frame);
        packets(c);
    }
}

static void decoded(Conversion *c)
{
    AVFrame *frame = av_frame_alloc();
    int result;
    require(frame != NULL, "allocate decoder frame");
    while ((result = avcodec_receive_frame(c->decoder, frame)) >= 0) {
        require(frame->format == AV_SAMPLE_FMT_FLTP && frame->sample_rate == 44100 &&
                frame->ch_layout.nb_channels == 2 && !frame->decode_error_flags, "Unexpected or damaged decoded audio");
        size_t needed = c->reference_size + (size_t)frame->nb_samples * 2;
        if (needed > c->reference_capacity) {
            size_t capacity = needed * 2;
            float *reference = realloc(c->reference, capacity * sizeof(float));
            require(reference != NULL, "allocate reference PCM");
            c->reference = reference;
            c->reference_capacity = capacity;
        }
        for (int i = 0; i < frame->nb_samples; i++)
            for (int channel = 0; channel < 2; channel++)
                c->reference[c->reference_size++] = ((float *)frame->extended_data[channel])[i];
        require(av_audio_fifo_write(c->fifo, (void **)frame->extended_data, frame->nb_samples) == frame->nb_samples, "buffer decoded audio");
        encode(c, 0);
        av_frame_unref(frame);
    }
    require(result == AVERROR(EAGAIN) || result == AVERROR_EOF, "WMA decoding failed");
    av_frame_free(&frame);
}

static void verify_output(const char *path, Conversion *c)
{
    OggVorbis_File file;
    FILE *input = fopen(path, "rb");
    require(input != NULL, "open generated Ogg");
    require(ov_open_callbacks(input, &file, NULL, 0, OV_CALLBACKS_DEFAULT) == 0, "decode generated Ogg using libvorbisfile");
    vorbis_info *info = ov_info(&file, -1);
    require(info && info->channels == 2 && info->rate == 44100, "Vorbis stream properties changed");
    require(ov_pcm_total(&file, -1) == c->samples, "Vorbis sample count changed");
    size_t position = 0;
    double signal = 0, error = 0;
    long count;
    float **pcm;
    int section;
    while ((count = ov_read_float(&file, &pcm, 4096, &section)) > 0) {
        for (long i = 0; i < count; i++) {
            for (int channel = 0; channel < 2; channel++) {
                require(position < c->reference_size, "Extra Vorbis samples");
                double original = c->reference[position++];
                double difference = original - pcm[channel][i];
                require(isfinite(pcm[channel][i]), "Non-finite Vorbis sample");
                signal += original * original;
                error += difference * difference;
            }
        }
    }
    require(count == 0 && position == c->reference_size, "Incomplete Vorbis decode");
    require(signal > 0 && error / signal < 0.01, "Vorbis output differs excessively from decoded WMA");
    printf("SAN9_AUDIO_OK samples=%lld rate=44100 channels=2 snr=%.2f dB\n", (long long)c->samples,
           error > 0 ? 10 * log10(signal / error) : 999.0);
    ov_clear(&file);
}

int main(int argc, char **argv)
{
    AVFormatContext *input = NULL;
    Conversion c = {0};
    AVPacket *packet = av_packet_alloc();
    int result;
    require(argc == 3 && packet != NULL, "Usage: verify-audio INPUT.wma OUTPUT.ogg");
    inventory();
    check(avformat_open_input(&input, argv[1], NULL, NULL), "open WMA");
    check(avformat_find_stream_info(input, NULL), "read WMA stream");
    const AVCodec *decoder;
    int index = av_find_best_stream(input, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
    check(index, "find WMA audio");
    require(decoder->id == AV_CODEC_ID_WMAV2, "Expected WMA v2 input");
    c.decoder = avcodec_alloc_context3(decoder);
    require(c.decoder != NULL, "allocate WMA decoder");
    check(avcodec_parameters_to_context(c.decoder, input->streams[index]->codecpar), "read codec parameters");
    c.decoder->err_recognition = AV_EF_CAREFUL | AV_EF_EXPLODE;
    check(avcodec_open2(c.decoder, decoder, NULL), "open WMA decoder");
    require(c.decoder->sample_rate == 44100 && c.decoder->ch_layout.nb_channels == 2, "Expected 44.1 kHz stereo source");
    check(avformat_alloc_output_context2(&c.output, NULL, "ogg", argv[2]), "create Ogg output");
    const AVCodec *encoder = avcodec_find_encoder_by_name("libvorbis");
    require(encoder != NULL, "find Vorbis encoder");
    c.encoder = avcodec_alloc_context3(encoder);
    require(c.encoder != NULL, "allocate Vorbis encoder");
    c.encoder->sample_fmt = AV_SAMPLE_FMT_FLTP;
    c.encoder->sample_rate = 44100;
    av_channel_layout_default(&c.encoder->ch_layout, 2);
    c.encoder->time_base = (AVRational){1, 44100};
    c.encoder->flags |= AV_CODEC_FLAG_QSCALE;
    c.encoder->global_quality = 5 * FF_QP2LAMBDA;
    if (c.output->oformat->flags & AVFMT_GLOBALHEADER)
        c.encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    check(avcodec_open2(c.encoder, encoder, NULL), "open Vorbis encoder");
    c.stream = avformat_new_stream(c.output, NULL);
    require(c.stream != NULL, "create Ogg stream");
    c.stream->time_base = c.encoder->time_base;
    check(avcodec_parameters_from_context(c.stream->codecpar, c.encoder), "write Vorbis parameters");
    check(avio_open(&c.output->pb, argv[2], AVIO_FLAG_WRITE), "open Ogg file");
    check(avformat_write_header(c.output, NULL), "write Ogg header");
    c.fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLTP, 2, 1);
    c.packet = av_packet_alloc();
    require(c.fifo != NULL && c.packet != NULL, "allocate conversion buffers");
    while ((result = av_read_frame(input, packet)) >= 0) {
        if (packet->stream_index == index) {
            require(!(packet->flags & AV_PKT_FLAG_CORRUPT), "Corrupt source packet");
            check(avcodec_send_packet(c.decoder, packet), "decode WMA packet");
            decoded(&c);
        }
        av_packet_unref(packet);
    }
    require(result == AVERROR_EOF, "Could not finish reading source");
    check(avcodec_send_packet(c.decoder, NULL), "flush WMA decoder");
    decoded(&c);
    encode(&c, 1);
    check(avcodec_send_frame(c.encoder, NULL), "flush Vorbis encoder");
    packets(&c);
    check(av_write_trailer(c.output), "finish Ogg output");
    check(avio_closep(&c.output->pb), "close Ogg output");
    verify_output(argv[2], &c);
    free(c.reference);
    av_audio_fifo_free(c.fifo);
    av_packet_free(&c.packet);
    av_packet_free(&packet);
    avcodec_free_context(&c.decoder);
    avcodec_free_context(&c.encoder);
    avformat_free_context(c.output);
    avformat_close_input(&input);
    return 0;
}
