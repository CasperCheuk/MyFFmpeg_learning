// FFmpeg_audio.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <chrono>
#include <windows.h>
#include <stdio.h>
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

#include "libavutil/opt.h"
};
using namespace std;
using namespace std::chrono;

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
            SDL_Delay(13);
        }
    }
    thread_exit = 0;
    thread_pause = 0;
    //Break
    SDL_Event event;
    event.type = SFM_BREAK_EVENT;
    SDL_PushEvent(&event);

    return 0;
}



int print_time(void)
{
    SYSTEMTIME sys; //typedef struct _SYSTEMTIME {
      //WORD wYear;
      //WORD wMonth;
      //WORD wDayOfWeek;
      //WORD wDay;
     // WORD wHour;
      //WORD wMinute;
      //WORD wSecond;
      //WORD wMilliseconds;
    // } SYSTEMTIME, *PSYSTEMTIME;
    GetLocalTime(&sys); // The GetLocalTime function retrieves the current local date and time
    printf("%d%d%d %d:%d:%d.%d 星期%d\n", sys.wYear, sys.wMonth, sys.wDay, sys.wHour, sys.wMinute, sys.wSecond, sys.wMilliseconds, sys.wDayOfWeek);
    return 0;
}


int flush_encoder(AVFormatContext* fmt_ctx, unsigned int stream_index) {
    int ret;
    int got_frame;
    AVPacket enc_pkt;
    if (!(fmt_ctx->streams[stream_index]->codec->codec->capabilities &
        AV_CODEC_CAP_DELAY))
        return 0;
    while (1) {
        enc_pkt.data = NULL;
        enc_pkt.size = 0;
        av_init_packet(&enc_pkt);
        ret = avcodec_encode_video2(fmt_ctx->streams[stream_index]->codec, &enc_pkt,
            NULL, &got_frame);
        av_frame_free(NULL);
        if (ret < 0)
            break;
        if (!got_frame) {
            ret = 0;
            break;
        }
        printf("Flush Encoder: Succeed to encode 1 frame!\tsize:%5d\n", enc_pkt.size);
        /* mux encoded frame */
        ret = av_write_frame(fmt_ctx, &enc_pkt);
        if (ret < 0)
            break;
    }
    return ret;
}



