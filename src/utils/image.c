#include <gd.h>
#include <utils/image.h>
#include <utils/logger.h>


static gdImagePtr imageLoad(const char *name) {
    FILE *fp;
    gdImagePtr im;
    fp = fopen(name, "rb");
    if (!fp) {
        fprintf(stderr, "Can't open image file\n");
        return NULL;
    }
    im = gdImageCreateFromTga(fp);
    fclose(fp);
    return im;
}

static int imageSave(gdImagePtr im, const char *name) {
    FILE *fp;
    fp = fopen(name, "wb");
    if (!fp) {
        DEBUG_FUNCTION_LINE("Can't open file '%s' for writing", name);
        return -1;
    }

    const char *lastDot = strrchr(name, '.');
    if (lastDot != NULL) {
        const char *extension = lastDot + 1;

        if (strcmp(extension, "png") == 0)
            gdImagePng(im, fp);
        if (strcmp(extension, "gif") == 0)
            gdImageGif(im, fp);
        else if (strcmp(extension, "jpg") == 0)
            gdImageJpeg(im, fp, 85);
        else if (strcmp(extension, "bmp") == 0)
            gdImageBmp(im, fp, 1);
        else
            DEBUG_FUNCTION_LINE("Extension '%s' not recognized in '%s'", extension, name);
    } else
        DEBUG_FUNCTION_LINE("Extension not found for '%s'", name);

    fclose(fp);
    return 0;
}

int imageConvert(const char *inName, const char *outName) {
    gdImagePtr im = imageLoad(inName);
    if (im == NULL)
        return -1;

    int ret = imageSave(im, outName);
    gdImageDestroy(im);

    return ret;
}
