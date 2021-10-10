﻿// FFmpeg_audio.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
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
#include "libavutil/imgutils.h"
#include "libswscale/swscale.h"
#include "SDL2/SDL.h"
};


#define MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48khz 32bit audio


//Refresh Event
#define SFM_REFRESH_EVENT  (SDL_USEREVENT + 1)

#define SFM_BREAK_EVENT  (SDL_USEREVENT + 2)

int thread_exit = 0;
int thread_pause = 0;

int sfp_refresh_thread(void* opaque) {
    thread_exit = 0;
    thread_pause = 0;

    while (!thread_exit) {
        if (!thread_pause) {
            SDL_Event event;
            event.type = SFM_REFRESH_EVENT;
            SDL_PushEvent(&event);
        }
        SDL_Delay(40);
    }
    thread_exit = 0;
    thread_pause = 0;
    //Break
    SDL_Event event;
    event.type = SFM_BREAK_EVENT;
    SDL_PushEvent(&event);

    return 0;
}

//视频解码
int main(int argc, char* argv[])
{
    FILE* pFile;
    char url[] = "sintel.h264";
    //char url[] = "bigbuckbunny_480x272.h265";

    AVFormatContext* pFormatCtx;
    int audioStream;
    int videoStream;
    AVCodecContext* pCodecCtx;
    AVCodec* pCodec;
    AVPacket* packet;
    uint8_t* out_buffer;
    //AVFrame* pFrame;
    int64_t in_channel_layout;
    struct SwrContext* au_convert_ctx;
    int ret = -1;
    int got_picture = -1;
    int index = 0;
    AVCodecParameters* pCodecpar;


    AVFrame* pFrame, * pFrameYUV;

    struct SwsContext* img_convert_ctx;
    //------------SDL----------------
    int screen_w, screen_h;
    SDL_Window* screen;
    SDL_Renderer* sdlRenderer;
    SDL_Texture* sdlTexture;
    SDL_Rect sdlRect;
    SDL_Thread* video_tid;
    SDL_Event event;

    int	i, videoindex;



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
    //Output Info-----------------------------
    printf("---------------- File Information ---------------\n");
    av_dump_format(pFormatCtx, 0, url, false);
    printf("-------------------------------------------------\n");

    //查找流
    videoStream = -1;
    for (i = 0; i < pFormatCtx->nb_streams; i++) {
        if ((pFormatCtx->streams[i]->codecpar->codec_type) == AVMEDIA_TYPE_VIDEO) {
            //找到视频流的index
            videoStream = i;
        }
    }

    //if (audioStream == -1) {
    //    printf("Didn't find a audio stream.\n");
    //    return -1;
    //}

    if (videoStream == -1) {
        printf("Didn't find a video stream.\n");
        return -1;
    }


    pCodecCtx = avcodec_alloc_context3(NULL);
    if (!pCodecCtx) {
        printf("pCodecCtx alloc erro.\n");
        return -1;
    }


    pCodecpar = pFormatCtx->streams[videoStream]->codecpar;

    //Find the decoder for the video stream
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



    pFrame = av_frame_alloc();
    pFrameYUV = av_frame_alloc();

    out_buffer = (uint8_t*)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height,1));
    av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize,out_buffer, AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height,1);

    img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
        pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);


    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        printf("Could not initialize SDL - %s\n", SDL_GetError());
        return -1;
    }

    //SDL 2.0 Support for multiple windows
    screen_w = pCodecCtx->width;
    screen_h = pCodecCtx->height;
    screen = SDL_CreateWindow("Simplest ffmpeg player's Window", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        screen_w, screen_h, SDL_WINDOW_OPENGL);



    if (!screen) {
        printf("SDL: could not create window - exiting:%s\n", SDL_GetError());
        return -1;
    }
    sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
    //IYUV: Y + U + V  (3 planes)
    //YV12: Y + V + U  (3 planes)
    sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pCodecCtx->width, pCodecCtx->height);

    sdlRect.x = 0;
    sdlRect.y = 0;
    sdlRect.w = screen_w;
    sdlRect.h = screen_h;

    packet = (AVPacket*)av_malloc(sizeof(AVPacket));

    video_tid = SDL_CreateThread(sfp_refresh_thread, NULL, NULL);
    //------------SDL End------------
    //Event Loop
    for (;;) {
        //Wait
        SDL_WaitEvent(&event);
        if (event.type == SFM_REFRESH_EVENT) {
           // printf("SFM_REFRESH_EVENT .\n");
            //------------------------------
            if (av_read_frame(pFormatCtx, packet) >= 0) {
                //printf("av_read_frame .\n");
                if (packet->stream_index == videoStream) {
                   // printf("debug 1 .\n");
                    //ret = avcodec_decode_audio4(pCodecCtx, pFrame, &got_picture, packet);
                    ret = avcodec_send_packet(pCodecCtx, packet);
                    if (ret < 0) {
                        printf("Error in send decode audio packet.\n");
                        continue;
                        //return -1;
                    }

                    ret = avcodec_receive_frame(pCodecCtx, pFrame);
                    //printf("debug 2 .\n");
                    got_picture = ret;
                    if (ret != 0)
                    {
                        printf("Error in decoding audio frame.\n");
                        //cout << "*" << pack.size << flush;
                    }
                    if (got_picture == 0) {
                        sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);
                        //SDL---------------------------
                        SDL_UpdateTexture(sdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0]);
                        SDL_RenderClear(sdlRenderer);
                        //SDL_RenderCopy( sdlRenderer, sdlTexture, &sdlRect, &sdlRect );  
                        SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
                        SDL_RenderPresent(sdlRenderer);
                        //SDL End-----------------------
                    }
                }

                av_packet_unref(packet);
             }else {
                //Exit Thread
                thread_exit = 1;
            }
        }
        else if (event.type == SDL_KEYDOWN) {
            //Pause
            if (event.key.keysym.sym == SDLK_SPACE)
                thread_pause = !thread_pause;
        }
        else if (event.type == SDL_QUIT) {
            thread_exit = 1;
        }
        else if (event.type == SFM_BREAK_EVENT) {
            break;
        }

    }




    sws_freeContext(img_convert_ctx);

    SDL_Quit();
    //--------------
    av_frame_free(&pFrameYUV);
    av_frame_free(&pFrame);
    avcodec_close(pCodecCtx);
    avformat_close_input(&pFormatCtx);


    //swr_free(&au_convert_ctx);
    //fclose(pFile);
    //av_free(out_buffer);

    av_packet_free(&packet);
    //Close the codec
    //avcodec_close(pCodecCtx);
    //Close the video file
    //avformat_close_input(&pFormatCtx);

    getchar();


    std::cout << "Hello World!\n";

    return 0;
}
