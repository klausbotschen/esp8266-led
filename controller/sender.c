// send UDP packets with LED data

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <termios.h>
#include <time.h> 
#include <pthread.h>
#include <stdatomic.h>
#include <ctype.h>
#include <errno.h>

#include "adafruit.h"
#include "patterns.h"

volatile int running = 1;
#define PKTLEN 1472
#define PORT 5700

in_addr_t nodeip[NODE_NR];
uint16_t cfgBrightness = 3, cfgPattern = 2;

volatile uint16_t frame, nodecnt = 0;
NODE_T *nodes;
pthread_cond_t sendSig, syncSig, pixelSig;
atomic_int nodeReady = ATOMIC_VAR_INIT(0);

#define SOCKLEN sizeof(struct sockaddr_in)

// ######################################################################

void add_ms(struct timespec *time, long milliseconds) {
    time->tv_nsec += milliseconds * 1000000L;
    if (time->tv_nsec >= 1000000000L) {
        time->tv_sec += 1;
        time->tv_nsec -= 1000000000L;
    }
}
// compute pixel patterns over all strips
void* pixelLoop(void* arg) {
    pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
    pthread_mutex_t mutex;
    uint16_t c;
    struct timespec target_time;

    frame = 0;
	pthread_mutex_init (&mutex, NULL);
    pthread_mutex_lock (&mutex);
    while (running) {
        clock_gettime(CLOCK_REALTIME, &target_time);
        add_ms (&target_time, 33);
        c = nodecnt;
        createPkt(nodes, frame);
        frame++;
        while (pthread_cond_timedwait(&cond, &mutex, &target_time) == EINTR);
        // set node counter
        if (c) {
            atomic_store(&nodeReady, c);
            pthread_cond_broadcast (&sendSig);
        }
    }
    return NULL;
}

// one thread for each node, send pixel data to it
void* sendLoop(void* arg) {
    uint16_t l;
    uint8_t *p;
    NODE_T *node = (NODE_T*) arg;
    pthread_mutex_t mutex;

	pthread_mutex_init (&mutex, NULL);
    pthread_mutex_lock (&mutex);

    while (running) {
        pthread_cond_wait (&sendSig, &mutex);
        p = node->pkt;
        l = node->len;
        while (node->cnt--) {
            send(node->fd, p, l, 0);
            p += l;
        }
        // when all sent, sync and start next drawing cycle
        if (atomic_fetch_sub(&nodeReady, 1) == 1) pthread_cond_signal (&syncSig);
    }
    close(node->fd);
    return NULL;
}

// broadcast UDP sync signal to all nodes
void* syncLoop(void* arg) {
    int fd;
	int l, on = 1;
    struct sockaddr_in addr;
    char buffer[32];
    pthread_mutex_t mutex;

    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        return NULL;
    }
    if (setsockopt (fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on)) < 0) {
        perror("Broadcast flag failed");
    }
 	pthread_mutex_init (&mutex, NULL);
    pthread_mutex_lock (&mutex);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_BROADCAST;
    while (running) {
        pthread_cond_wait (&syncSig, &mutex);
        l = snprintf(buffer, sizeof(buffer), "s%04x", frame);
        // ignore errors, nodes will reconnect
        sendto(fd, buffer, l, 0, (const struct sockaddr*)&addr, SOCKLEN);
    }
    close(fd);
    return NULL;
}

// look if node is already registered, if not, add a new entry
void addNode (char *buf, struct in_addr ip) {
    uint32_t ctrid;
    uint16_t i, f=0;
    NODE_T *node;
    struct sockaddr_in addr;
    pthread_t thread;

    for (i=0; i<NODE_NR; i++) {
        if (!f && !nodeip[i]) f = i+1;
        if (nodeip[i] == ip.s_addr) return; // already registered
    }
    if (!f) { printf ("node list full\n"); return; }
    f--;
    node = nodes + f;
    buf++;
    printf("New node: %s <= %s\n", buf, inet_ntoa(ip));
    nodeip[f] = ip.s_addr;
    ctrid = strtol(buf, NULL, 16);
    node->id = ctrid >> 16;
    node->len = 3*LED_CNT+2; // 3 byte LED_CNT pixel + header
    node->mapping = ctrid & 0xffff;
    node->pkt = malloc(node->len*4);
    nodecnt++;
    // create socket
    if ((node->fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation");
        return;
    }
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr = ip;
    if (connect(node->fd, (struct sockaddr*) &addr, SOCKLEN) == -1) {
        perror("Socket connect");
    }
    // spin up a new thread for this node
    if (pthread_create(&thread, NULL, sendLoop, node) != 0) {
        perror("Failed to create thread");
        return;
    }
}

// process alive packets "a<id>"
void* receiveLoop(void* arg) {
    struct sockaddr_in addr;
    uint16_t pktsize;
    char buffer[64];
    int fd;

    memset(&addr, 0, SOCKLEN);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT+1);
    addr.sin_addr.s_addr = INADDR_ANY;

    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    if (bind(fd, (struct sockaddr*)&addr, SOCKLEN) < 0) {
        perror("Bind failed");
        close(fd);
        pthread_exit(NULL);
    }
    while (running) {
        socklen_t len = SOCKLEN;
        pktsize = recvfrom(fd, buffer, sizeof(buffer) - 1, 0,
                            (struct sockaddr*)&addr, &len);
        if (pktsize > 0) {
            buffer[pktsize] = '\0';
            addNode (buffer, addr.sin_addr);
        }
    }
    close(fd);
    return NULL;
}

void sendControlCmd(int fd, char *b, uint16_t l, int ix) {
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = nodeip[ix];
    sendto(fd, b, l, 0, (const struct sockaddr*)&addr, SOCKLEN);
}

