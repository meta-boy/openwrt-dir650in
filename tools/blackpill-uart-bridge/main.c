/*
 * Minimal USB CDC-ACM <-> USART1 bridge for STM32F401 "Black Pill" (25 MHz HSE).
 * USART1: PA9 = TX, PA10 = RX. Baud follows the host's SET_LINE_CODING (8N1).
 * Bare metal, polled USB OTG FS, no external libraries.
 */
#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(a))

/* ---- RCC / FLASH / GPIO / USART / NVIC ---- */
#define RCC_CR       REG(0x40023800)
#define RCC_PLLCFGR  REG(0x40023804)
#define RCC_CFGR     REG(0x40023808)
#define RCC_AHB1ENR  REG(0x40023830)
#define RCC_AHB2ENR  REG(0x40023834)
#define RCC_APB2ENR  REG(0x40023844)
#define FLASH_ACR    REG(0x40023C00)

#define GPIOA_MODER   REG(0x40020000)
#define GPIOA_OSPEEDR REG(0x40020008)
#define GPIOA_PUPDR   REG(0x4002000C)
#define GPIOA_AFRH    REG(0x40020024)

#define USART1_SR  REG(0x40011000)
#define USART1_DR  REG(0x40011004)
#define USART1_BRR REG(0x40011008)
#define USART1_CR1 REG(0x4001100C)

#define NVIC_ISER1 REG(0xE000E104)

#define PCLK2 84000000u

/* ---- USB OTG FS ---- */
#define OTG 0x50000000u
#define GAHBCFG   REG(OTG + 0x008)
#define GUSBCFG   REG(OTG + 0x00C)
#define GRSTCTL   REG(OTG + 0x010)
#define GINTSTS   REG(OTG + 0x014)
#define GINTMSK   REG(OTG + 0x018)
#define GRXSTSP   REG(OTG + 0x020)
#define GRXFSIZ   REG(OTG + 0x024)
#define DIEPTXF0  REG(OTG + 0x028)
#define GCCFG     REG(OTG + 0x038)
#define DIEPTXF(n) REG(OTG + 0x104 + ((n) - 1) * 4)
#define DCFG      REG(OTG + 0x800)
#define DCTL      REG(OTG + 0x804)
#define DIEPCTL(n)  REG(OTG + 0x900 + (n) * 0x20)
#define DIEPINT(n)  REG(OTG + 0x908 + (n) * 0x20)
#define DIEPTSIZ(n) REG(OTG + 0x910 + (n) * 0x20)
#define DTXFSTS(n)  REG(OTG + 0x918 + (n) * 0x20)
#define DOEPCTL(n)  REG(OTG + 0xB00 + (n) * 0x20)
#define DOEPINT(n)  REG(OTG + 0xB08 + (n) * 0x20)
#define DOEPTSIZ(n) REG(OTG + 0xB10 + (n) * 0x20)
#define PCGCCTL   REG(OTG + 0xE00)
#define FIFO(n)   REG(OTG + 0x1000 + (n) * 0x1000)

#define EPENA (1u << 31)
#define EPDIS (1u << 30)
#define SD0PID (1u << 28)
#define SNAK  (1u << 27)
#define CNAK  (1u << 26)
#define STALL (1u << 21)
#define USBAEP (1u << 15)

#define GINT_RXFLVL (1u << 4)
#define GINT_USBRST (1u << 12)
#define GINT_ENUMDNE (1u << 13)

/* ---- ring buffers ---- */
#define RB 2048
typedef struct { volatile uint16_t h, t; uint8_t b[RB]; } ring_t;
static ring_t u2h; /* UART -> host */
static ring_t h2u; /* host -> UART */
static inline int rb_count(ring_t *r) { return (uint16_t)(r->h - r->t) % RB; }
static inline int rb_space(ring_t *r) { return RB - 1 - rb_count(r); }
static inline void rb_put(ring_t *r, uint8_t c) {
    uint16_t n = (r->h + 1) % RB;
    if (n == r->t) r->t = (r->t + 1) % RB; /* overwrite oldest */
    r->b[r->h] = c; r->h = n;
}
static inline int rb_get(ring_t *r) {
    if (r->h == r->t) return -1;
    uint8_t c = r->b[r->t]; r->t = (r->t + 1) % RB; return c;
}