//视频解码
int main(int argc, char* argv[])
{
    FILE* pFile;
    //char url[] = "sintel.h264";
    //char url[] = "bigbuckbunny_480x272.h265";
    //char url[] = "sintel.ts";
    char url[] = "diaosinanshi.mov";
    const char* out_file = "out_sintel.h265";


    AVFormatContext* pInFormatCtx;
    int audioStream;
    int videoStream;
    AVCodecContext* pCodecCtx;
    AVCodec* pInCodec;
    AVPacket* packet;
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

    //---------------------------Encoding-----------------------------------------
    AVStream* video_st;
    uint8_t* out_buffer;
    AVCodec* pOutCodec;
    AVOutputFormat* out_fmt;
    AVFormatContext* pOutFormatCtx;
    AVFrame* pOutFrame;

    AVCodecContext* pOutCodecCtx;
    int out_picture_size;
    int y_out_size;
    int out_framecnt = 0;

    uint8_t* out_picture_buf;
    AVPacket *out_pkt;
    //--------------------------------------------------------------------

    avformat_network_init();
    pInFormatCtx = avformat_alloc_context();
    pOutFormatCtx = avformat_alloc_context();


    //Method1 方法1.组合使用几个函数
    //Guess Format 猜格式
    out_fmt = av_guess_format(NULL, out_file, NULL);
    pOutFormatCtx->oformat = out_fmt;

    //Open
    if (avformat_open_input(&pInFormatCtx, url, NULL, NULL) != 0) {
        printf("Couldn't open input stream.\n");
        return -1;
    }

    //Open output URL
    if (avio_open(&pOutFormatCtx->pb, out_file, AVIO_FLAG_READ_WRITE) < 0) {
        printf("Failed to open output file! \n");
        return -1;
    }

    //Retrieve stream information
    if (avformat_find_stream_info(pInFormatCtx, NULL) < 0) {
        printf("Couldn't find stream information.\n");
        return -1;
    }

    //Dump valid information onto standard error
    //Output Info-----------------------------
    printf("---------------- File Information ---------------\n");
    av_dump_format(pInFormatCtx, 0, url, false);
    printf("-------------------------------------------------\n");



    //---------------------------Encoding-----------------------------------------
    video_st = avformat_new_stream(pOutFormatCtx, 0);
    video_st->time_base.num = 1;
    video_st->time_base.den = 25;

    if (video_st == NULL) {
        return -1;
    }

    //----------------------------------------------------------------------------

   


    //查找流
    videoStream = -1;
    for (i = 0; i < pInFormatCtx->nb_streams; i++) {
        if ((pInFormatCtx->streams[i]->codecpar->codec_type) == AVMEDIA_TYPE_VIDEO) {
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


    pCodecpar = pInFormatCtx->streams[videoStream]->codecpar;

    //Find the decoder for the video stream
    pInCodec = avcodec_find_decoder(pCodecpar->codec_id);
    if (pInCodec == NULL) {
        printf("Codec not found.\n");
        return -1;
    }


    ret = avcodec_parameters_to_context(pCodecCtx, pCodecpar);
    if (ret < 0) {
        printf("avcodec_parameters_to_context erro.\n");
        return -1;
    }

    //Open codec
    if (avcodec_open2(pCodecCtx, pInCodec, NULL)) {
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
        screen_w, screen_h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);



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




    //---------------------------Encoding-----------------------------------------
    //  

    //Param that must set
    pOutCodecCtx = video_st->codec;
    //pOutCodecCtx->codec_id =AV_CODEC_ID_HEVC;
    pOutCodecCtx->codec_id = out_fmt->video_codec;
    pOutCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
    pOutCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    pOutCodecCtx->width = pCodecCtx->width;
    pOutCodecCtx->height = pCodecCtx->height;
    pOutCodecCtx->time_base.num = 1;
    pOutCodecCtx->time_base.den = 25;
    pOutCodecCtx->bit_rate = 400000;
    pOutCodecCtx->gop_size = 250;
    //H264
    //pOutCodecCtx->me_range = 16;
    //pOutCodecCtx->max_qdiff = 4;
    //pCodecCtx->qcompress = 0.6;
    pOutCodecCtx->qmin = 10;
    pOutCodecCtx->qmax = 51;
    //Optional Param
    pOutCodecCtx->max_b_frames = 3;

    // Set Option
    AVDictionary* out_param = 0;
    //H.264
    if (pOutCodecCtx->codec_id == AV_CODEC_ID_H264) {
        av_dict_set(&out_param, "preset", "slow", 0);
        av_dict_set(&out_param, "tune", "zerolatency", 0);
        //av_dict_set(&param, "profile", "main", 0);
    }
    //H.265
    if (pOutCodecCtx->codec_id == AV_CODEC_ID_H265) {
        av_dict_set(&out_param, "preset", "ultrafast", 0);
        av_dict_set(&out_param, "tune", "zero-latency", 0);
    }
    //Show some Information
    av_dump_format(pOutFormatCtx, 0, out_file, 1);



    pOutCodec = avcodec_find_encoder(pOutCodecCtx->codec_id);
    if (!pOutCodec) {
        printf("Can not find encoder! \n");
        return -1;
    }
    if (avcodec_open2(pOutCodecCtx, pOutCodec, &out_param) < 0) {
        printf("Failed to open encoder! \n");
        return -1;
    }


    pOutFrame = av_frame_alloc();
    out_picture_size = avpicture_get_size(pOutCodecCtx->pix_fmt, pOutCodecCtx->width, pOutCodecCtx->height);
    out_picture_buf = (uint8_t*)av_malloc(out_picture_size);
    avpicture_fill((AVPicture*)pFrame, out_picture_buf, pOutCodecCtx->pix_fmt, pOutCodecCtx->width, pOutCodecCtx->height);

    //Write File Header
    avformat_write_header(pOutFormatCtx, NULL);

    av_new_packet(out_pkt, out_picture_size);

    y_out_size = pOutCodecCtx->width * pOutCodecCtx->height;


    //--------------------------------------------------------------------

    video_tid = SDL_CreateThread(sfp_refresh_thread, NULL, NULL);
    //------------SDL End------------
    //Event Loop
    for (;;) {
        //Wait
        SDL_WaitEvent(&event);
        if (event.type == SFM_REFRESH_EVENT) {
           //printf("SFM_REFRESH_EVENT .\n");
            //------------------------------
            if (av_read_frame(pInFormatCtx, packet) >= 0) {
                //printf("av_read_frame .\n");
                if (packet->stream_index == videoStream) {
                    print_time();
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

                        /*sdlRect.x = 0;
                        sdlRect.y = 0;
                        sdlRect.w = screen_w;
                        sdlRect.h = screen_h;*/

                        sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);
                        //SDL---------------------------
                        SDL_UpdateTexture(sdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0]);
                        SDL_RenderClear(sdlRenderer);
                        SDL_RenderCopy( sdlRenderer, sdlTexture, &sdlRect, &sdlRect );  
                        //SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
                        SDL_RenderPresent(sdlRenderer);
                        //SDL End-----------------------



    //---------------------------Encoding-----------------------------------------
                        //Encode
                        pOutFrame->data[0] = pFrameYUV->data[0];   // Y
                        pOutFrame->data[1] = pFrameYUV->data[1];   // U 
                        pOutFrame->data[2] = pFrameYUV->data[2];   // V
                        //PTS
                        pOutFrame->pts = pFrameYUV->pts;
                        int out_got_picture = 0;

                        int ret = avcodec_encode_video2(pOutCodecCtx, out_pkt, pOutFrame, &out_got_picture);
                        //TODO
                        //ret = avcodec_send_packet(pCodecCtx, packet);
                        //ret = avcodec_receive_frame(pCodecCtx, pFrame);
                        if (ret < 0) {
                            printf("Failed to encode! \n");
                            return -1;
                        }
                        if (got_picture == 1) {
                            printf("Succeed to encode frame: %5d\tsize:%5d\n", out_framecnt, out_pkt->size);
                            out_framecnt++;
                            out_pkt->stream_index = video_st->index;
                            ret = av_write_frame(pOutFormatCtx, out_pkt);
                            av_packet_unref(out_pkt);
                        }
    //--------------------------------------------------------------------
                    }
                }

                av_packet_unref(packet);
             }else {
                //Exit Thread
                thread_exit = 1;
            }
        }
        else if (event.type == SDL_WINDOWEVENT) {
            //printf("SDL_WINDOWEVENT .\n");
            //If Resize
            //SDL_GetWindowSize(screen, &screen_w, &screen_h);
        }
        else if (event.type == SDL_KEYDOWN) {
            //printf("SDL_KEYDOWN .\n");
            //Pause
            if (event.key.keysym.sym == SDLK_SPACE)
                thread_pause = !thread_pause;
        }
        else if (event.type == SDL_QUIT) {
            //printf("SDL_QUIT .\n");
            thread_exit = 1;
        }
        else if (event.type == SFM_BREAK_EVENT) {
            //printf("SFM_BREAK_EVENT .\n");
            break;
        }

    }

    //---------------------------Encoding-----------------------------------------
    //Flush Encoder
    int out_ret = flush_encoder(pOutFormatCtx, 0);
    if (out_ret < 0) {
        printf("Flushing encoder failed\n");
        return -1;
    }


    //Write file trailer
    av_write_trailer(pOutFormatCtx);


    avformat_close_input(&pOutFormatCtx);
    av_packet_free(&out_pkt);


    if (video_st) {
        avcodec_close(video_st->codec);
        av_frame_free(&pOutFrame);
        av_free(out_picture_buf);

    }

    avio_close(pOutFormatCtx->pb);
    //--------------------------------------------------------------------


    sws_freeContext(img_convert_ctx);

    SDL_Quit();
    //--------------
    av_frame_free(&pFrameYUV);
    av_frame_free(&pFrame);
    avcodec_close(pCodecCtx);
    avformat_close_input(&pInFormatCtx);


    //swr_free(&au_convert_ctx);
    //fclose(pFile);
    //av_free(out_buffer);

    av_packet_free(&packet);
    //Close the codec
    //avcodec_close(pCodecCtx);
    //Close the video file
    //avformat_close_input(&pInFormatCtx);

    getchar();


    std::cout << "Hello World!\n";

    return 0;
}
