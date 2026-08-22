/****************************************************************************
 * NCSA Mosaic for the X Window System                                      *
 * Software Development Group                                               *
 * National Center for Supercomputing Applications                          *
 * University of Illinois at Urbana-Champaign                               *
 * 605 E. Springfield, Champaign IL 61820                                   *
 * mosaic@ncsa.uiuc.edu                                                     *
 *                                                                          *
 * Copyright (C) 1993, Board of Trustees of the University of Illinois      *
 *                                                                          *
 * NCSA Mosaic software, both binary and source (hereafter, Software) is    *
 * copyrighted by The Board of Trustees of the University of Illinois       *
 * (UI), and ownership remains with the UI.                                 *
 *                                                                          *
 * The UI grants you (hereafter, Licensee) a license to use the Software    *
 * for academic, research and internal business purposes only, without a    *
 * fee.  Licensee may distribute the binary and source code (if released)   *
 * to third parties provided that the copyright notice and this statement   *
 * appears on all copies and that no charge is associated with such         *
 * copies.                                                                  *
 *                                                                          *
 * Licensee may make derivative works.  However, if Licensee distributes    *
 * any derivative work based on or derived from the Software, then          *
 * Licensee will (1) notify NCSA regarding its distribution of the          *
 * derivative work, and (2) clearly notify users that such derivative       *
 * work is a modified version and not the original NCSA Mosaic              *
 * distributed by the UI.                                                   *
 *                                                                          *
 * Any Licensee wishing to make commercial use of the Software should       *
 * contact the UI, c/o NCSA, to negotiate an appropriate license for such   *
 * commercial use.  Commercial use includes (1) integration of all or       *
 * part of the source code into a product for sale or license by or on      *
 * behalf of Licensee to third parties, or (2) distribution of the binary   *
 * code or source code to third parties that need it to utilize a           *
 * commercial product sold or licensed by or on behalf of Licensee.         *
 *                                                                          *
 * UI MAKES NO REPRESENTATIONS ABOUT THE SUITABILITY OF THIS SOFTWARE FOR   *
 * ANY PURPOSE.  IT IS PROVIDED "AS IS" WITHOUT EXPRESS OR IMPLIED          *
 * WARRANTY.  THE UI SHALL NOT BE LIABLE FOR ANY DAMAGES SUFFERED BY THE    *
 * USERS OF THIS SOFTWARE.                                                  *
 *                                                                          *
 * By using or copying this Software, Licensee agrees to abide by the       *
 * copyright law and all other applicable laws of the U.S. including, but   *
 * not limited to, export control laws, and the terms of this license.      *
 * UI shall have the right to terminate this license immediately by         *
 * written notice upon Licensee's breach of, or non-compliance with, any    *
 * of its terms.  Licensee may be held legally responsible for any          *
 * copyright infringement that is caused or encouraged by Licensee's        *
 * failure to abide by the terms of this license.                           *
 *                                                                          *
 * Comments and questions are welcome and can be sent to                    *
 * mosaic-x@ncsa.uiuc.edu.                                                  *
 ****************************************************************************/

/* Author: DXP

 A lot of this is copied from the PNGLIB file example.c

 Modified:

    August   1995 - Glenn Randers-Pehrson <glennrp@arl.mil>
                    Changed dithering to use a 6x6x6 color cube.

    March 21 1996 - DXP
                    Fixed some interlacing problems.

*/

#include "config.h"
#ifdef HAVE_PNG

#include <stdio.h>
#include <X11/Intrinsic.h>

#include "mosaic.h"
#include "readPNG.h"

#include <setjmp.h>

#define MAX(x,y)  (((x) > (y)) ? (x) : (y))
#define PNG_BYTES_TO_CHECK 4

#ifndef DISABLE_TRACE
extern int srcTrace;
#endif

/* view background at decode time, 0..255 per channel (img.c stashes
   it before each ReadBitmap): transparent pixels composite onto this.
   -1 means unknown; fall back to the stock Motif grey. */
extern int png_view_bg_red, png_view_bg_green, png_view_bg_blue;

