//
// !!! Rebol code frequently uses uint32_t access for pixel data instead of
// going byte-by-byte.  This creates problems with endianness, and opens the
// doors to potential problems with strict-aliasing.  See notes:
//
// https://github.com/metaeducation/ren-c/issues/756
//
// It's not a particular priority for the interpreter core to address this
// detail.  However, it is kept at the moment for those who may try adapting
// old graphics code to the new codebase.
//

// Global pixel format setup for REBOL image!, image loaders, color handling,
// tuple! conversions etc.  The graphics compositor code should rely on this
// setting(and do specific conversions if needed)
//
// TO_RGBA_COLOR always returns 32bit RGBA value, converts R,G,B,A
// components to native RGBA order
//
// TO_PIXEL_COLOR must match internal image! datatype byte order, converts
// R,G,B,A components to native image format
//
// C_R, C_G, C_B, C_A Maps color components to correct byte positions for
// image! datatype byte order

#ifdef ENDIAN_BIG // ARGB pixel format on big endian systems
    #define TO_RGBA_COLOR(r,g,b,a) \
        (((uint32_t)(r)) << 24 \
        | ((uint32_t)(g)) << 16 \
        | ((uint32_t)(b)) << 8 \
        | ((uint32_t)(a)))

    #define C_A 0
    #define C_R 1
    #define C_G 2
    #define C_B 3

    #define TO_PIXEL_COLOR(r,g,b,a) \
        (((uint32_t)(a)) << 24 \
        | ((uint32_t)(r)) << 16 \
        | ((uint32_t)(g)) << 8 \
        | ((uint32_t)(b)))
#else
    #define TO_RGBA_COLOR(r,g,b,a) \
        (((uint32_t)(a)) << 24 \
        | ((uint32_t)(b)) << 16 \
        | ((uint32_t)(g)) << 8 \
        | ((uint32_t)(r)))

    #ifdef TO_ANDROID_ARM // RGBA pixel format on Android
        #define C_R 0
        #define C_G 1
        #define C_B 2
        #define C_A 3

        #define TO_PIXEL_COLOR(r,g,b,a) \
            (((uint32_t)(a)) << 24 \
            | ((uint32_t)(b)) << 16 \
            | ((uint32_t)(g)) << 8 \
            | ((uint32_t)(r)))

    #else // BGRA pixel format on Windows
        #define C_B 0
        #define C_G 1
        #define C_R 2
        #define C_A 3

        #define TO_PIXEL_COLOR(r,g,b,a) \
            (((uint32_t)(a)) << 24 \
            | ((uint32_t)(r)) << 16 \
            | ((uint32_t)(g)) << 8 \
            | ((uint32_t)(b)))
    #endif
#endif