void dispNodelist(void) {
    uint16_t i;
    NODE_T *node;
    struct in_addr ia;

    for (i=0; i<NODE_NR; i++) {
        node = nodes+i;
        if (!node->fd) continue;
        ia.s_addr = nodeip[i];
        printf ("%c %15s  id %04X  map %04X\n",
            'A'+i, inet_ntoa(ia), node->id, node->mapping);
    }
}

void editLoop(int fd) {
    int ch;
    uint16_t level=0, pos, ix, len, es=0;
    char nid[5], map[5];
    NODE_T *node;
    char buf[64];

    setPattern(cfgPattern, 0);
    while (running) {
        ch = getchar();
        if (ch == 0x1b) { es++; continue; }
        if (es == 1 && ch == '[') { es++; continue; }
        ch = toupper(ch);
        switch (level) {
            case 0: // root level
            switch (ch) {
                case 'X': running = 0; break;
                case 'L': dispNodelist(); printf("?> "); level=1; break;
                case 'B': printf ("brightness: %2i", cfgBrightness); level=4; break;
                case 'P': printf ("pattern: %2i", cfgPattern); level=5; break;
            }
            break;
            case 1: // select node, start id change
            ix = ch - 'A';
            if (isalpha(ch) && ix < NODE_NR) {
                node = nodes + ix;
                if (node->fd) {
                    setPattern(0, ix);
                    snprintf (nid, sizeof(nid), "%04X", node->id);
                    printf ("\r%c> id = %04X\x08\x08\x08\x08", ch, node->id);
                    pos = 0;
                    level = 2;
                }
            } else {
                printf ("\r\33[2K");
                level=0;
                setPattern(cfgPattern, 0);
            }
            break;
            case 2: // controller id
            if (ch == '\x7f' && pos) { printf("\x08"); pos--; }
            if (isxdigit(ch) && pos < sizeof(nid)) {
                putchar(ch);
                nid[pos++] = ch;
            }
            if (ch == '\x0a') {
                while (pos < sizeof(nid)) putchar(nid[pos++]);
                snprintf (map, sizeof(map), "%04X", node->mapping);
                printf (" map = %04X\x08\x08\x08\x08", node->mapping);
                pos = 0;
                level = 3;
            }
            break;
            case 3: // mapping
            if (ch == '\x7f' && pos) { printf("\x08"); pos--; }
            if (isxdigit(ch) && pos < sizeof(map)) {
                putchar(ch);
                map[pos++] = ch;
            }
            if (ch == '\x0a') {
                len = snprintf (buf, sizeof(buf), "ci%s%s", nid, map);
                sendControlCmd (fd, buf, len, ix);
                node->id = strtol(nid, NULL, 16);
                node->mapping = strtol(map, NULL, 16);
                printf ("\r\n?> ");
                pos = 0;
                level = 1;
            }
            break;
            case 4: // brightness 
            switch (ch) {
                case 'D': // ESC [ D cursor left
                case '-':
                if (cfgBrightness) cfgBrightness--;
                printf ("\rbrightness: %2i", cfgBrightness);
                setBrightness(cfgBrightness);
                break;
                case 'C': // ESC [ C cursor right
                case '+':
                if (cfgBrightness<15) cfgBrightness++;
                printf ("\rbrightness: %2i", cfgBrightness);
                setBrightness(cfgBrightness);
                break;
                default: printf ("\r\33[2K"); level=0; break;
            }
            break;
            case 5: // pattern 
            switch (ch) {
                case 'D': // ESC [ D cursor left
                case '-':
                if (cfgPattern>1) cfgPattern--;
                printf ("\rpattern: %2i", cfgPattern);
                setPattern(cfgPattern, 0);
                break;
                case 'C': // ESC [ C cursor right
                case '+':
                if (cfgPattern<PAT_NR) cfgPattern++;
                printf ("\rpattern: %2i", cfgPattern);
                setPattern(cfgPattern, 0);
                break;
                default: printf ("\r\33[2K"); level=0; break;
            }
            break;
        }
    }
}

// read parameters
int main(int argc, char* argv[]) {
    pthread_t listener, pixeldraw, syncer;
    int fd;
    struct termios ts;

    nodes = malloc(sizeof(NODE_T) * NODE_NR);
    memset (nodes, 0, sizeof(NODE_T) * NODE_NR);
    memset (nodeip, 0, sizeof(nodeip));
    // control command socket
    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    pthread_cond_init (&sendSig, NULL);
    pthread_cond_init (&syncSig, NULL);
    pthread_cond_init (&pixelSig, NULL);
    if (pthread_create(&listener, NULL, receiveLoop, NULL) != 0) {
        perror("Failed to create receiveLoop");
        exit(EXIT_FAILURE);
    }
    if (pthread_create(&pixeldraw, NULL, pixelLoop, NULL) != 0) {
        perror("Failed to create pixelLoop");
        exit(EXIT_FAILURE);
    }
    if (pthread_create(&syncer, NULL, syncLoop, NULL) != 0) {
        perror("Failed to create syncLoop");
        exit(EXIT_FAILURE);
    }
    // non-canoncal, no echo => no line edit
    tcgetattr(STDIN_FILENO, &ts);
    ts.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &ts);
    
    editLoop(fd);

    pthread_cond_destroy (&sendSig);
    pthread_cond_destroy (&syncSig);
    pthread_cond_destroy (&pixelSig);
    printf("\nStopping threads...");
    pthread_join(pixeldraw, NULL);
    printf(" done.\n");
    // canonical mode, echo
    ts.c_lflag |= (ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &ts);
    return 0;
}
