/*
 * Copyright (c) 2015 Himangi Saraogi <himangi774@gmail.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file vdpau filter.
 */

#include <vdpau/vdpau.h>
#include <vdpau/vdpau_x11.h>

#include <X11/Xlib.h>

#include "ffmpeg.h"

#include "libavutil/avstring.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavcodec/vdpau.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct {
    Display *dpy;
    int screen;
    VdpDevice vdp_device;
    VdpGetProcAddress *vdp_get_proc_address;
    VdpYCbCrFormat vdpau_format;
    AVFrame *frame[5];
    VdpVideoMixer vdp_video_mixer;
    int buffer_cnt;

    VdpGetErrorString *get_error_string;
    VdpGetInformationString *get_information_string;
    VdpBitmapSurfaceCreate *bitmap_surface_create;
    VdpBitmapSurfaceDestroy *bitmap_surface_destroy;
    VdpBitmapSurfacePutBitsNative *bitmap_surface_put_bits_native;
    VdpBitmapSurfaceQueryCapabilities *bitmap_surface_query_capabilities;
    VdpDecoderCreate *decoder_create;
    VdpDecoderDestroy *decoder_destroy;
    VdpDecoderRender *decoder_render;
    VdpDeviceDestroy *device_destroy;
    VdpGenerateCSCMatrix *generate_csc_matrix;
    VdpOutputSurfaceCreate *output_surface_create;
    VdpOutputSurfaceDestroy *output_surface_destroy;
    VdpOutputSurfacePutBitsIndexed *output_surface_put_bits_indexed;
    VdpOutputSurfaceGetBitsNative *output_surface_get_bits_native;
    VdpOutputSurfacePutBitsNative *output_surface_put_bits_native;
    VdpOutputSurfaceRenderBitmapSurface *output_surface_render_bitmap_surface;
    VdpOutputSurfaceRenderOutputSurface *output_surface_render_output_surface;
    VdpOutputSurfaceQueryCapabilities *output_surface_query_capabilities;
    VdpOutputSurfaceQueryPutBitsYCbCrCapabilities *output_surface_query_put_bits;
    VdpOutputSurfaceGetParameters *output_surface_get_parameters;
    VdpPreemptionCallbackRegister *preemption_callback_register;
    VdpPresentationQueueBlockUntilSurfaceIdle *presentation_queue_block_until_surface_idle;
    VdpPresentationQueueCreate *presentation_queue_create;
    VdpPresentationQueueDestroy *presentation_queue_destroy;
    VdpPresentationQueueDisplay *presentation_queue_display;
    VdpPresentationQueueGetTime *presentation_queue_get_time;
    VdpPresentationQueueSetBackgroundColor *presentation_queue_set_background_color;
    VdpPresentationQueueGetBackgroundColor *presentation_queue_get_background_color;
    VdpPresentationQueueQuerySurfaceStatus *presentation_queue_query_surface_status;
    VdpPresentationQueueTargetCreateX11 *presentation_queue_target_create_x11;
    VdpPresentationQueueTargetDestroy *presentation_queue_target_destroy;
    VdpVideoMixerCreate *video_mixer_create;
    VdpVideoMixerDestroy *video_mixer_destroy;
    VdpVideoMixerQueryFeatureSupport *video_mixer_query_feature_support;
    VdpVideoMixerRender *video_mixer_render;
    VdpVideoMixerSetAttributeValues *video_mixer_set_attribute_values;
    VdpVideoMixerGetFeatureEnables *video_mixer_get_feature_enables;
    VdpVideoMixerSetFeatureEnables *video_mixer_set_feature_enables;
    VdpVideoMixerGetFeatureSupport *video_mixer_get_feature_support;
    VdpVideoMixerGetParameterValues *video_mixer_get_parameter_values;
    VdpVideoMixerQueryParameterSupport *video_mixer_query_parameter_support;
    VdpVideoMixerQueryAttributeSupport *video_mixer_query_attribute_support;
    VdpVideoMixerQueryParameterValueRange *video_mixer_query_parameter_value_range;
    VdpVideoSurfaceCreate *video_surface_create;
    VdpVideoSurfaceDestroy *video_surface_destroy;
    VdpVideoSurfacePutBitsYCbCr *video_surface_put_bits_y_cb_cr;
    VdpVideoSurfaceGetBitsYCbCr *video_surface_get_bits;
    VdpVideoSurfaceGetParameters *video_surface_get_parameters;
    VdpVideoSurfaceQueryGetPutBitsYCbCrCapabilities *video_surface_query;
} VDPAUContext;

static const AVOption vdpau_options[] = {
};

AVFILTER_DEFINE_CLASS(vdpau);

