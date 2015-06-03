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


#include <dlfcn.h>

#include <vdpau/vdpau.h>
#include <vdpau/vdpau_x11.h>

#include <X11/Xlib.h>

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
} VDPAUContext;

static const AVOption vdpau_options[] = {
};

AVFILTER_DEFINE_CLASS(vdpau);

static av_cold int init(AVFilterContext *ctx)
{
    VDPAUContext *s = ctx->priv;
    VdpStatus vdp_st;

    vdp_st = vdp_device_create_x11(s->dpy, s->screen,
                                   &s->vdp_device, &s->vdp_get_proc_address);
    if (vdp_st != VDP_STATUS_OK) {
        av_log(NULL, AV_LOG_ERROR, "VDPAU device creation on X11 display %s failed.\n",
               XDisplayString(s->dpy));
        goto fail;
    }

    return 0;

fail:
    av_log(NULL, AV_LOG_ERROR, "VDPAU init failed for stream");
    return AVERROR(EINVAL);
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *pix_fmts = NULL;

    return ff_set_common_formats(ctx, pix_fmts);
}

static int config_input(AVFilterLink *inlink)
{
    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *inpicref)
{
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
