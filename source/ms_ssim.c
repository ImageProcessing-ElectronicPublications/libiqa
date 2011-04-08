/*
 * Copyright (c) 2011, Tom Distler (http://tdistler.com)
 * All rights reserved.
 *
 * The BSD License
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * - Neither the name of the tdistler.com nor the names of its contributors may
 *   be used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE 
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "iqa.h"
#include "ssim.h"
#include "decimate.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Default number of scales */
#define SCALES  5

/* Low-pass filter for down-sampling (9/7 biorthogonal wavelet filter) */
#define LPF_LEN 9
static const float g_lpf[LPF_LEN][LPF_LEN] = {
   { 0.000714f,-0.000450f,-0.002090f, 0.007132f, 0.016114f, 0.007132f,-0.002090f,-0.000450f, 0.000714f},
   {-0.000450f, 0.000283f, 0.001316f,-0.004490f,-0.010146f,-0.004490f, 0.001316f, 0.000283f,-0.000450f},
   {-0.002090f, 0.001316f, 0.006115f,-0.020867f,-0.047149f,-0.020867f, 0.006115f, 0.001316f,-0.002090f},
   { 0.007132f,-0.004490f,-0.020867f, 0.071207f, 0.160885f, 0.071207f,-0.020867f,-0.004490f, 0.007132f},
   { 0.016114f,-0.010146f,-0.047149f, 0.160885f, 0.363505f, 0.160885f,-0.047149f,-0.010146f, 0.016114f},
   { 0.007132f,-0.004490f,-0.020867f, 0.071207f, 0.160885f, 0.071207f,-0.020867f,-0.004490f, 0.007132f},
   {-0.002090f, 0.001316f, 0.006115f,-0.020867f,-0.047149f,-0.020867f, 0.006115f, 0.001316f,-0.002090f},
   {-0.000450f, 0.000283f, 0.001316f,-0.004490f,-0.010146f,-0.004490f, 0.001316f, 0.000283f,-0.000450f},
   { 0.000714f,-0.000450f,-0.002090f, 0.007132f, 0.016114f, 0.007132f,-0.002090f,-0.000450f, 0.000714f},
};

/* Alpha, beta, and gamma values for each scale */
static float g_alphas[] = { 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.1333f };
static float g_betas[]  = { 0.0448f, 0.2856f, 0.3001f, 0.2363f, 0.1333f };
static float g_gammas[] = { 0.0448f, 0.2856f, 0.3001f, 0.2363f, 0.1333f };


struct _context {
    double l;  /* Luminance */
    double c;  /* Contrast */
    double s;  /* Structure */
};

int _ms_ssim_map(const struct _ssim_int *si, void *ctx)
{
    struct _context *ms_ctx = (struct _context*)ctx;
    ms_ctx->l += si->l;
    ms_ctx->c += si->c;
    ms_ctx->s += si->s;
    return 0;
}

float _ms_ssim_reduce(int w, int h, void *ctx)
{
    double size = (double)(w*h);
    struct _context *ms_ctx = (struct _context*)ctx;
    ms_ctx->l /= size;
    ms_ctx->c /= size;
    ms_ctx->s /= size;
    return 0.0f;
}


/* Releases the scaled buffers */
void _free_buffers(float **buf, int scales)
{
    int idx;
    for (idx=0; idx<scales; ++idx)
        free(buf[idx]);
}

/* Allocates the scaled buffers. If error, all buffers are free'd */
int _alloc_buffers(float **buf, int w, int h, int scales)
{
    int idx;
    int cur_w = w;
    int cur_h = h;
    for (idx=0; idx<scales; ++idx) {
        buf[idx] = (float*)malloc(cur_w*cur_h*sizeof(float));
        if (!buf[idx]) {
            _free_buffers(buf, idx);
            return 1;
        }
        cur_w/=2;
        cur_h/=2;
    }
    return 0;
}

/*
 * MS_SSIM(X,Y) = Lm(x,y)^aM * MULT[j=1->M]( Cj(x,y)^bj  *  Sj(x,y)^gj )
 * where,
 *  L = mean
 *  C = variance
 *  S = cross-correlation
 *
 *  b1=g1=0.0448, b2=g2=0.2856, b3=g3=0.3001, b4=g4=0.2363, a5=b5=g5=0.1333
 */