void *memset(void *d, int c, unsigned n) { uint8_t *p = d; while (n--) *p++ = c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { uint8_t *p = d; const uint8_t *q = s; while (n--) *p++ = *q++; return d; }

static void delay(volatile uint32_t n) { while (n--) __asm__ volatile("nop"); }

/* ---- descriptors ---- */
static const uint8_t dev_desc[18] = {
    18, 1, 0x00, 0x02, 0x02, 0x00, 0x00, 64,
    0x83, 0x04, 0x40, 0x57, /* 0483:5740 */
    0x00, 0x02, 1, 2, 3, 1 };

static const uint8_t cfg_desc[67] = {
    9, 2, 67, 0, 2, 1, 0, 0x80, 50,
    /* interface 0: CDC comm */
    9, 4, 0, 0, 1, 0x02, 0x02, 0x01, 0,
    5, 0x24, 0x00, 0x10, 0x01,          /* header */
    5, 0x24, 0x01, 0x00, 1,             /* call management */
    4, 0x24, 0x02, 0x02,                /* ACM: line coding + ctrl line state */
    5, 0x24, 0x06, 0, 1,                /* union */
    7, 5, 0x82, 0x03, 16, 0, 255,       /* EP2 IN interrupt */
    /* interface 1: CDC data */
    9, 4, 1, 0, 2, 0x0A, 0, 0, 0,
    7, 5, 0x01, 0x02, 64, 0, 0,         /* EP1 OUT bulk */
    7, 5, 0x81, 0x02, 64, 0, 0 };       /* EP1 IN bulk */

static const char *const strs[] = { 0, "Claude", "BlackPill UART Bridge", "BRIDGE01" };
static uint8_t strbuf[64];

/* line coding: baud(4) stop(1) parity(1) bits(1) */
static uint8_t line_coding[7] = { 0x00, 0x96, 0x00, 0x00, 0, 0, 8 }; /* 38400 8N1 */
static int expect_line_coding;
static int configured;
static int ep1_out_pending; /* needs re-arm when buffer space frees */

static void uart_set_baud(uint32_t baud) {
    if (baud < 1200 || baud > 2000000) return;
    USART1_CR1 &= ~(1u << 13);
    USART1_BRR = (PCLK2 + baud / 2) / baud;
    USART1_CR1 |= (1u << 13);
}

/* ---- USB helpers ---- */
static void fifo_write(int ep, const uint8_t *p, int len) {
    for (int i = 0; i < len; i += 4) {
        uint32_t w = 0;
        for (int k = 0; k < 4 && i + k < len; k++) w |= (uint32_t)p[i + k] << (8 * k);
        FIFO(ep) = w;
    }
}

static void ep0_send(const uint8_t *p, int len, int maxlen) {
    if (len > maxlen) len = maxlen;
    int pk = len ? (len + 63) / 64 : 1;
    DIEPTSIZ(0) = ((uint32_t)pk << 19) | (uint32_t)len;
    DIEPCTL(0) |= EPENA | CNAK;
    fifo_write(0, p, len);
}

static void ep0_zlp(void) { ep0_send(0, 0, 0); }

static void ep0_stall(void) {
    DIEPCTL(0) |= STALL;
    DOEPCTL(0) |= STALL;
}

static void ep0_out_arm(void) {
    DOEPTSIZ(0) = (3u << 29) | (1u << 19) | 64;
    DOEPCTL(0) |= EPENA | CNAK;
}

static void ep1_out_arm(void) {
    DOEPTSIZ(1) = (1u << 19) | 64;
    DOEPCTL(1) |= EPENA | CNAK;
}

static void fifo_flush(void) {
    GRSTCTL = (1u << 5) | (0x10u << 6); /* flush all TX FIFOs */
    while (GRSTCTL & (1u << 5)) {}
    GRSTCTL = (1u << 4);                 /* flush RX FIFO */
    while (GRSTCTL & (1u << 4)) {}
}

static void usb_reset(void) {
    configured = 0;
    ep1_out_pending = 0;
    for (int i = 0; i < 4; i++) DOEPCTL(i) |= SNAK;
    DCFG &= ~(0x7Fu << 4);
    fifo_flush();
    DIEPCTL(1) = 0; DOEPCTL(1) = 0; DIEPCTL(2) = 0;
    DIEPCTL(0) = 0; /* MPSIZ = 64 */
    ep0_out_arm();
}

static void set_configuration(void) {
    DIEPCTL(1) = USBAEP | (2u << 18) | (1u << 22) | SD0PID | SNAK | 64;
    DOEPCTL(1) = USBAEP | (2u << 18) | SD0PID | 64;
    ep1_out_arm();
    DIEPCTL(2) = USBAEP | (3u << 18) | (2u << 22) | SD0PID | SNAK | 16;
    configured = 1;
}

static void handle_setup(const uint8_t *s) {
    uint8_t bmRequestType = s[0], bRequest = s[1];
    uint16_t wValue = s[2] | (s[3] << 8);
    uint16_t wLength = s[6] | (s[7] << 8);

    if (bmRequestType == 0x80 && bRequest == 6) { /* GET_DESCRIPTOR */
        uint8_t type = wValue >> 8, idx = wValue & 0xFF;
        if (type == 1) { ep0_send(dev_desc, sizeof dev_desc, wLength); return; }
        if (type == 2) { ep0_send(cfg_desc, sizeof cfg_desc, wLength); return; }
        if (type == 3) {
            if (idx == 0) { static const uint8_t l[4] = { 4, 3, 0x09, 0x04 }; ep0_send(l, 4, wLength); return; }
            if (idx < 4) {
                const char *c = strs[idx]; int n = 0;
                while (c[n] && n < 30) { strbuf[2 + 2 * n] = c[n]; strbuf[3 + 2 * n] = 0; n++; }
                strbuf[0] = 2 + 2 * n; strbuf[1] = 3;
                ep0_send(strbuf, strbuf[0], wLength); return;
            }
        }
        ep0_stall(); return;
    }
    if (bmRequestType == 0x00 && bRequest == 5) { /* SET_ADDRESS */
        DCFG = (DCFG & ~(0x7Fu << 4)) | ((uint32_t)(wValue & 0x7F) << 4);
        ep0_zlp(); return;
    }
    if (bmRequestType == 0x00 && bRequest == 9) { /* SET_CONFIGURATION */
        if (wValue) set_configuration(); else configured = 0;
        ep0_zlp(); return;
    }
    if (bmRequestType == 0x80 && bRequest == 8) { /* GET_CONFIGURATION */
        static uint8_t c; c = configured; ep0_send(&c, 1, wLength); return;
    }
    if ((bmRequestType & 0x80) && bRequest == 0) { /* GET_STATUS */
        static const uint8_t z[2] = { 0, 0 }; ep0_send(z, 2, wLength); return;
    }
    if (bmRequestType == 0x01 && bRequest == 11) { ep0_zlp(); return; } /* SET_INTERFACE */
    if (bmRequestType == 0x21 && bRequest == 0x20) { expect_line_coding = 1; return; } /* SET_LINE_CODING: data follows */
    if (bmRequestType == 0xA1 && bRequest == 0x21) { ep0_send(line_coding, 7, wLength); return; }
    if (bmRequestType == 0x21 && bRequest == 0x22) { ep0_zlp(); return; } /* SET_CONTROL_LINE_STATE */
    if (bmRequestType == 0x21 && bRequest == 0x23) { ep0_zlp(); return; } /* SEND_BREAK */
    if ((bmRequestType & 0x60) == 0 && (bRequest == 1 || bRequest == 3)) { ep0_zlp(); return; } /* CLEAR/SET_FEATURE */
    ep0_stall();
}

static void usb_poll(void) {
    uint32_t sts = GINTSTS;

    if (sts & GINT_USBRST) { GINTSTS = GINT_USBRST; usb_reset(); }
    if (sts & GINT_ENUMDNE) { GINTSTS = GINT_ENUMDNE; DIEPCTL(0) &= ~3u; }

    while (GINTSTS & GINT_RXFLVL) {
        uint32_t st = GRXSTSP;
        int ep = st & 0xF;
        int bcnt = (st >> 4) & 0x7FF;
        int pktsts = (st >> 17) & 0xF;
        uint8_t buf[64];
        int words = (bcnt + 3) / 4;
        for (int i = 0; i < words; i++) {
            uint32_t w = FIFO(0);
            if (i < 16) { buf[4 * i] = w; buf[4 * i + 1] = w >> 8; buf[4 * i + 2] = w >> 16; buf[4 * i + 3] = w >> 24; }
        }
        if (pktsts == 6 && ep == 0) {          /* SETUP data */
            expect_line_coding = 0;
            handle_setup(buf);
        } else if (pktsts == 2) {               /* OUT data */
            if (ep == 0) {
                if (expect_line_coding && bcnt >= 7) {
                    memcpy(line_coding, buf, 7);
                    expect_line_coding = 0;
                    uart_set_baud(buf[0] | (buf[1] << 8) | (buf[2] << 16) | ((uint32_t)buf[3] << 24));
                    ep0_zlp();
                }
            } else if (ep == 1) {
                for (int i = 0; i < bcnt && i < 64; i++) rb_put(&h2u, buf[i]);
            }
        } else if (pktsts == 3) {               /* OUT transfer complete */
            if (ep == 0) ep0_out_arm();
            else if (ep == 1) {
                if (rb_space(&h2u) >= 64) ep1_out_arm(); else ep1_out_pending = 1;
            }
        } else if (pktsts == 4) {               /* SETUP stage done */
            ep0_out_arm();
        }
    }

    if (ep1_out_pending && rb_space(&h2u) >= 64) { ep1_out_pending = 0; ep1_out_arm(); }

    /* UART -> host on EP1 IN (max 63 bytes so every packet is short) */
    if (configured && !(DIEPCTL(1) & EPENA)) {
        int n = rb_count(&u2h);
        if (n > 0) {
            uint8_t pk[63];
            if (n > 63) n = 63;
            for (int i = 0; i < n; i++) pk[i] = rb_get(&u2h);
            DIEPTSIZ(1) = (1u << 19) | (uint32_t)n;
            DIEPCTL(1) |= EPENA | CNAK;
            fifo_write(1, pk, n);
        }
    }
}

void usart1_isr(void) {
    uint32_t sr = USART1_SR;
    if (sr & ((1u << 5) | (1u << 3))) { /* RXNE or ORE */
        uint8_t c = USART1_DR;
        if (sr & (1u << 5)) rb_put(&u2h, c);
    }
}

static void clock_init(void) {
    RCC_CR |= 1u << 16;                     /* HSE on */
    while (!(RCC_CR & (1u << 17))) {}
    FLASH_ACR = 2 | (1u << 8) | (1u << 9) | (1u << 10);
    /* 25 MHz / 25 * 336 / 4 = 84 MHz SYSCLK, /7 = 48 MHz USB */
    RCC_PLLCFGR = 25 | (336u << 6) | (1u << 16) | (1u << 22) | (7u << 24);
    RCC_CR |= 1u << 24;
    while (!(RCC_CR & (1u << 25))) {}
    RCC_CFGR = (4u << 10) | 2;              /* APB1 /2, APB2 /1, SW = PLL */
    while (((RCC_CFGR >> 2) & 3) != 2) {}
}

static void gpio_uart_init(void) {
    RCC_AHB1ENR |= 1u << 0;                 /* GPIOA */
    RCC_APB2ENR |= 1u << 4;                 /* USART1 */
    RCC_AHB2ENR |= 1u << 7;                 /* OTG FS */
    /* PA9, PA10 AF7; PA11, PA12 AF10 */
    GPIOA_MODER = (GPIOA_MODER & ~(0xFFu << 18)) | (0xAAu << 18);
    GPIOA_OSPEEDR |= 0xFFu << 18;
    GPIOA_PUPDR = (GPIOA_PUPDR & ~(0xFFu << 18)) | (1u << 20); /* pull-up on PA10 RX */
    GPIOA_AFRH = (GPIOA_AFRH & ~0xFFFF0u) | (7u << 4) | (7u << 8) | (10u << 12) | (10u << 16);

    USART1_BRR = (PCLK2 + 38400 / 2) / 38400;
    USART1_CR1 = (1u << 13) | (1u << 5) | (1u << 3) | (1u << 2);
    NVIC_ISER1 = 1u << (37 - 32);
}

static void usb_init(void) {
    GUSBCFG |= 1u << 6;                     /* PHYSEL (embedded FS PHY) */
    while (!(GRSTCTL & (1u << 31))) {}      /* AHB idle */
    GRSTCTL |= 1u;                          /* core soft reset */
    while (GRSTCTL & 1u) {}
    delay(10000);
    GCCFG = (1u << 16) | (1u << 21);        /* PWRDWN off (transceiver on), NOVBUSSENS */
    GUSBCFG = (GUSBCFG & ~(0xFu << 10)) | (1u << 30) | (6u << 10) | (1u << 6); /* force device, TRDT */
    delay(2000000);                         /* >25 ms mode switch */
    PCGCCTL = 0;
    DCFG |= 3;                              /* full speed */
    /* FIFO RAM (words): RX 128 @0, TX0 32 @128, TX1 32 @160, TX2 16 @192 */
    GRXFSIZ = 128;
    DIEPTXF0 = (32u << 16) | 128;
    DIEPTXF(1) = (32u << 16) | 160;
    DIEPTXF(2) = (16u << 16) | 192;
    fifo_flush();
    GINTMSK = 0;                            /* polled */
    GINTSTS = 0xFFFFFFFF;
    DCTL &= ~(1u << 1);                     /* soft-connect */
}

int main(void) {
    clock_init();
    gpio_uart_init();
    usb_init();
    __asm__ volatile("cpsie i");
    for (;;) {
        usb_poll();
        if (USART1_SR & (1u << 7)) {        /* TXE */
            int c = rb_get(&h2u);
            if (c >= 0) USART1_DR = (uint8_t)c;
        }
    }
}

/* ---- startup ---- */
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;

void reset_handler(void) {
    uint32_t *s = &_sidata, *d = &_sdata;
    while (d < &_edata) *d++ = *s++;
    for (d = &_sbss; d < &_ebss; ) *d++ = 0;
    main();
    for (;;) {}
}

void default_handler(void) { for (;;) {} }

__attribute__((section(".isr_vector"), used))
void (*const vectors[16 + 86])(void) = {
    [0] = (void (*)(void))&_estack,
    [1] = reset_handler,
    [2 ... 15] = default_handler,
    [16 ... 16 + 85] = default_handler,
    [16 + 37] = usart1_isr,
};
