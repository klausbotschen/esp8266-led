// pattern generator

#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "adafruit.h"
#include "patterns.h"

uint16_t type=1, mode=0;

void setPattern(uint16_t t, uint16_t m) {
    type = t;
    mode = m;
}

// pattern 0: identify node location and wired LED strips
// mode = selected node index
void testPattern(NODE_T* nodes, uint16_t frame) {
    uint16_t i = 0;
    uint8_t* p;

    while (i < NODE_NR) {
        NODE_T* node = nodes + i++;
        if (!node->fd) continue;
        p = node->pkt;
        // focus on the selected node
        if (mode == i-1) {
            memset(p, 0, node->len*4);
            *p++ = 0x01;
            *p++ = frame;
            setPixels (p);
            setPixelColor(0, 0x00ffffff);
            p += node->len-2;
            *p++ = 0x02;
            *p++ = frame;
            setPixels (p);
            setPixelColor(1, 0x00ffffff);
            p += node->len-2;
            *p++ = 0x04;
            *p++ = frame;
            setPixels (p);
            setPixelColor(2, 0x00ffffff);
            p += node->len-2;
            *p++ = 0x08;
            *p++ = frame;
            setPixels (p);
            setPixelColor(3, 0x00ffffff);
            node->cnt = 4;
        } else {
            memset(p, 0, node->len);
            *p++ = 0x0f;
            *p++ = frame;
            node->cnt = 1;
        }
    }
}

void runningDots(NODE_T* nodes, uint16_t frame) {
    uint16_t i=0, col;
    uint8_t* p;
    static uint16_t pix=0;

    while (i < NODE_NR) {
        NODE_T* node = nodes+i++;
        if (!node->fd) continue;
        p = node->pkt;
        col = frame * 256;
        if (pix >= 100) pix = 0;
        memset(p, 0, node->len*4);
        *p++ = 0x01;
        *p++ = frame;
        setPixels (p);
        setPixelColor(pix, ColorHSV(col, 255, 255));
        p += node->len-2;
        *p++ = 0x02;
        *p++ = frame;
        setPixels (p);
        setPixelColor(LED_CNT-1-pix, ColorHSV(col, 255, 255));
        p += node->len-2;
        *p++ = 0x04;
        *p++ = frame;
        setPixels (p);
        setPixelColor(pix, ColorHSV(col, 255, 255));
        p += node->len-2;
        *p++ = 0x08;
        *p++ = frame;
        setPixels (p);
        setPixelColor(LED_CNT-1-pix, ColorHSV(col, 255, 255));
        node->cnt = 4;
    }
    pix++;
}

// multiple synchronious wandering trains in changing colors
void trains(NODE_T* nodes, uint16_t frame) {
    uint16_t nix=0, id, fid, i;
    uint32_t col, im;
    uint8_t* p;
    // position, size, speed (step size relative to 2^16)
    static uint16_t pix=0, psz=10, pstep=800;

    while (nix < NODE_NR) {
        NODE_T* node = nodes+nix;
        nix++;
        if (!node->fd) continue;
        id = node->id & 0x00ff;
        p = node->pkt;
        fid = frame + id * 50;
        col = ColorHSV(fid * 256, 255, 255);
        memset(p, 0, node->len);
        *p++ = 0x0f;
        *p++ = frame;
        setPixels (p);
        im = pix * LED_CNT / 65536;
        for (i=0; i < psz; i++) {
            if (im >= LED_CNT) im = 0;
            setPixelColor(im++, col);
        }
        node->cnt = 1;
    }
    pix += pstep;
}

#define SPOTS_NR 25
// many random spots in random colors
void spotflash(NODE_T* nodes, uint16_t frame) {
    uint16_t nix=0, i;
    uint32_t col, pix;
    uint8_t* p;

    while (nix < NODE_NR) {
        NODE_T* node = nodes+nix;
        nix++;
        if (!node->fd) continue;
        p = node->pkt;
        memset(p, 0, node->len*3);
        *p++ = 0x02;
        *p++ = frame;
        setPixels (p);
        for (i=0; i < SPOTS_NR; i++) {
            pix = random() % 180;
            col = ColorHSV(random() % 0xffff, 255, 255);
            setPixelColor(pix, col);
        }
        p += node->len-2;
        *p++ = 0x04;
        *p++ = frame;
        setPixels (p);
        for (i=0; i < SPOTS_NR; i++) {
            pix = random() % 180;
            col = ColorHSV(random() % 0xffff, 255, 255);
            setPixelColor(pix, col);
        }
        p += node->len-2;
        *p++ = 0x08;
        *p++ = frame;
        setPixels (p);
        for (i=0; i < SPOTS_NR; i++) {
            pix = random() % 180;
            col = ColorHSV(random() % 0xffff, 255, 255);
            setPixelColor(pix, col);
        }
        node->cnt = 3;
    }
}


// create 1..4 instances of pixel data of same length
//  // 0x1F = all 4 + show
void createPkt(NODE_T* node, uint16_t frame) {
    switch (type) {
        case 0: testPattern (node, frame); break;
        case 1: runningDots (node, frame); break;
        case 2: trains (node, frame); break;
        case 3: spotflash (node, frame); break;
    }
}

// eof