unsigned char *
ReadPNG(FILE *infile,int *width, int *height, XColor *colrs)
{
    int composited = 0;

    unsigned char *pixmap;
    unsigned char *p;
    png_byte *q;

    png_struct *png_ptr;
    png_info *info_ptr;

    double screen_gamma;

    png_byte *png_pixels=NULL, **row_pointers=NULL;
    int i, j, bit_depth, color_type, num_palette, interlace_type;
    int rowbytes, pixel_step;

    png_color std_color_cube[216];
    png_colorp palette;


    /* first check to see if its a valid PNG file. If not, return. */
    /* we assume that infile is a valid filepointer */
    {
        png_byte buf[PNG_BYTES_TO_CHECK];

        if (fread(buf, 1, PNG_BYTES_TO_CHECK, infile) != PNG_BYTES_TO_CHECK)
            return 0;

        if (png_sig_cmp(buf, 0, PNG_BYTES_TO_CHECK))
            return 0;
    }

    /* allocate the structures */
    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if(!png_ptr)
        return 0;

    /* initialize the structures */
    info_ptr = png_create_info_struct(png_ptr);
    if(!info_ptr) {
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        return 0;
    }

    /* Establish the setjmp return context for png_error to use. */
    if (setjmp(png_jmpbuf(png_ptr))) {

#ifndef DISABLE_TRACE
        if (srcTrace) {
            fprintf(stderr, "\n!!!libpng read error!!!\n");
        }
#endif

        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        /* the caller owns infile: it rewinds and tries other
           decoders after we fail, so never fclose it here */
        if(png_pixels != NULL)
            free((char *)png_pixels);

        if(row_pointers != NULL)
            free((png_byte **)row_pointers);

        return 0;
    }

#ifdef SAM_NO
    /* SWP -- Hopefully to fix cores on bad PNG files */
    png_set_message_fn(png_ptr,png_get_msg_ptr(png_ptr),NULL,NULL);
#endif

    /*png_read_init(png_ptr);*/

        /* set up the input control */
    png_init_io(png_ptr, infile);

        /* the signature check above already consumed these bytes */
    png_set_sig_bytes(png_ptr, PNG_BYTES_TO_CHECK);

        /* read the file information */
    png_read_info(png_ptr, info_ptr);

        /* setup other stuff using the fields of png_info. */

    png_get_IHDR(png_ptr, info_ptr, width, height, &bit_depth, &color_type, &interlace_type, NULL, NULL);

#ifndef DISABLE_TRACE
    if (srcTrace) {
        fprintf(stderr,"\n\nBEFORE\nwidth = %ls\n", width);
        fprintf(stderr,"height = %ls\n", height);
        fprintf(stderr,"bit depth = %d\n", bit_depth);
        fprintf(stderr,"color type = %d\n", color_type);
        fprintf(stderr,"interlace type = %d\n", interlace_type);
        /*
        fprintf(stderr,"compression type = %d\n", info_ptr->compression_type);
        fprintf(stderr,"filter type = %d\n", info_ptr->filter_type);
        fprintf(stderr,"num colors = %d\n",info_ptr->num_palette);
        fprintf(stderr,"rowbytes = %d\n", info_ptr->rowbytes);
        */
		
    }
#endif


        /* strip pixels in 16-bit images down to 8 bits */
    if (bit_depth == 16)
        png_set_strip_16(png_ptr);

        /* transparency (an alpha channel or a tRNS chunk): expand to
           8-bit RGB and composite onto the view background -- simply
           stripping the alpha shows whatever RGB hides under the
           transparent pixels, usually solid black */
    if ((color_type & PNG_COLOR_MASK_ALPHA) ||
        png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) {
        png_color_16 my_background;

        my_background.index = 0;
        my_background.red = (png_view_bg_red >= 0) ?
            (png_uint_16)png_view_bg_red : 0xbf;
        my_background.green = (png_view_bg_green >= 0) ?
            (png_uint_16)png_view_bg_green : 0xbf;
        my_background.blue = (png_view_bg_blue >= 0) ?
            (png_uint_16)png_view_bg_blue : 0xbf;
        my_background.gray = my_background.red;

        png_set_expand(png_ptr);
        if (!(color_type & PNG_COLOR_MASK_COLOR))
            png_set_gray_to_rgb(png_ptr);
        png_set_background(png_ptr, &my_background,
                           PNG_BACKGROUND_GAMMA_SCREEN, 0, 1.0);
        composited = 1;
    }

        /* If it is a color image then check if it has a palette. If not
           then dither the image to 256 colors, and make up a palette.
           A composited image is 8-bit RGB by now whatever it started
           as, and its file palette (if any) no longer matches the
           composited pixels, so it always takes the color-cube path. */
    if (composited ||
        color_type==PNG_COLOR_TYPE_RGB ||
        color_type==PNG_COLOR_TYPE_RGB_ALPHA) {

        if (composited ||
            png_get_PLTE(png_ptr, info_ptr, &palette, &num_palette) == 0) {

#ifndef DISABLE_TRACE
            if (srcTrace) {
                fprintf(stderr,"dithering (RGB->palette)...\n");
            }
#endif
                /* if there is is no valid palette, then we need to make
                   one up */
            for(i=0;i<216;i++) {
                    /* 255.0/5 = 51 */
                std_color_cube[i].red=(i%6)*51;
                std_color_cube[i].green=((i/6)%6)*51;
                std_color_cube[i].blue=(i/36)*51;
            }

                /* composited images: swap the cube entry nearest the
                   view background for the exact background color, so
                   fully transparent areas quantize to it and vanish
                   instead of showing as an off-shade rectangle */
            if (composited) {
                int ri, gi, bi;
                int br = (png_view_bg_red >= 0) ? png_view_bg_red : 0xbf;
                int bgr = (png_view_bg_green >= 0) ?
                    png_view_bg_green : 0xbf;
                int bb = (png_view_bg_blue >= 0) ? png_view_bg_blue : 0xbf;

                ri = (br + 25) / 51;
                gi = (bgr + 25) / 51;
                bi = (bb + 25) / 51;
                if (ri > 5) ri = 5;
                if (gi > 5) gi = 5;
                if (bi > 5) bi = 5;
                i = ri + 6 * gi + 36 * bi;
                std_color_cube[i].red = br;
                std_color_cube[i].green = bgr;
                std_color_cube[i].blue = bb;
            }

                /* this should probably be dithering to
                   Rdata.colors_per_inlined_image colors */
            png_set_quantize(png_ptr, std_color_cube,
                           216,
                           216, NULL, 1);

        } else {
#ifndef DISABLE_TRACE
            if (srcTrace) {
                fprintf(stderr,"dithering (RGB->file supplied palette)...\n");
            }
#endif

            png_uint_16p histogram = NULL;
            png_get_hIST(png_ptr, info_ptr, &histogram);
            png_set_quantize(png_ptr, palette,
                           num_palette,
                           get_pref_int(eCOLORS_PER_INLINED_IMAGE),
                           histogram, 1);

        }
    }

        /* PNG files pack pixels of bit depths 1, 2, and 4 into bytes as
           small as they can. This expands pixels to 1 pixel per byte, and
           if a transparency value is supplied, an alpha channel is
           built.*/
    if ((color_type == PNG_COLOR_TYPE_GRAY ||
         color_type == PNG_COLOR_TYPE_GRAY_ALPHA) && bit_depth < 8)
        /* scales the gray values to the full 0..255 range too */
        png_set_expand_gray_1_2_4_to_8(png_ptr);
    else if (bit_depth < 8)
        png_set_packing(png_ptr);


        /* have libpng handle the gamma conversion */

    if (get_pref_boolean(eUSE_SCREEN_GAMMA)) { /*SWP*/
        if (bit_depth != 16) {  /* temporary .. glennrp */
            screen_gamma=(double)(get_pref_float(eSCREEN_GAMMA));

#ifndef DISABLE_TRACE
            if (srcTrace) {
                fprintf(stderr,"screen gamma=%f\n",screen_gamma);
            }
#endif
            double image_gamma;
            if (png_get_gAMA(png_ptr, info_ptr, &image_gamma) != 0) {
#ifndef DISABLE_TRACE
                if (srcTrace) {
                    printf("setting gamma=%f\n", image_gamma);
                }
#endif
                png_set_gamma(png_ptr, screen_gamma, image_gamma);
            }
            else {
#ifndef DISABLE_TRACE
                if (srcTrace) {
                    fprintf(stderr,"setting gamma=%f\n",0.45455);
                }
#endif
                png_set_gamma(png_ptr, screen_gamma, 0.45455);
            }
        }
    }

    if (interlace_type)
        png_set_interlace_handling(png_ptr);

    png_read_update_info(png_ptr, info_ptr);

#ifndef DISABLE_TRACE
    if (srcTrace) {
        fprintf(stderr,"\n\nAFTER\nwidth = %ls\n", width);
        fprintf(stderr,"height = %ls\n", height);
        fprintf(stderr,"bit depth = %d\n", bit_depth);
        fprintf(stderr,"color type = %d\n", color_type);
        fprintf(stderr,"interlace type = %d\n", interlace_type);
        /*
        fprintf(stderr,"compression type = %d\n", info_ptr->compression_type);
        fprintf(stderr,"filter type = %d\n", info_ptr->filter_type);
        fprintf(stderr,"num colors = %d\n",info_ptr->num_palette);
        fprintf(stderr,"rowbytes = %d\n", info_ptr->rowbytes);
        */
    }
#endif

        /* allocate the pixel grid which we will need to send to
           png_read_image(). */
    rowbytes = png_get_rowbytes(png_ptr, info_ptr);
    if ((*width) <= 0 || (*height) <= 0 || rowbytes < (*width)) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return 0;
    }
    png_pixels = (png_byte *)malloc(rowbytes *
                                    (*height) * sizeof(png_byte));
    row_pointers = (png_byte **)malloc((*height) * sizeof(png_byte *));
    if (png_pixels == NULL || row_pointers == NULL) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        if (png_pixels != NULL)
            free((char *)png_pixels);
        if (row_pointers != NULL)
            free((png_byte **)row_pointers);
        return 0;
    }

    for (i=0; i < *height; i++)
        row_pointers[i] = png_pixels + i * rowbytes;


        /* FINALLY - read the darn thing. */
    png_read_image(png_ptr, row_pointers);


        /* now that we have the (transformed to 8-bit RGB) image, we have
           to copy the resulting palette to our colormap. */
    if (composited) {
            /* quantized to the 216 color cube above */
        for (i=0; i < 216; i++) {
            colrs[i].red = std_color_cube[i].red << 8;
            colrs[i].green = std_color_cube[i].green << 8;
            colrs[i].blue = std_color_cube[i].blue << 8;
            colrs[i].pixel = i;
            colrs[i].flags = DoRed|DoGreen|DoBlue;
        }
    } else if (color_type & PNG_COLOR_MASK_COLOR) {
        if (png_get_PLTE(png_ptr, info_ptr, &palette, &num_palette) != 0) {

            for (i=0; i < num_palette; i++) {
                colrs[i].red = palette[i].red << 8;
                colrs[i].green = palette[i].green << 8;
                colrs[i].blue = palette[i].blue << 8;
                colrs[i].pixel = i;
                colrs[i].flags = DoRed|DoGreen|DoBlue;
            }

        }
        else {
            for (i=0; i < 216; i++) {
                colrs[i].red = std_color_cube[i].red << 8;
                colrs[i].green = std_color_cube[i].green << 8;
                colrs[i].blue = std_color_cube[i].blue << 8;
                colrs[i].pixel = i;
                colrs[i].flags = DoRed|DoGreen|DoBlue;
            }
        }
    } else {
            /* grayscale image */

        for(i=0; i < 256; i++ ) {
            colrs[i].red = i << 8;
            colrs[i].green = i << 8;
            colrs[i].blue = i << 8;
            colrs[i].pixel = i;
            colrs[i].flags = DoRed|DoGreen|DoBlue;
        }
    }

        /* Now copy the pixel data from png_pixels to pixmap */

    pixmap = (png_byte *)malloc((*width) * (*height) * sizeof(png_byte));

    p = pixmap;

        /* rows may still carry more than one byte per pixel (an alpha
           channel libpng didn't strip, say); step over the extras */
    pixel_step = rowbytes / (*width);
    for(i=0; i<*height; i++) {
        q = row_pointers[i];
        for(j=0; j<*width; j++) {
            *p++ = *q; /*palette/gray index*/
            q += pixel_step;
        }
    }

    free((char *)png_pixels);
    free((png_byte **)row_pointers);

    /* clean up after the read, and free any memory allocated */
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    return pixmap;
}


#endif
