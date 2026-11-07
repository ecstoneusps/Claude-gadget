// Desktop preview of the face engine. Renders frames with a tiny software
// rasterizer and writes them as raw RGB so make_preview.py can build a GIF
// and a contact sheet. Build & run via tools/preview/make_preview.sh.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <vector>

#include "Face.h"
#include "SoftCanvas.h"



int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s anim|sheet <out.rgb>\n", argv[0]);
        return 1;
    }
    const int W = 320, H = 240;
    FILE* out = fopen(argv[2], "wb");
    if (!out) return 1;

    const int N = (int)Expression::Count;
    if (strcmp(argv[1], "sheet") == 0) {
        // One settled, non-blinking frame per expression, eyes centered.
        for (int i = 0; i < N; ++i) {
            Face face;
            face.setAutoBlink(false);
            face.setLookAt(0, 0);
            face.setExpression((Expression)i);
            for (int t = 0; t < 60; ++t) face.update(33);
            SoftCanvas c(W, H);
            face.draw(c);
            c.writeRGB(out);
            printf("%s\n", expressionName((Expression)i));
        }
    } else {
        // Animated tour: each expression for ~2s, with a "talking" segment.
        Face face;
        face.setSeed(42);
        const int fps = 25, dt = 1000 / fps;
        int frames = 0;
        for (int i = 0; i < N; ++i) {
            face.setExpression((Expression)i);
            for (int f = 0; f < fps * 2; ++f) {
                bool talking = (Expression)i == Expression::Neutral && f > fps / 2;
                face.setTalkLevel(talking ? 0.5f + 0.5f * sinf(f * 1.7f) * sinf(f * 0.6f) : 0);
                face.update(dt);
                SoftCanvas c(W, H);
                face.draw(c);
                c.writeRGB(out);
                ++frames;
            }
        }
        printf("%d frames\n", frames);
    }
    fclose(out);
    return 0;
}
