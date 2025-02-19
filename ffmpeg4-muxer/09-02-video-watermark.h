﻿#include <stdio.h>
#pragma warning(disable:4996)
#pragma warning(disable:4703)

extern "C"
{
#include "../ffmpeg-4.2.1/include/libavcodec/avcodec.h"
#include "../ffmpeg-4.2.1/include/libavformat/avformat.h"
//#include "../ffmpeg-4.2.1/include/libavfilter/avfiltergraph.h"
#include "../ffmpeg-4.2.1/include/libavfilter/avfilter.h"
#include "../ffmpeg-4.2.1/include/libavfilter/buffersink.h"
#include "../ffmpeg-4.2.1/include/libavfilter/buffersrc.h"
#include "../ffmpeg-4.2.1/include/libavutil/opt.h"
#include "../ffmpeg-4.2.1/include/libavutil/imgutils.h"
}

#pragma comment(lib, "../ffmpeg-4.2.1/lib/avformat.lib")
#pragma comment(lib, "../ffmpeg-4.2.1/lib/avcodec.lib")
#pragma comment(lib, "../ffmpeg-4.2.1/lib/avfilter.lib")
//#pragma comment(lib, "../ffmpeg-4.2.1/lib/avutil.lib")
//#pragma comment(lib, "../ffmpeg-4.2.1/lib/swresample.lib")
//#pragma comment(lib, "../ffmpeg-4.2.1/lib/swscale.lib")