float iqa_ms_ssim(const unsigned char *ref, const unsigned char *cmp, int w, int h, 
    int stride, const struct iqa_ms_ssim_args *args)
{
    int wang=0;
    int scales=SCALES;
    int gauss=1;
    float *alphas=g_alphas, *betas=g_betas, *gammas=g_gammas;
    int idx,x,y,cur_w,cur_h;
    int offset,src_offset;
    float **ref_imgs, **cmp_imgs; /* Array of pointers to scaled images */
    float result, msssim;
    struct _kernel lpf, window;
    struct iqa_ssim_args s_args;
    struct _map_reduce mr;
    struct _context *ms_ctxs;
    double luminance=1.0, contrast=1.0, structure=1.0;

    if (args) {
        wang   = args->wang;
        scales = args->scales;
        gauss  = args->gaussian;
        alphas = args->alphas;
        betas  = args->betas;
        gammas = args->gammas;
    }

    window.kernel = (float*)g_square_window;
    window.w = window.h = SQUARE_LEN;
    window.normalized = 1;
    window.bnd_opt = KBND_SYMMETRIC;
    if (gauss) {
        window.kernel = (float*)g_gaussian_window;
        window.w = window.h = GAUSSIAN_LEN;
    }

    mr.map     = _ms_ssim_map;
    mr.reduce  = _ms_ssim_reduce;
    ms_ctxs = (struct _context*)malloc(scales*sizeof(struct _context));
    if (!ms_ctxs)
        return INFINITY;
    memset(ms_ctxs, 0, scales*sizeof(struct _context));

    /* Allocate the scaled image buffers */
    ref_imgs = (float**)malloc(scales*sizeof(float*));
    cmp_imgs = (float**)malloc(scales*sizeof(float*));
    if (!ref_imgs || !cmp_imgs) {
        if (ref_imgs) free(ref_imgs);
        if (cmp_imgs) free(cmp_imgs);
        free(ms_ctxs);
        return INFINITY;
    }
    if (_alloc_buffers(ref_imgs, w, h, scales)) {
        free(ref_imgs);
        free(cmp_imgs);
        free(ms_ctxs);
        return INFINITY;
    }
    if (_alloc_buffers(cmp_imgs, w, w, scales)) {
        _free_buffers(ref_imgs, scales);
        free(ref_imgs);
        free(cmp_imgs);
        free(ms_ctxs);
        return INFINITY;
    }

    /* Copy original images into first scale buffer, forcing stride = width. */
    for (y=0; y<h; ++y) {
        src_offset = y*stride;
        offset = y*w;
        for (x=0; x<w; ++x, ++offset, ++src_offset) {
            ref_imgs[0][offset] = (float)ref[src_offset];
            cmp_imgs[0][offset] = (float)cmp[src_offset];
        }
    }

    /* Create scaled versions of the images */
    cur_w=w;
    cur_h=h;
    lpf.kernel = (float*)g_lpf;
    lpf.w = lpf.h = LPF_LEN;
    lpf.normalized = 1;
    lpf.bnd_opt = KBND_SYMMETRIC;
    for (idx=1; idx<scales; ++idx) {
        if (_iqa_decimate(ref_imgs[idx-1], cur_w, cur_h, 2, &lpf, ref_imgs[idx], 0, 0) ||
            _iqa_decimate(cmp_imgs[idx-1], cur_w, cur_h, 2, &lpf, cmp_imgs[idx], &cur_w, &cur_h))
        {
            _free_buffers(ref_imgs, scales);
            _free_buffers(cmp_imgs, scales);
            free(ref_imgs);
            free(cmp_imgs);
            free(ms_ctxs);
            return INFINITY;
        }
    }

    cur_w=w;
    cur_h=h;
    for (idx=0; idx<scales; ++idx) {
        if (!wang) {
            /* MS-SSIM* (Rouse/Hemami) */
            s_args.alpha = 1.0f;
            s_args.beta  = 1.0f;
            s_args.gamma = 1.0f;
            s_args.K1 = 0.0f; /* Force stabilization constants to 0 */
            s_args.K2 = 0.0f;
            s_args.L  = 255;
            s_args.f  = 1; /* Don't resize */
            mr.context = &(ms_ctxs[idx]);
            result = _iqa_ssim(ref_imgs[idx], cmp_imgs[idx], cur_w, cur_h, &window, &mr, &s_args);
        }
        else {
            /* MS-SSIM (Wang) */
            s_args.alpha = 1.0f;
            s_args.beta  = 1.0f;
            s_args.gamma = 1.0f;
            s_args.K1 = 0.01f;
            s_args.K2 = 0.03f;
            s_args.L  = 255;
            s_args.f  = 1; /* Don't resize */
            mr.context = &(ms_ctxs[idx]);
            result = _iqa_ssim(ref_imgs[idx], cmp_imgs[idx], cur_w, cur_h, &window, &mr, &s_args);
        }

        if (result == INFINITY) {
            msssim = result;
            break;
        }
        cur_w/=2;
        cur_h/=2;
    }

    /* Compute final MS_SSIM value */
    for (idx=0; idx<scales; ++idx) {
        if (idx==scales-1)
            luminance *=  pow(ms_ctxs[idx].l,(double)g_alphas[idx]);
        contrast *=  pow(ms_ctxs[idx].c,(double)g_betas[idx]);
        structure *=  pow(fabs(ms_ctxs[idx].s),(double)g_gammas[idx]);
    }
    msssim = (float)(luminance * contrast * structure);

    _free_buffers(ref_imgs, scales);
    _free_buffers(cmp_imgs, scales);
    free(ref_imgs);
    free(cmp_imgs);
    free(ms_ctxs);

    return msssim;
}
