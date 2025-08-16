// patterns.c

// id stored on node, defines position, legs (2/3/4 strips) and pixel count
// - 0: 4 x NODE_NR LEDs
// len used for UDP packet length, including header
// mapping from logical to physical channels, 0x1234 = identical
typedef struct {
    int fd;
    uint16_t id, len, mapping, cnt;
    uint8_t *pkt;
} NODE_T;

void createPkt(NODE_T* node, uint16_t frame);
void setPattern(uint16_t type, uint16_t mode);

#define NODE_NR 18
#define LED_CNT 200
#define PAT_NR 3

// eof
