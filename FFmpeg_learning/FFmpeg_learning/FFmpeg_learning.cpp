// FFmpeg_audio.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>

#include <stdlib.h>
#include <string.h>

/*
#define __STDC_CONSTANT_MACROS
extern "C" { FILE __iob_func[3] = { *stdin,*stdout,*stderr }; }
*/

//Windows
extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswresample/swresample.h"
#include "libavutil/channel_layout.h"
#include "SDL2/SDL.h"
};


#define MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48khz 32bit audio

//音频解码
//音频重采样
int main(int argc, char* argv[])
{
    FILE* pFile;
    char url[] = "skycity1.mp3";

    AVFormatContext* pFormatCtx;
    unsigned int	i;
    int audioStream;
    AVCodecContext* pCodecCtx;
    AVCodec* pCodec;
    AVPacket* packet;
    uint8_t* out_buffer;
    AVFrame* pFrame;
    int64_t in_channel_layout;
    struct SwrContext* au_convert_ctx;
    int ret = -1;
    int got_picture = -1;
    int index = 0;
    AVCodecParameters* pCodecpar;

    if (fopen_s(&pFile, "output.pcm", "wb") != 0) {
        printf("Couldn't open output.pcm.\n");
        return -1;
    }


    //av_register_all();弃用
    avformat_network_init();
    pFormatCtx = avformat_alloc_context();

    //Open
    if (avformat_open_input(&pFormatCtx, url, NULL, NULL) != 0) {
        printf("Couldn't open input stream.\n");
        return -1;
    }

    //Retrieve stream information
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        printf("Couldn't find stream information.\n");
        return -1;
    }

    //Dump valid information onto standard error
    av_dump_format(pFormatCtx, 0, url, false);

    //查找音频流
    audioStream = -1;
    for (i = 0; i < pFormatCtx->nb_streams; i++) {
        if ((pFormatCtx->streams[i]->codecpar->codec_type) == AVMEDIA_TYPE_AUDIO) {
            //找到音频流的index
            audioStream = i;
        }
    }

    if (audioStream == -1) {
        printf("Didn't find a audio stream.\n");
        return -1;
    }


    pCodecCtx = avcodec_alloc_context3(NULL);
    if (!pCodecCtx) {
        printf("pCodecCtx alloc erro.\n");
        return -1;
    }


    pCodecpar = pFormatCtx->streams[audioStream]->codecpar;

    //Find the decoder for the audio stream
    pCodec = avcodec_find_decoder(pCodecpar->codec_id);
    if (pCodec == NULL) {
        printf("Codec not found.\n");
        return -1;
    }


    ret = avcodec_parameters_to_context(pCodecCtx, pCodecpar);
    if (ret < 0) {
        printf("avcodec_parameters_to_context erro.\n");
        return -1;
    }

    //Open codec
    if (avcodec_open2(pCodecCtx, pCodec, NULL)) {
        printf("Could not open codec.\n");
        return -1;
    }

    packet = (AVPacket*)av_packet_alloc();
    if (!packet) {
        av_packet_unref(packet);
        return -1;
    }

    //Out Audio Param 音频重采样
    uint64_t out_channel_layout = AV_CH_LAYOUT_STEREO;
    //nb_samples: AAC-1024 MP3-1152
    int out_nb_samples = pCodecCtx->frame_size;
    AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;
    int out_sample_rate = 44100;
    int out_channels = av_get_channel_layout_nb_channels(out_channel_layout);
    //Out Buffer Size
    int out_buffer_size = av_samples_get_buffer_size(NULL, out_channels, out_nb_samples, out_sample_fmt, 1);

    out_buffer = (uint8_t*)av_malloc(MAX_AUDIO_FRAME_SIZE * 2);
    pFrame = av_frame_alloc();

    in_channel_layout = av_get_default_channel_layout(pCodecCtx->channels);
    au_convert_ctx = swr_alloc();
    au_convert_ctx = swr_alloc_set_opts(au_convert_ctx, out_channel_layout, out_sample_fmt, out_sample_rate, in_channel_layout,
        pCodecCtx->sample_fmt, pCodecCtx->sample_rate, 0, NULL);
    swr_init(au_convert_ctx);

    while (av_read_frame(pFormatCtx, packet) >= 0) {
        if (packet->stream_index == audioStream) {
            //ret = avcodec_decode_audio4(pCodecCtx, pFrame, &got_picture, packet);
            ret = avcodec_send_packet(pCodecCtx, packet);
            if (ret < 0) {
                printf("Error in send decode audio packet.\n");
                continue;
                //return -1;
            }

            ret = avcodec_receive_frame(pCodecCtx, pFrame);
            got_picture = ret;
            if (ret != 0)
            {
                printf("Error in decoding audio frame.\n");
                //cout << "*" << pack.size << flush;
            }

            if (got_picture == 0) {
                swr_convert(au_convert_ctx, &out_buffer, MAX_AUDIO_FRAME_SIZE, (const uint8_t**)pFrame->data, pFrame->nb_samples);
                printf("index:%5d\t pts:%lld\t packet size:%d\n", index, packet->pts, packet->size);

                //Write PCM
                fwrite(out_buffer, 1, out_buffer_size, pFile);
                index++;
            }
        }
        av_packet_unref(packet);
    }

    swr_free(&au_convert_ctx);
    fclose(pFile);
    av_free(out_buffer);

    av_packet_free(&packet);
    //Close the codec
    avcodec_close(pCodecCtx);
    //Close the video file
    avformat_close_input(&pFormatCtx);

    getchar();


    std::cout << "Hello World!\n";

    return 0;
}
