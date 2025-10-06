#include "utils.h"
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>

/* --------------------------------------------------------------------
   Mémoire
   -------------------------------------------------------------------- */
void* memcpy(void* dest, const void* src, size_t n) {
    char* d = dest;
    const char* s = src;
    while (n--) *d++ = *s++;
    return dest;
}

void* memset(void* ptr, int value, size_t num) {
    unsigned char* p = ptr;
    while (num--) *p++ = (unsigned char)value;
    return ptr;
}

int memcmp(const void* s1, const void* s2, size_t n) {
    const unsigned char* p1 = s1, *p2 = s2;
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}

/* --------------------------------------------------------------------
   Chaînes
   -------------------------------------------------------------------- */
int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int strncmp(const char* s1, const char* s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i]) return (s1[i] < s2[i]) ? -1 : 1;
        if (s1[i] == '\0') return 0;
    }
    return 0;
}

char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while (*src) *d++ = *src++;
    *d = '\0';
    return dest;
}

char* strncpy(char* dest, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
    return dest;
}

size_t strlen(const char* str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

char* strcat(char* dest, const char* src) {
    char* d = dest;
    while (*d) d++;
    while (*src) *d++ = *src++;
    *d = '\0';
    return dest;
}

char* strchr(const char* s, int c) {
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return (c == 0) ? (char*)s : NULL;
}

char* strrchr(const char* s, int c) {
    const char* last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    return (c == 0) ? (char*)s : (char*)last;
}

int strcasecmp(const char* s1, const char* s2) {
    while (*s1 && *s2) {
        char c1 = (*s1 >= 'A' && *s1 <= 'Z') ? *s1 + 32 : *s1;
        char c2 = (*s2 >= 'A' && *s2 <= 'Z') ? *s2 + 32 : *s2;
        if (c1 != c2) return (unsigned char)c1 - (unsigned char)c2;
        s1++; s2++;
    }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

/* --------------------------------------------------------------------
   Petit malloc / free freestanding (heap statique)
   -------------------------------------------------------------------- */
#define HEAP_SIZE (64 * 1024)
static uint8_t kernel_heap[HEAP_SIZE];
static size_t heap_offset = 0;

void* malloc(size_t size) {
    if (size == 0) return NULL;
    if (heap_offset + size > HEAP_SIZE) return NULL;
    void* ptr = &kernel_heap[heap_offset];
    heap_offset += (size + 7) & ~7; // align 8 octets
    return ptr;
}

void free(void* ptr) {
    (void)ptr; /* Pas de gestion libre pour simplifier */
}

/* --------------------------------------------------------------------
   64-bit division helpers (nécessaires à GCC en freestanding)
   -------------------------------------------------------------------- */
unsigned long long __udivdi3(unsigned long long a, unsigned long long b) {
    return a / b;
}
unsigned long long __umoddi3(unsigned long long a, unsigned long long b) {
    return a % b;
}

/* --------------------------------------------------------------------
   Conversion numérique (pour printf/snprintf)
   -------------------------------------------------------------------- */
static void itoa_dec(int value, char* buffer) {
    char tmp[32];
    int i = 0, j = 0;
    if (value == 0) { buffer[0] = '0'; buffer[1] = '\0'; return; }
    int neg = (value < 0);
    if (neg) value = -value;
    while (value > 0) { tmp[i++] = (value % 10) + '0'; value /= 10; }
    if (neg) buffer[j++] = '-';
    while (i > 0) buffer[j++] = tmp[--i];
    buffer[j] = '\0';
}

static void itoa_hex(uint32_t value, char* buffer) {
    const char* hex = "0123456789ABCDEF";
    char tmp[32];
    int i = 0, j = 0;
    if (value == 0) { buffer[0] = '0'; buffer[1] = '\0'; return; }
    while (value > 0) { tmp[i++] = hex[value & 0xF]; value >>= 4; }
    buffer[j++] = '0'; buffer[j++] = 'x';
    while (i > 0) buffer[j++] = tmp[--i];
    buffer[j] = '\0';
}

/* --------------------------------------------------------------------
   snprintf (déjà présent dans ton code, inchangé)
   -------------------------------------------------------------------- */
int snprintf(char* out, size_t n, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    size_t pos = 0;

    for (const char* p = fmt; *p && pos < n - 1; p++) {
        if (*p != '%') {
            out[pos++] = *p;
            continue;
        }
        p++;
        if (*p == 's') {
            const char* s = va_arg(args, const char*);
            while (*s && pos < n - 1) out[pos++] = *s++;
        } else if (*p == 'd') {
            char buf[32];
            itoa_dec(va_arg(args, int), buf);
            for (char* q = buf; *q && pos < n - 1; q++) out[pos++] = *q;
        } else if (*p == 'x') {
            char buf[32];
            itoa_hex(va_arg(args, uint32_t), buf);
            for (char* q = buf; *q && pos < n - 1; q++) out[pos++] = *q;
        } else {
            out[pos++] = *p;
        }
    }

    out[pos] = '\0';
    va_end(args);
    return pos;
}

/* --------------------------------------------------------------------
   printf minimal avec putchar VGA fallback
   -------------------------------------------------------------------- */

/* VGA fallback si ton kernel n’a pas de putchar défini ailleurs */
__attribute__((weak))
int putchar(int c) {
    static uint16_t* const VGA = (uint16_t*)0xB8000;
    static int x = 0, y = 0;
    if (c == '\n') {
        x = 0; y++;
    } else {
        VGA[y * 80 + x] = (uint16_t)c | 0x0700;
        if (++x >= 80) { x = 0; y++; }
    }
    if (y >= 25) {
        for (int i = 0; i < 24 * 80; i++) VGA[i] = VGA[i + 80];
        for (int i = 24 * 80; i < 25 * 80; i++) VGA[i] = ' ' | 0x0700;
        y = 24;
    }
    return c;
}

int vprintf(const char* fmt, va_list args) {
    char buf[512];
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    for (int i = 0; i < len; i++) putchar(buf[i]);
    return len;
}

/* vsnprintf minimal pour vprintf */
int vsnprintf(char* out, size_t n, const char* fmt, va_list args) {
    size_t pos = 0;
    for (const char* p = fmt; *p && pos < n - 1; p++) {
        if (*p != '%') {
            out[pos++] = *p;
            continue;
        }
        p++;
        if (*p == 's') {
            const char* s = va_arg(args, const char*);
            while (*s && pos < n - 1) out[pos++] = *s++;
        } else if (*p == 'd') {
            char buf[32];
            itoa_dec(va_arg(args, int), buf);
            for (char* q = buf; *q && pos < n - 1; q++) out[pos++] = *q;
        } else if (*p == 'x') {
            char buf[32];
            itoa_hex(va_arg(args, uint32_t), buf);
            for (char* q = buf; *q && pos < n - 1; q++) out[pos++] = *q;
        } else {
            out[pos++] = *p;
        }
    }
    out[pos] = '\0';
    return pos;
}

int printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int r = vprintf(fmt, args);
    va_end(args);
    return r;
}
