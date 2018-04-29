/* Copyright (C) 2013-2016, The Regents of The University of Michigan.
All rights reserved.

This software was developed in the APRIL Robotics Lab under the
direction of Edwin Olson, ebolson@umich.edu. This software may be
available under alternative licensing terms; contact the address above.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

The views and conclusions contained in the software and documentation are those
of the authors and should not be interpreted as representing official policies,
either expressed or implied, of the Regents of The University of Michigan.
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <algorithm>

#include "common/image_u8.h"
#include "common/pnm.h"
#include "common/math_util.h"

// least common multiple of 64 (sandy bridge cache line) and 24 (stride
// needed for RGB in 8-wide vector processing)
#define DEFAULT_ALIGNMENT_U8 96

image_u8_t *image_u8_create_stride(unsigned int width, unsigned int height, unsigned int stride)
{
    uint8_t *buf = (uint8_t *)calloc(height*stride, sizeof(uint8_t));

    // const initializer
    image_u8_t tmp = { (int32_t)width, (int32_t)height, (int32_t)stride, buf };

    image_u8_t *im = (image_u8_t *)calloc(1, sizeof(image_u8_t));
    memcpy(im, &tmp, sizeof(image_u8_t));
    return im;
}

image_u8_t *image_u8_create(unsigned int width, unsigned int height)
{
    return image_u8_create_alignment(width, height, DEFAULT_ALIGNMENT_U8);
}

image_u8_t *image_u8_create_alignment(unsigned int width, unsigned int height, unsigned int alignment)
{
    int stride = width;

    if ((stride % alignment) != 0)
        stride += alignment - (stride % alignment);

    return image_u8_create_stride(width, height, stride);
}

image_u8_t *image_u8_copy(const image_u8_t *in)
{
    uint8_t *buf = (uint8_t *)malloc(in->height*in->stride*sizeof(uint8_t));
    memcpy(buf, in->buf, in->height*in->stride*sizeof(uint8_t));

    // const initializer
    image_u8_t tmp = { in->width, in->height, in->stride, buf };

    image_u8_t *copy = (image_u8_t *)calloc(1, sizeof(image_u8_t));
    memcpy(copy, &tmp, sizeof(image_u8_t));
    return copy;
}

void image_u8_destroy(image_u8_t *im)
{
    if (!im)
        return;

    free(im->buf);
    free(im);
}

////////////////////////////////////////////////////////////
// PNM file i/o
image_u8_t *image_u8_create_from_pnm(const char *path)
{
    return image_u8_create_from_pnm_alignment(path, DEFAULT_ALIGNMENT_U8);
}

image_u8_t *image_u8_create_from_pnm_alignment(const char *path, int alignment)
{
    pnm_t *pnm = pnm_create_from_file(path);
    if (pnm == NULL)
        return NULL;

    image_u8_t *im = NULL;

    switch (pnm->format) {
        case PNM_FORMAT_GRAY: {
            im = image_u8_create_alignment(pnm->width, pnm->height, alignment);

            if (pnm->max == 255) {
                for (int y = 0; y < im->height; y++)
                    memcpy(&im->buf[y*im->stride], &pnm->buf[y*im->width], im->width);
            } else if (pnm->max == 65535) {
                for (int y = 0; y < im->height; y++)
                    for (int x = 0; x < im->width; x++)
                        im->buf[y*im->stride + x] = pnm->buf[2*(y*im->width + x)];
            } else {
                assert(0);
            }

            break;
        }

        case PNM_FORMAT_RGB: {
            im = image_u8_create_alignment(pnm->width, pnm->height, alignment);

            if (pnm->max == 255) {
                // Gray conversion for RGB is gray = (r + g + g + b)/4
                for (int y = 0; y < im->height; y++) {
                    for (int x = 0; x < im->width; x++) {
                        uint8_t gray = (pnm->buf[y*im->width*3 + 3*x+0] +    // r
                                        pnm->buf[y*im->width*3 + 3*x+1] +    // g
                                        pnm->buf[y*im->width*3 + 3*x+1] +    // g
                                        pnm->buf[y*im->width*3 + 3*x+2])     // b
                            / 4;

                        im->buf[y*im->stride + x] = gray;
                    }
                }
            } else if (pnm->max == 65535) {
                for (int y = 0; y < im->height; y++) {
                    for (int x = 0; x < im->width; x++) {
                        int r = pnm->buf[6*(y*im->width + x) + 0];
                        int g = pnm->buf[6*(y*im->width + x) + 2];
                        int b = pnm->buf[6*(y*im->width + x) + 4];

                        im->buf[y*im->stride + x] = (r + g + g + b) / 4;
                    }
                }
            } else {
                assert(0);
            }

            break;
        }

        case PNM_FORMAT_BINARY: {
            im = image_u8_create_alignment(pnm->width, pnm->height, alignment);

            // image is padded to be whole bytes on each row.

            // how many bytes per row on the input?
            int pbmstride = (im->width + 7) / 8;

            for (int y = 0; y < im->height; y++) {
                for (int x = 0; x < im->width; x++) {
                    int byteidx = y * pbmstride + x / 8;
                    int bitidx = 7 - (x & 7);

                    // ack, black is one according to pbm docs!
                    if ((pnm->buf[byteidx] >> bitidx) & 1)
                        im->buf[y*im->stride + x] = 0;
                    else
                        im->buf[y*im->stride + x] = 255;
                }
            }
            break;
        }
    }

    pnm_destroy(pnm);
    return im;
}

image_u8_t *image_u8_create_from_f32(image_f32_t *fim)
{
    image_u8_t *im = image_u8_create(fim->width, fim->height);

    for (int y = 0; y < fim->height; y++) {
        for (int x = 0; x < fim->width; x++) {
            float v = fim->buf[y*fim->stride + x];
            im->buf[y*im->stride + x] = (int) (255 * v);
        }
    }

    return im;
}


int image_u8_write_pnm(const image_u8_t *im, const char *path)
{
    FILE *f = fopen(path, "wb");
    int res = 0;

    if (f == NULL) {
        res = -1;
        goto finish;
    }

    // Only outputs to grayscale
    fprintf(f, "P5\n%d %d\n255\n", im->width, im->height);

    for (int y = 0; y < im->height; y++) {
        if (im->width != fwrite(&im->buf[y*im->stride], 1, im->width, f)) {
            res = -2;
            goto finish;
        }
    }

  finish:
    if (f != NULL)
        fclose(f);

    return res;
}

void image_u8_draw_circle(image_u8_t *im, float x0, float y0, float r, int v)
{
    r = r*r;

    for (int y = y0-r; y <= y0+r; y++) {
        for (int x = x0-r; x <= x0+r; x++) {
            float d = (x-x0)*(x-x0) + (y-y0)*(y-y0);
            if (d > r)
                continue;

            if (x >= 0 && x < im->width && y >= 0 && y < im->height) {
                int idx = y*im->stride + x;
                im->buf[idx] = v;
            }
        }
    }
}

void image_u8_draw_annulus(image_u8_t *im, float x0, float y0, float r0, float r1, int v)
{
    r0 = r0*r0;
    r1 = r1*r1;

    assert(r0 < r1);

    for (int y = y0-r1; y <= y0+r1; y++) {
        for (int x = x0-r1; x <= x0+r1; x++) {
            float d = (x-x0)*(x-x0) + (y-y0)*(y-y0);
            if (d < r0 || d > r1)
                continue;

            int idx = y*im->stride + x;
            im->buf[idx] = v;
        }
    }
}

// only widths 1 and 3 supported (and 3 only badly)
void image_u8_draw_line(image_u8_t *im, float x0, float y0, float x1, float y1, int v, int width)
{
    double dist = sqrtf((y1-y0)*(y1-y0) + (x1-x0)*(x1-x0));
    double delta = 0.5 / dist;

    // terrible line drawing code
    for (float f = 0; f <= 1; f += delta) {
        int x = ((int) (x1 + (x0 - x1) * f));
        int y = ((int) (y1 + (y0 - y1) * f));

        if (x < 0 || y < 0 || x >= im->width || y >= im->height)
            continue;

        int idx = y*im->stride + x;
        im->buf[idx] = v;
        if (width > 1) {
            im->buf[idx+1] = v;
            im->buf[idx+im->stride] = v;
            im->buf[idx+1+im->stride] = v;
        }
    }
}

void image_u8_darken(image_u8_t *im)
{
    for (int y = 0; y < im->height; y++) {
        for (int x = 0; x < im->width; x++) {
            im->buf[im->stride*y+x] /= 2;
        }
    }
}

static void convolve(const uint8_t *x, uint8_t *y, int sz, const uint8_t *k, int ksz)
{
    assert((ksz&1)==1);

    for (int i = 0; i < ksz/2 && i < sz; i++)
        y[i] = x[i];

    for (int i = 0; i < sz - ksz; i++) {
        uint32_t acc = 0;

        for (int j = 0; j < ksz; j++)
            acc += k[j]*x[i+j];

        y[ksz/2 + i] = acc >> 8;
    }

    for (int i = sz - ksz + ksz/2; i < sz; i++)
        y[i] = x[i];
}

void image_u8_convolve_2D(image_u8_t *im, const uint8_t *k, int ksz)
{
    assert((ksz & 1) == 1); // ksz must be odd.

    for (int y = 0; y < im->height; y++) {
#ifdef _MSC_VER
        uint8_t *x = (uint8_t *)malloc(im->stride*sizeof *x);
#else
        uint8_t x[im->stride];
#endif
        memcpy(x, &im->buf[y*im->stride], im->stride);

        convolve(x, &im->buf[y*im->stride], im->width, k, ksz);

#ifdef _MSC_VER
        free(x);
#endif
    }

    for (int x = 0; x < im->width; x++) {
#ifdef _MSC_VER
        uint8_t *xb = (uint8_t *)malloc(im->height*sizeof *xb);
        uint8_t *yb = (uint8_t *)malloc(im->height*sizeof *yb);
#else
        uint8_t xb[im->height];
        uint8_t yb[im->height];
#endif

        for (int y = 0; y < im->height; y++)
            xb[y] = im->buf[y*im->stride + x];

        convolve(xb, yb, im->height, k, ksz);

        for (int y = 0; y < im->height; y++)
            im->buf[y*im->stride + x] = yb[y];

#ifdef _MSC_VER
        free(xb);
        free(yb);
#endif
    }
}

void image_u8_gaussian_blur(image_u8_t *im, double sigma, int ksz)
{
    if (sigma == 0)
        return;

    assert((ksz & 1) == 1); // ksz must be odd.

    // build the kernel.
#ifdef _MSC_VER
    double *dk = (double *)malloc(ksz*sizeof *dk);
#else
    double dk[ksz];
#endif

    // for kernel of length 5:
    // dk[0] = f(-2), dk[1] = f(-1), dk[2] = f(0), dk[3] = f(1), dk[4] = f(2)
    for (int i = 0; i < ksz; i++) {
        int x = -ksz/2 + i;
        double v = exp(-.5*sq(x / sigma));
        dk[i] = v;
    }

    // normalize
    double acc = 0;
    for (int i = 0; i < ksz; i++)
        acc += dk[i];

    for (int i = 0; i < ksz; i++)
        dk[i] /= acc;

#ifdef _MSC_VER
    uint8_t *k = (uint8_t *)malloc(ksz*sizeof *k);
#else
    uint8_t k[ksz];
#endif
    for (int i = 0; i < ksz; i++)
        k[i] = dk[i]*255;

    if (0) {
        for (int i = 0; i < ksz; i++)
            printf("%d %15f %5d\n", i, dk[i], k[i]);
    }

    image_u8_convolve_2D(im, k, ksz);

#ifdef _MSC_VER
    free(dk);
    free(k);
#endif
}

image_u8_t *image_u8_rotate(const image_u8_t *in, double rad, uint8_t pad)
{
    int iwidth = in->width, iheight = in->height;
    rad = -rad; // interpret y as being "down"

    float c = cos(rad), s = sin(rad);

    float p[][2] = { { 0, 0}, { (float)iwidth, 0 }, { (float)iwidth, (float)iheight }, { 0, (float)iheight} };

    //float xmin = HUGE_VALF, xmax = -HUGE_VALF, ymin = HUGE_VALF, ymax = -HUGE_VALF;
    float xmin = 0x7f800000;//std::numeric_limits<float>::infinity(),
    float xmax = -0x7f800000;//std::numeric_limits<float>::infinity(),
    float ymin = 0x7f800000;//std::numeric_limits<float>::infinity(),
    float ymax = -0x7f800000;//std::numeric_limits<float>::infinity();

    float icx = iwidth / 2.0, icy = iheight / 2.0;

    for (int i = 0; i < 4; i++) {
        float px = p[i][0] - icx;
        float py = p[i][1] - icy;

        float nx = px*c - py*s;
        float ny = px*s + py*c;

        xmin = (std::min)(xmin, nx);
        xmax = (std::max)(xmax, nx);
        ymin = (std::min)(ymin, ny);
        ymax = (std::max)(ymax, ny);
    }

    int owidth = ceil(xmax-xmin), oheight = ceil(ymax - ymin);
    image_u8_t *out = image_u8_create(owidth, oheight);

    // iterate over output pixels.
    for (int oy = 0; oy < oheight; oy++) {
        for (int ox = 0; ox < owidth; ox++) {
            // work backwards from destination coordinates...
            // sample pixel centers.
            float sx = ox - owidth / 2.0 + .5;
            float sy = oy - oheight / 2.0 + .5;

            // project into input-image space
            int ix = floor(sx*c + sy*s + icx);
            int iy = floor(-sx*s + sy*c + icy);

            if (ix >= 0 && iy >= 0 && ix < iwidth && iy < iheight)
                out->buf[oy*out->stride+ox] = in->buf[iy*in->stride + ix];
            else
                out->buf[oy*out->stride+ox] = pad;
        }
    }

    return out;
}

#ifdef __ARM_NEON__
#include <arm_neon.h>

void neon_decimate2(uint8_t * __restrict dest, int destwidth, int destheight, int deststride,
                    uint8_t * __restrict src, int srcwidth, int srcheight, int srcstride)
{
    for (int y = 0; y < destheight; y++) {
        for (int x = 0; x < destwidth; x+=8) {
            uint8x16x2_t row0 = vld2q_u8(src + 2*x);
            uint8x16x2_t row1 = vld2q_u8(src + 2*x + srcstride);
            uint8x16_t sum0 = vhaddq_u8(row0.val[0], row1.val[1]);
            uint8x16_t sum1 = vhaddq_u8(row1.val[0], row0.val[1]);
            uint8x16_t sum = vhaddq_u8(sum0, sum1);
            vst1q_u8(dest + x, sum);
        }
        src += 2*srcstride;
        dest += deststride;
    }
}

void neon_decimate3(uint8_t * __restrict dest, int destwidth, int destheight, int deststride,
                    uint8_t * __restrict src, int srcwidth, int srcheight, int srcstride)
{
    for (int y = 0; y < destheight; y++) {
        for (int x = 0; x < destwidth; x+=8) {
            uint8x16x3_t row0 = vld3q_u8(src + 3*x);
            uint8x16x3_t row1 = vld3q_u8(src + 3*x + srcstride);
            uint8x16x3_t row2 = vld3q_u8(src + 3*x + 2*srcstride);

            uint8x16_t sum0 = vhaddq_u8(row0.val[0], row0.val[1]);
            uint8x16_t sum1 = vhaddq_u8(row0.val[2], row1.val[0]);
            uint8x16_t sum2 = vhaddq_u8(row1.val[1], row1.val[2]);
            uint8x16_t sum3 = vhaddq_u8(row2.val[0], row2.val[1]);

            uint8x16_t suma = vhaddq_u8(sum0, sum1);
            uint8x16_t sumb = vhaddq_u8(sum2, sum3);
            uint8x16_t sum = vhaddq_u8(suma, sumb);

            vst1q_u8(dest + x, sum);
        }
        src += 3*srcstride;
        dest += deststride;
    }
}

void neon_decimate4(uint8_t * __restrict dest, int destwidth, int destheight, int deststride,
                    uint8_t * __restrict src, int srcwidth, int srcheight, int srcstride)
{
    for (int y = 0; y < destheight; y++) {
        for (int x = 0; x < destwidth; x+=8) {
            uint8x16x4_t row0 = vld4q_u8(src + 4*x);
            uint8x16x4_t row1 = vld4q_u8(src + 4*x + srcstride);
            uint8x16x4_t row2 = vld4q_u8(src + 4*x + 2*srcstride);
            uint8x16x4_t row3 = vld4q_u8(src + 4*x + 3*srcstride);

            uint8x16_t sum0, sum1;

            sum0 = vhaddq_u8(row0.val[0], row0.val[3]);
            sum1 = vhaddq_u8(row0.val[2], row0.val[1]);
            uint8x16_t suma = vhaddq_u8(sum0, sum1);

            sum0 = vhaddq_u8(row1.val[0], row1.val[3]);
            sum1 = vhaddq_u8(row1.val[2], row1.val[1]);
            uint8x16_t sumb = vhaddq_u8(sum0, sum1);

            sum0 = vhaddq_u8(row2.val[0], row2.val[3]);
            sum1 = vhaddq_u8(row2.val[2], row2.val[1]);
            uint8x16_t sumc = vhaddq_u8(sum0, sum1);

            sum0 = vhaddq_u8(row3.val[0], row3.val[3]);
            sum1 = vhaddq_u8(row3.val[2], row3.val[1]);
            uint8x16_t sumd = vhaddq_u8(sum0, sum1);

            uint8x16_t sumx = vhaddq_u8(suma, sumd);
            uint8x16_t sumy = vhaddq_u8(sumc, sumb);

            uint8x16_t sum = vhaddq_u8(sumx, sumy);

            vst1q_u8(dest + x, sum);
        }
        src += 4*srcstride;
        dest += deststride;
    }
}

#endif

image_u8_t *image_u8_decimate(image_u8_t *im, float ffactor)
{
    int width = im->width, height = im->height;

    if (ffactor == 1.5) {
        int swidth = width / 3 * 2, sheight = height / 3 * 2;

        image_u8_t *decim = image_u8_create(swidth, sheight);

        int y = 0, sy = 0;
        while (sy < sheight) {
            int x = 0, sx = 0;
            while (sx < swidth) {

                // a b c
                // d e f
                // g h i
                uint8_t a = im->buf[(y+0)*im->stride + (x+0)];
                uint8_t b = im->buf[(y+0)*im->stride + (x+1)];
                uint8_t c = im->buf[(y+0)*im->stride + (x+2)];

                uint8_t d = im->buf[(y+1)*im->stride + (x+0)];
                uint8_t e = im->buf[(y+1)*im->stride + (x+1)];
                uint8_t f = im->buf[(y+1)*im->stride + (x+2)];

                uint8_t g = im->buf[(y+2)*im->stride + (x+0)];
                uint8_t h = im->buf[(y+2)*im->stride + (x+1)];
                uint8_t i = im->buf[(y+2)*im->stride + (x+2)];

                decim->buf[(sy+0)*decim->stride + (sx + 0)] =
                    (4*a+2*b+2*d+e)/9;
                decim->buf[(sy+0)*decim->stride + (sx + 1)] =
                    (4*c+2*b+2*f+e)/9;

                decim->buf[(sy+1)*decim->stride + (sx + 0)] =
                    (4*g+2*d+2*h+e)/9;
                decim->buf[(sy+1)*decim->stride + (sx + 1)] =
                    (4*i+2*f+2*h+e)/9;

                x += 3;
                sx += 2;
            }

            y += 3;
            sy += 2;
        }

        return decim;
    }

    int factor = (int) ffactor;

    int swidth = width / factor, sheight = height / factor;

    image_u8_t *decim = image_u8_create(swidth, sheight);

#ifdef __ARM_NEON__
    if (factor == 2) {
        neon_decimate2(decim->buf, decim->width, decim->height, decim->stride,
                       im->buf, im->width, im->height, im->stride);
        return decim;
    } else if (factor == 3) {
        neon_decimate3(decim->buf, decim->width, decim->height, decim->stride,
                       im->buf, im->width, im->height, im->stride);
        return decim;
    } else if (factor == 4) {
        neon_decimate4(decim->buf, decim->width, decim->height, decim->stride,
                       im->buf, im->width, im->height, im->stride);
        return decim;
    }
#endif

    if (factor == 2) {
        for (int sy = 0; sy < sheight; sy++) {
            int sidx = sy * decim->stride;
            int idx = (sy*2)*im->stride;

            for (int sx = 0; sx < swidth; sx++) {
                uint32_t v = im->buf[idx] + im->buf[idx+1] +
                    im->buf[idx+im->stride] + im->buf[idx+im->stride + 1];
                decim->buf[sidx] = (v>>2);
                idx+=2;
                sidx++;
            }
        }
    } else if (factor == 3) {
        for (int sy = 0; sy < sheight; sy++) {
            int sidx = sy * decim->stride;
            int idx = (sy*3)*im->stride;

            for (int sx = 0; sx < swidth; sx++) {
                uint32_t v = im->buf[idx] + im->buf[idx+1] + im->buf[idx+2] +
                    im->buf[idx+im->stride] + im->buf[idx+im->stride + 1] + im->buf[idx+im->stride + 2] +
                    im->buf[idx+2*im->stride] + im->buf[idx+2*im->stride + 1];
                // + im->buf[idx+2*im->stride + 1];
                // deliberately omit lower right corner so there are exactly 8 samples...
                decim->buf[sidx] = (v>>3);
                idx+=3;
                sidx++;
            }
        }
    } else if (factor == 4) {
        for (int sy = 0; sy < sheight; sy++) {
            int sidx = sy * decim->stride;
            int idx = (sy*4)*im->stride;

            for (int sx = 0; sx < swidth; sx++) {
                uint32_t v = im->buf[idx] + im->buf[idx+1] + im->buf[idx+2] + im->buf[idx+3] +
                    im->buf[idx+im->stride] + im->buf[idx+im->stride + 1] + im->buf[idx+im->stride + 1] + im->buf[idx+im->stride + 2] +
                    im->buf[idx+2*im->stride] + im->buf[idx+2*im->stride + 1] + im->buf[idx+2*im->stride + 2] + im->buf[idx+2*im->stride + 3];

                decim->buf[sidx] = (v>>4);
                idx+=4;
                sidx++;
            }
        }
    } else {
        // XXX this isn't a very good decimation code.
#ifdef _MSC_VER
      uint32_t *row = (uint32_t *)malloc(swidth*sizeof *row);
#else
        uint32_t row[swidth];
#endif

        for (int y = 0; y < height; y+= factor) {
            memset(row, 0, sizeof(row));

            for (int dy = 0; dy < factor; dy++) {
                for (int x = 0; x < width; x++) {
                    row[x/factor] += im->buf[(y+dy)*im->stride + x];
                }
            }

            for (int x = 0; x < swidth; x++)
                decim->buf[(y/factor)*decim->stride + x] = row[x] / sq(factor);

        }

#ifdef _MSC_VER
        free(row);
#endif
    }

    return decim;
}

void image_u8_fill_line_max(image_u8_t *im, const image_u8_lut_t *lut, const float *xy0, const float *xy1)
{
    // what is the maximum distance that will result in drawing into our LUT?
    float max_dist2 = (lut->nvalues-1)/lut->scale;
    float max_dist = sqrt(max_dist2);

    // the orientation of the line
    double theta = atan2(xy1[1]-xy0[1], xy1[0]-xy0[0]);
    double v = sin(theta), u = cos(theta);

    int ix0 = iclamp((std::min)(xy0[0], xy1[0]) - max_dist, 0, im->width-1);
    int ix1 = iclamp((std::max)(xy0[0], xy1[0]) + max_dist, 0, im->width-1);

    int iy0 = iclamp((std::min)(xy0[1], xy1[1]) - max_dist, 0, im->height-1);
    int iy1 = iclamp((std::max)(xy0[1], xy1[1]) + max_dist, 0, im->height-1);

    // the line segment xy0---xy1 can be parameterized in terms of line coordinates.
    // We fix xy0 to be at line coordinate 0.
    float xy1_line_coord = (xy1[0]-xy0[0])*u + (xy1[1]-xy0[1])*v;

    float min_line_coord = (std::min)(0.f, xy1_line_coord);
    float max_line_coord = (std::max)(0.f, xy1_line_coord);

    for (int iy = iy0; iy <= iy1; iy++) {
        float y = iy+.5;

        for (int ix = ix0; ix <= ix1; ix++) {
            float x = ix+.5;

            // compute line coordinate of this pixel.
            float line_coord = (x - xy0[0])*u + (y - xy0[1])*v;

            // find point on line segment closest to our current pixel.
            if (line_coord < min_line_coord)
                line_coord = min_line_coord;
            else if (line_coord > max_line_coord)
                line_coord = max_line_coord;

            float px = xy0[0] + line_coord*u;
            float py = xy0[1] + line_coord*v;

            double dist2 = (x-px)*(x-px) + (y-py)*(y-py);

            // not in our LUT?
            int idx = dist2 * lut->scale;
            if (idx >= lut->nvalues)
                continue;

            uint8_t lut_value = lut->values[idx];
            uint8_t old_value = im->buf[iy*im->stride + ix];
            if (lut_value > old_value)
                im->buf[iy*im->stride + ix] = lut_value;
        }
    }
}