//转换之后播放文件：ffplay -f rawvideo -pixel_format yuv420p -video_size 1280x720 -framerate 30 out_crop_vfilter.yuv
int filterYuv(int argc, char** argv)
{
	int ret = 0;

	// input yuv
	FILE* inFile = NULL;
	const char* inFileName = "..\\res\\yuv420p_1280x720.yuv";
	fopen_s(&inFile, inFileName, "rb+");
	if (!inFile) {
		printf("Fail to open file\n");
		return -1;
	}

	int in_width = 1280;
	int in_height = 720;

	// output yuv
	FILE* outFile = NULL;
	const char* outFileName = "..\\res\\out_crop_vfilter.yuv";
	fopen_s(&outFile, outFileName, "wb");
	if (!outFile) {
		printf("Fail to create file for output\n");
		return -1;
	}

	avfilter_register_all();

	//1.创建filter graph
	AVFilterGraph* filter_graph = avfilter_graph_alloc();
	if (!filter_graph) {
		printf("Fail to create filter graph!\n");
		return -1;
	}

	//2.创建 source filter ,源过滤器，用于输入数据
	char args[512];
	sprintf(args,
		"video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
		in_width, in_height, AV_PIX_FMT_YUV420P,
		1, 25, 1, 1);
	const AVFilter* bufferSrc = avfilter_get_by_name("buffer");   // AVFilterGraph的输入源
	AVFilterContext* bufferSrc_ctx;
	ret = avfilter_graph_create_filter(&bufferSrc_ctx, bufferSrc, "in", args, NULL, filter_graph);
	if (ret < 0) {
		printf("Fail to create filter bufferSrc\n");
		return -1;
	}

	//3.创建 sink filter，接收过滤器，用于输出数据，
	AVBufferSinkParams* bufferSink_params;
	AVFilterContext* bufferSink_ctx;
	const AVFilter* bufferSink = avfilter_get_by_name("buffersink");
	enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE };
	bufferSink_params = av_buffersink_params_alloc();
	bufferSink_params->pixel_fmts = pix_fmts;
	ret = avfilter_graph_create_filter(&bufferSink_ctx, bufferSink, "out", NULL,
		bufferSink_params, filter_graph);
	if (ret < 0) {
		printf("Fail to create filter sink filter\n");
		return -1;
	}

	//4.创建各种fliter,filterContext split filter
	const AVFilter* splitFilter = avfilter_get_by_name("split");
	AVFilterContext* splitFilter_ctx;
	ret = avfilter_graph_create_filter(&splitFilter_ctx, splitFilter, "split", "outputs=2",
		NULL, filter_graph);
	if (ret < 0) {
		printf("Fail to create split filter\n");
		return -1;
	}

	// crop filter
	const AVFilter* cropFilter = avfilter_get_by_name("crop");
	AVFilterContext* cropFilter_ctx;
	ret = avfilter_graph_create_filter(&cropFilter_ctx, cropFilter, "crop",
		"out_w=iw:out_h=ih/2:x=0:y=0", NULL, filter_graph);
	//宽度不变，高度减半，剪裁坐标起始：x=0,y=0。 提取上半部分视频
	if (ret < 0) {
		printf("Fail to create crop filter\n");
		return -1;
	}

	// vflip filter， 垂直翻转
	const AVFilter* vflipFilter = avfilter_get_by_name("vflip");
	AVFilterContext* vflipFilter_ctx;
	ret = avfilter_graph_create_filter(&vflipFilter_ctx, vflipFilter, "vflip", NULL, NULL, filter_graph);
	if (ret < 0) {
		printf("Fail to create vflip filter\n");
		return -1;
	}

	// overlay filter，叠加
	const AVFilter* overlayFilter = avfilter_get_by_name("overlay");
	AVFilterContext* overlayFilter_ctx;
	ret = avfilter_graph_create_filter(&overlayFilter_ctx, overlayFilter, "overlay",
		"y=H/1.8", NULL, filter_graph);  // y=H/2表示叠加在目标视频的半高位置
	if (ret < 0) {
		printf("Fail to create overlay filter\n");
		return -1;
	}

	//5.连接各种filter
	// src filter to split filter
	ret = avfilter_link(bufferSrc_ctx, 0, splitFilter_ctx, 0);
	if (ret != 0) {
		printf("Fail to link src filter and split filter\n");
		return -1;
	}
	// split filter's first pad to overlay filter's main pad
	ret = avfilter_link(splitFilter_ctx, 0, overlayFilter_ctx, 0);
	if (ret != 0) {
		printf("Fail to link split filter and overlay filter main pad\n");
		return -1;
	}
	// split filter's second pad to crop filter
	ret = avfilter_link(splitFilter_ctx, 1, cropFilter_ctx, 0);
	if (ret != 0) {
		printf("Fail to link split filter's second pad and crop filter\n");
		return -1;
	}
	// crop filter to vflip filter
	ret = avfilter_link(cropFilter_ctx, 0, vflipFilter_ctx, 0);
	if (ret != 0) {
		printf("Fail to link crop filter and vflip filter\n");
		return -1;
	}
	// vflip filter to overlay filter's second pad
	ret = avfilter_link(vflipFilter_ctx, 0, overlayFilter_ctx, 1);
	if (ret != 0) {
		printf("Fail to link vflip filter and overlay filter's second pad\n");
		return -1;
	}
	// overlay filter to sink filter
	ret = avfilter_link(overlayFilter_ctx, 0, bufferSink_ctx, 0);
	if (ret != 0) {
		printf("Fail to link overlay filter and sink filter\n");
		return -1;
	}

	// check filter graph
	ret = avfilter_graph_config(filter_graph, NULL);
	if (ret < 0) {
		printf("Fail in filter graph\n");
		return -1;
	}

	char* graph_str = avfilter_graph_dump(filter_graph, NULL);
	FILE* graphFile = NULL;
	fopen_s(&graphFile, "graphFile.txt", "w");  // 打印filtergraph的具体情况
	fprintf(graphFile, "%s", graph_str);
	av_free(graph_str);

	AVFrame* frame_in = av_frame_alloc();
	unsigned char* frame_buffer_in = (unsigned char*)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, in_width, in_height, 1));
	av_image_fill_arrays(frame_in->data, frame_in->linesize, frame_buffer_in,
		AV_PIX_FMT_YUV420P, in_width, in_height, 1);

	AVFrame* frame_out = av_frame_alloc();
	unsigned char* frame_buffer_out = (unsigned char*)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, in_width, in_height, 1));
	av_image_fill_arrays(frame_out->data, frame_out->linesize, frame_buffer_out,
		AV_PIX_FMT_YUV420P, in_width, in_height, 1);

	frame_in->width = in_width;
	frame_in->height = in_height;
	frame_in->format = AV_PIX_FMT_YUV420P;
	uint32_t frame_count = 0;
	while (1) {
		// 读取yuv数据
		if (fread(frame_buffer_in, 1, in_width * in_height * 3 / 2, inFile) != in_width * in_height * 3 / 2) {
			break;
		}
		//input Y,U,V
		frame_in->data[0] = frame_buffer_in;
		frame_in->data[1] = frame_buffer_in + in_width * in_height;
		frame_in->data[2] = frame_buffer_in + in_width * in_height * 5 / 4;

		//6.送入AVFrame数据，而不是AVPacket.要注意
		if (av_buffersrc_add_frame(bufferSrc_ctx, frame_in) < 0) {
			printf("Error while add frame.\n");
			break;
		}
		//7.提取处理完毕的AVFrame,  filter内部自己处理 已经连接好的过滤器
		ret = av_buffersink_get_frame(bufferSink_ctx, frame_out);
		if (ret < 0)
			break;

		//output Y,U,V
		if (frame_out->format == AV_PIX_FMT_YUV420P) {
			for (int i = 0; i < frame_out->height; i++) {
				fwrite(frame_out->data[0] + frame_out->linesize[0] * i, 1, frame_out->width, outFile);
			}
			for (int i = 0; i < frame_out->height / 2; i++) {
				fwrite(frame_out->data[1] + frame_out->linesize[1] * i, 1, frame_out->width / 2, outFile);
			}
			for (int i = 0; i < frame_out->height / 2; i++) {
				fwrite(frame_out->data[2] + frame_out->linesize[2] * i, 1, frame_out->width / 2, outFile);
			}
		}
		++frame_count;
		if (frame_count % 25 == 0)
			printf("Process %d frame!\n", frame_count);
		av_frame_unref(frame_out);
	}

	fclose(inFile);
	fclose(outFile);

	av_frame_free(&frame_in);
	av_frame_free(&frame_out);
	avfilter_graph_free(&filter_graph); // 内部去释放AVFilterContext产生的内存
	return 0;
}
