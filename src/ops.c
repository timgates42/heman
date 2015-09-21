#include "image.h"
#include <assert.h>
#include <memory.h>
#include <kazmath/vec3.h>

heman_image* heman_ops_step(heman_image* hmap, HEMAN_FLOAT threshold)
{
    assert(hmap->nbands == 1);
    heman_image* result = heman_image_create(hmap->width, hmap->height, 1);
    int size = hmap->height * hmap->width;
    HEMAN_FLOAT* src = hmap->data;
    HEMAN_FLOAT* dst = result->data;
    for (int i = 0; i < size; ++i) {
        *dst++ = (*src++) >= threshold ? 1 : 0;
    }
    return result;
}

heman_image* heman_ops_sweep(heman_image* hmap)
{
    assert(hmap->nbands == 1);
    heman_image* result = heman_image_create(hmap->height, 1, 1);
    HEMAN_FLOAT* dst = result->data;
    const HEMAN_FLOAT* src = hmap->data;
    HEMAN_FLOAT invw = 1.0f / hmap->width;
    for (int y = 0; y < hmap->height; y++) {
        HEMAN_FLOAT acc = 0;
        for (int x = 0; x < hmap->width; x++) {
            acc += *src++;
        }
        *dst++ = (acc * invw);
    }
    return result;
}

static void copy_row(heman_image* src, heman_image* dst, int dstx, int y)
{
    int width = src->width;
    if (src->nbands == 1) {
        for (int x = 0; x < width; x++) {
            HEMAN_FLOAT* srcp = heman_image_texel(src, x, y);
            HEMAN_FLOAT* dstp = heman_image_texel(dst, dstx + x, y);
            *dstp = *srcp;
        }
        return;
    }
    for (int x = 0; x < width; x++) {
        HEMAN_FLOAT* srcp = heman_image_texel(src, x, y);
        HEMAN_FLOAT* dstp = heman_image_texel(dst, dstx + x, y);
        int nbands = src->nbands;
        while (nbands--) {
            *dstp++ = *srcp++;
        }
    }
}

heman_image* heman_ops_stitch_horizontal(heman_image** images, int count)
{
    assert(count > 0);
    int width = images[0]->width;
    int height = images[0]->height;
    int nbands = images[0]->nbands;
    for (int i = 1; i < count; i++) {
        assert(images[i]->width == width);
        assert(images[i]->height == height);
        assert(images[i]->nbands == nbands);
    }
    heman_image* result = heman_image_create(width * count, height, nbands);

#pragma omp parallel for
    for (int y = 0; y < height; y++) {
        for (int tile = 0; tile < count; tile++) {
            copy_row(images[tile], result, tile * width, y);
        }
    }

    return result;
}

heman_image* heman_ops_stitch_vertical(heman_image** images, int count)
{
    assert(count > 0);
    int width = images[0]->width;
    int height = images[0]->height;
    int nbands = images[0]->nbands;
    for (int i = 1; i < count; i++) {
        assert(images[i]->width == width);
        assert(images[i]->height == height);
        assert(images[i]->nbands == nbands);
    }
    heman_image* result = heman_image_create(width, height * count, nbands);
    int size = width * height * nbands;
    HEMAN_FLOAT* dst = result->data;
    for (int tile = 0; tile < count; tile++) {
        memcpy(dst, images[tile]->data, size * sizeof(float));
        dst += size;
    }
    return result;
}

heman_image* heman_ops_normalize_f32(
    heman_image* source, HEMAN_FLOAT minv, HEMAN_FLOAT maxv)
{
    heman_image* result =
        heman_image_create(source->width, source->height, source->nbands);
    HEMAN_FLOAT* src = source->data;
    HEMAN_FLOAT* dst = result->data;
    HEMAN_FLOAT scale = 1.0f / (maxv - minv);
    int size = source->height * source->width * source->nbands;
    for (int i = 0; i < size; ++i) {
        HEMAN_FLOAT v = (*src++ - minv) * scale;
        *dst++ = CLAMP(v, 0, 1);
    }
    return result;
}

heman_image* heman_ops_laplacian(heman_image* heightmap)
{
    assert(heightmap->nbands == 1);
    int width = heightmap->width;
    int height = heightmap->height;
    heman_image* result = heman_image_create(width, height, 1);
    int maxx = width - 1;
    int maxy = height - 1;

#pragma omp parallel for
    for (int y = 0; y < height; y++) {
        int y1 = MIN(y + 1, maxy);
        HEMAN_FLOAT* dst = result->data + y * width;
        for (int x = 0; x < width; x++) {
            int x1 = MIN(x + 1, maxx);
            HEMAN_FLOAT p = *heman_image_texel(heightmap, x, y);
            HEMAN_FLOAT px = *heman_image_texel(heightmap, x1, y);
            HEMAN_FLOAT py = *heman_image_texel(heightmap, x, y1);
            *dst++ = (p - px) * (p - px) + (p - py) * (p - py);
        }
    }

    return result;
}

void heman_ops_accumulate(heman_image* dst, heman_image* src)
{
    assert(dst->nbands == src->nbands);
    assert(dst->width == src->width);
    assert(dst->height == src->height);
    int size = dst->height * dst->width;
    HEMAN_FLOAT* sdata = src->data;
    HEMAN_FLOAT* ddata = dst->data;
    for (int i = 0; i < size; ++i) {
        *ddata++ += (*sdata++);
    }
}

heman_image* heman_ops_sobel(heman_image* img, heman_color edge_color)
{
    int width = img->width;
    int height = img->height;
    assert(img->nbands == 3);
    heman_image* result = heman_image_create(width, height, 3);
    heman_image* gray = heman_color_to_grayscale(img);

    kmVec3 edge_rgb;
    edge_rgb.x = 0;
    edge_rgb.y = 0;
    edge_rgb.z = 0;

#pragma omp parallel for
    for (int y = 0; y < height; y++) {
        kmVec3* dst = (kmVec3*) result->data + y * width;
        const kmVec3* src = (kmVec3*) img->data + y * width;
        for (int x = 0; x < width; x++) {
            int xm1 = MAX(x - 1, 0);
            int xp1 = MIN(x + 1, width - 1);
            int ym1 = MAX(y - 1, 0);
            int yp1 = MIN(y + 1, height - 1);
            HEMAN_FLOAT t00 = *heman_image_texel(gray, xm1, ym1);
            HEMAN_FLOAT t10 = *heman_image_texel(gray, x, ym1);
            HEMAN_FLOAT t20 = *heman_image_texel(gray, xp1, ym1);
            HEMAN_FLOAT t01 = *heman_image_texel(gray, xm1, 0);
            HEMAN_FLOAT t21 = *heman_image_texel(gray, xp1, 0);
            HEMAN_FLOAT t02 = *heman_image_texel(gray, xm1, yp1);
            HEMAN_FLOAT t12 = *heman_image_texel(gray, x, yp1);
            HEMAN_FLOAT t22 = *heman_image_texel(gray, xp1, yp1);
            HEMAN_FLOAT gx = t00 + 2.0 * t01 + t02 - t20 - 2.0 * t21 - t22;
            HEMAN_FLOAT gy = t00 + 2.0 * t10 + t20 - t02 - 2.0 * t12 - t22;
            HEMAN_FLOAT is_edge = gx * gx + gy * gy > 1e-5;
            kmVec3Lerp(dst++, src++, &edge_rgb, is_edge);
        }
    }

    heman_image_destroy(gray);
    return result;
}