static const int vdpau_formats[][2] = {
    { VDP_YCBCR_FORMAT_YV12, AV_PIX_FMT_YUV420P },
    { VDP_YCBCR_FORMAT_NV12, AV_PIX_FMT_NV12 },
    { VDP_YCBCR_FORMAT_YUYV, AV_PIX_FMT_YUYV422 },
    { VDP_YCBCR_FORMAT_UYVY, AV_PIX_FMT_UYVY422 },
};

static av_cold int init(AVFilterContext *ctx)
{
    VDPAUContext *s = ctx->priv;
    VdpStatus vdp_st;
    int i;

    vdp_st = vdp_device_create_x11(s->dpy, s->screen,
                                   &s->vdp_device, &s->vdp_get_proc_address);
    if (vdp_st != VDP_STATUS_OK) {
        av_log(ctx, AV_LOG_ERROR, "VDPAU device creation on X11 display %s failed.\n",
               XDisplayString(s->dpy));
        goto fail;
    }

#define GET_CALLBACK(id, result)                                                \
    do {                                                                            \
        void *tmp;                                                                  \
        vdp_st = s->vdp_get_proc_address(s->vdp_device, id, &tmp);                  \
        if (vdp_st != VDP_STATUS_OK) {                                              \
            av_log(ctx, AV_LOG_ERROR, "Error getting the " #id " callback.\n");    \
            goto fail;                                                              \
        }                                                                           \
        s->result = tmp;                                                            \
    } while (0)

    GET_CALLBACK(VDP_FUNC_ID_VIDEO_SURFACE_GET_BITS_Y_CB_CR, video_surface_get_bits);
    GET_CALLBACK(VDP_FUNC_ID_VIDEO_SURFACE_GET_PARAMETERS, video_surface_get_parameters);
    GET_CALLBACK(VDP_FUNC_ID_GET_ERROR_STRING, get_error_string);
    GET_CALLBACK(VDP_FUNC_ID_GET_INFORMATION_STRING, get_information_string);
    GET_CALLBACK(VDP_FUNC_ID_DEVICE_DESTROY, device_destroy);
    GET_CALLBACK(VDP_FUNC_ID_GENERATE_CSC_MATRIX, generate_csc_matrix);
    GET_CALLBACK(VDP_FUNC_ID_VIDEO_SURFACE_QUERY_GET_PUT_BITS_Y_CB_CR_CAPABILITIES, video_surface_query);
    GET_CALLBACK(VDP_FUNC_ID_VIDEO_SURFACE_CREATE, video_surface_create);
    GET_CALLBACK(VDP_FUNC_ID_VIDEO_SURFACE_DESTROY, video_surface_destroy);
    GET_CALLBACK(VDP_FUNC_ID_VIDEO_SURFACE_GET_PARAMETERS, video_surface_get_parameters);
    GET_CALLBACK(VDP_FUNC_ID_VIDEO_SURFACE_GET_BITS_Y_CB_CR, video_surface_get_bits);
    GET_CALLBACK(VDP_FUNC_ID_VIDEO_SURFACE_PUT_BITS_Y_CB_CR, video_surface_put_bits_y_cb_cr);
    GET_CALLBACK(VDP_FUNC_ID_OUTPUT_SURFACE_QUERY_CAPABILITIES, output_surface_query_capabilities);
    GET_CALLBACK(VDP_FUNC_ID_OUTPUT_SURFACE_QUERY_PUT_BITS_Y_CB_CR_CAPABILITIES, output_surface_query_put_bits);
    GET_CALLBACK(VDP_FUNC_ID_OUTPUT_SURFACE_CREATE, output_surface_create);
    GET_CALLBACK(VDP_FUNC_ID_OUTPUT_SURFACE_DESTROY, output_surface_destroy);
    GET_CALLBACK(VDP_FUNC_ID_OUTPUT_SURFACE_GET_BITS_NATIVE, output_surface_get_bits_native);
    GET_CALLBACK(VDP_FUNC_ID_OUTPUT_SURFACE_PUT_BITS_NATIVE, output_surface_put_bits_native);
    GET_CALLBACK(VDP_FUNC_ID_OUTPUT_SURFACE_PUT_BITS_INDEXED, output_surface_put_bits_indexed);
    GET_CALLBACK(VDP_FUNC_ID_BITMAP_SURFACE_QUERY_CAPABILITIES, bitmap_surface_query_capabilities);
    GET_CALLBACK(VDP_FUNC_ID_BITMAP_SURFACE_CREATE, bitmap_surface_create);
    GET_CALLBACK(VDP_FUNC_ID_BITMAP_SURFACE_DESTROY, bitmap_surface_destroy);
    GET_CALLBACK(VDP_FUNC_ID_BITMAP_SURFACE_PUT_BITS_NATIVE, bitmap_surface_put_bits_native);
    GET_CALLBACK(VDP_FUNC_ID_OUTPUT_SURFACE_RENDER_OUTPUT_SURFACE, output_surface_render_output_surface);
    GET_CALLBACK(VDP_FUNC_ID_OUTPUT_SURFACE_RENDER_BITMAP_SURFACE, output_surface_render_bitmap_surface);
    GET_CALLBACK(VDP_FUNC_ID_DECODER_CREATE, decoder_create);
    GET_CALLBACK(VDP_FUNC_ID_DECODER_DESTROY, decoder_destroy);
    GET_CALLBACK(VDP_FUNC_ID_DECODER_RENDER, decoder_render);
    GET_CALLBACK(VDP_FUNC_ID_VIDEO_MIXER_QUERY_FEATURE_SUPPORT, video_mixer_query_feature_support);
    GET_CALLBACK(VDP_FUNC_ID_VIDEO_MIXER_QUERY_PARAMETER_SUPPORT, video_mixer_query_parameter_support);
    GET_CALLBACK(VDP_FUNC_ID_VIDEO_MIXER_QUERY_ATTRIBUTE_SUPPORT, video_mixer_query_attribute_support);
    GET_CALLBACK(VDP_FUNC_ID_VIDEO_MIXER_QUERY_PARAMETER_VALUE_RANGE, video_mixer_query_parameter_value_range);
    GET_CALLBACK(VDP_FUNC_ID_VIDEO_MIXER_CREATE, video_mixer_create);
    GET_CALLBACK(VDP_FUNC_ID_VIDEO_MIXER_SET_FEATURE_ENABLES, video_mixer_set_feature_enables);
    GET_CALLBACK(VDP_FUNC_ID_VIDEO_MIXER_SET_ATTRIBUTE_VALUES, video_mixer_set_attribute_values);
    GET_CALLBACK(VDP_FUNC_ID_VIDEO_MIXER_GET_FEATURE_SUPPORT, video_mixer_get_feature_support);
    GET_CALLBACK(VDP_FUNC_ID_VIDEO_MIXER_GET_FEATURE_ENABLES, video_mixer_get_feature_enables);
    GET_CALLBACK(VDP_FUNC_ID_VIDEO_MIXER_GET_PARAMETER_VALUES, video_mixer_get_parameter_values);
    GET_CALLBACK(VDP_FUNC_ID_VIDEO_MIXER_GET_ATTRIBUTE_VALUES, video_mixer_set_attribute_values);
    GET_CALLBACK(VDP_FUNC_ID_VIDEO_MIXER_DESTROY, video_mixer_destroy);
    GET_CALLBACK(VDP_FUNC_ID_VIDEO_MIXER_RENDER, video_mixer_render);
    GET_CALLBACK(VDP_FUNC_ID_PRESENTATION_QUEUE_TARGET_DESTROY, presentation_queue_target_destroy);
    GET_CALLBACK(VDP_FUNC_ID_PRESENTATION_QUEUE_CREATE, presentation_queue_create);
    GET_CALLBACK(VDP_FUNC_ID_PRESENTATION_QUEUE_DESTROY, presentation_queue_destroy);
    GET_CALLBACK(VDP_FUNC_ID_PRESENTATION_QUEUE_SET_BACKGROUND_COLOR, presentation_queue_set_background_color);
    GET_CALLBACK(VDP_FUNC_ID_PRESENTATION_QUEUE_GET_BACKGROUND_COLOR, presentation_queue_get_background_color);
    GET_CALLBACK(VDP_FUNC_ID_PRESENTATION_QUEUE_GET_TIME, presentation_queue_get_time);
    GET_CALLBACK(VDP_FUNC_ID_PRESENTATION_QUEUE_DISPLAY, presentation_queue_display);
    GET_CALLBACK(VDP_FUNC_ID_PRESENTATION_QUEUE_BLOCK_UNTIL_SURFACE_IDLE, presentation_queue_block_until_surface_idle);
    GET_CALLBACK(VDP_FUNC_ID_PRESENTATION_QUEUE_QUERY_SURFACE_STATUS, presentation_queue_query_surface_status);
    GET_CALLBACK(VDP_FUNC_ID_PREEMPTION_CALLBACK_REGISTER, preemption_callback_register);

    s->buffer_cnt = 3;
    for (i = 0; i < FF_ARRAY_ELEMS(vdpau_formats); i++) {
        VdpBool supported;
        vdp_st = s->video_surface_query(s->vdp_device, VDP_CHROMA_TYPE_420,
                                        vdpau_formats[i][0], &supported);
        if (vdp_st != VDP_STATUS_OK) {
            av_log(ctx, AV_LOG_ERROR,
                   "Error querying VDPAU surface capabilities: %s\n",
                   s->get_error_string(vdp_st));
            goto fail;
        }
        if (supported)
            break;
    }

    if (i == FF_ARRAY_ELEMS(vdpau_formats)) {
        av_log(NULL, AV_LOG_ERROR,
               "Supported VDPAU formats not present.\n");
        return AVERROR(EINVAL);
    }

    s->vdpau_format = vdpau_formats[i][0];

    return 0;

fail:
    av_log(ctx, AV_LOG_ERROR, "VDPAU init failed for stream");
    return AVERROR(EINVAL);
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *pix_fmts = NULL;

    return ff_set_common_formats(ctx, pix_fmts);
}

static int config_input(AVFilterLink *inlink)
{
    int i;
    VDPAUContext *s = inlink->dst->priv;


    const static VdpVideoMixerFeature mixer_features[] = {
        VDP_VIDEO_MIXER_FEATURE_SHARPNESS,
        VDP_VIDEO_MIXER_FEATURE_NOISE_REDUCTION,
    };
    uint32_t feature_count = 2;

    const static VdpVideoMixerParameter mixer_params[] = {
        VDP_VIDEO_MIXER_PARAMETER_VIDEO_SURFACE_WIDTH,
        VDP_VIDEO_MIXER_PARAMETER_VIDEO_SURFACE_HEIGHT,
    };

    uint32_t parameter_count = 2;
    void const *parameter_values[] = {&inlink->w, &inlink->h};

    s->video_mixer_create(s->vdp_device, feature_count, mixer_features,
                          parameter_count, mixer_params, parameter_values,
                          &s->vdp_video_mixer);


    for (i = 0; i < s->buffer_cnt; i++) {
        s->frame[i] = ff_get_video_buffer(inlink, inlink->w, inlink->h);
        if (!s->frame[i])
            return AVERROR(ENOMEM);
    }

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *inpicref)
{
    AVFilterContext *ctx = inlink->dst;
    VDPAUContext *s = ctx->priv;
    VdpStatus vdp_st;
    int i;
    VdpVideoSurface input_video_surface;

    /*TODO: check all formats and find the ones supported by the VDPAU device */

    //Use VDP_CHROMA_TYPE_420 for chroma type as libavcodec decodes to it.
    vdp_st = s->video_surface_create(s->vdp_device, VDP_CHROMA_TYPE_420,
                                     inpicref->width, inpicref->height,
                                     &input_video_surface);
    if (vdp_st != VDP_STATUS_OK) {
        av_log(ctx, AV_LOG_ERROR,
               "Error creating input video surface: %s\n",
               s->get_error_string(vdp_st));
        return AVERROR_INVALIDDATA;
    }

    /* Put bits */
    const void *source_planes[3];
    uint32_t source_pitches[] = { inpicref->width * inpicref->height };

    for (i = 0; i < s->buffer_cnt; i++)
    {
        source_planes[i] = s->frame[i]->data;
    }

    vdp_st = s->video_surface_put_bits_y_cb_cr(input_video_surface,
                                               VDP_CHROMA_TYPE_420,
                                               source_planes, source_pitches);
    if (vdp_st != VDP_STATUS_OK) {
        av_log(ctx, AV_LOG_ERROR,
               "Error copying to vdpau device: %s\n",
               s->get_error_string(vdp_st));
        return AVERROR(EIO);
    }



    // Get bits.
    vdp_st = s->video_surface_get_bits(input_video_surface,
                                       VDP_YCBCR_FORMAT_NV12,
                                       source_planes, source_pitches);
    if (vdp_st != VDP_STATUS_OK) {
        av_log(ctx, AV_LOG_ERROR,
               "Error copying from vdpau device: %s\n",
               s->get_error_string(vdp_st));
        return AVERROR(EIO);
    }

    for (i = 0; i < s->buffer_cnt - 1; i++)
        s->frame[i] = s->frame[i+1];
    s->frame[s->buffer_cnt - 1] = ff_get_video_buffer(inlink, inlink->w, inlink->h);
    if (!s->frame[s->buffer_cnt - 1])
        return AVERROR(ENOMEM);
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{

}

static const AVFilterPad vdpau_inputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .filter_frame  = filter_frame,
        .config_props  = config_input,
    },
    { NULL }
};

static const AVFilterPad vdpau_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
    { NULL }
};

AVFilter ff_vf_vdpau = {
    .name          = "vdpau",
    .description   = NULL_IF_CONFIG_SMALL("Apply a VDPAU filter feature."),
    .priv_size     = sizeof(VDPAUContext),
    .priv_class    = &vdpau_class,
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = vdpau_inputs,
    .outputs       = vdpau_outputs,
};
