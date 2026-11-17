/* ca_store.c -- the root certificates Unity's TLS trusts (see ca_store.h)
 *
 * Unity's TLS (unitytls) builds its root store on Android from the Java
 * TrustManager's accepted issuers (java_lang.c). Here they come from the
 * console's own certificate store: the ssl service hands out every built-in
 * root (SslCaCertificateId_All), the ones from id 1000 up being the web PKI
 * (below are Nintendo's private roots). gd_geode_nx did the same for curl,
 * and its HTTPS works on hardware.
 *
 * A player can add roots the firmware lacks: any .pem / .crt / .cer / .der
 * file in <port>/certs/ (PEM may hold several certificates).
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <dirent.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <switch.h>

#include "ca_store.h"
#include "log.h"
#include "paths.h"

#define MAX_CERTS 512

static CaCert s_certs[MAX_CERTS];
static int s_count;
static int s_loaded;

static void add(const uint8_t *der, size_t n) {
  if (s_count >= MAX_CERTS || n < 16 || der[0] != 0x30)
    return;
  uint8_t *c = malloc(n);
  if (!c)
    return;
  memcpy(c, der, n);
  s_certs[s_count].der = c;
  s_certs[s_count].size = n;
  s_count++;
}

static int load_firmware(void) {
  if (R_FAILED(sslInitialize(1)))
    return 0;
  u32 ids[1] = { (u32)SslCaCertificateId_All };
  u32 size = 0, total = 0;
  int got = 0;
  if (R_SUCCEEDED(sslGetCertificateBufSize(ids, 1, &size)) && size >= sizeof(SslBuiltInCertificateInfo)) {
    const size_t cap = (size + 0xfff) & ~(size_t)0xfff;
    void *buf = memalign(0x1000, cap);
    if (buf) {
      memset(buf, 0, cap);
      if (R_SUCCEEDED(sslGetCertificates(buf, size, ids, 1, &total))) {
        const SslBuiltInCertificateInfo *ci = buf;
        for (u32 i = 0; i < total; i++) {
          if (ci[i].cert_id < 1000 || ci[i].status != SslTrustedCertStatus_EnabledTrusted ||
              !ci[i].cert_data || !ci[i].cert_size)
            continue;
          add(ci[i].cert_data, ci[i].cert_size);
          got++;
        }
      }
      free(buf);
    }
  }
  sslExit();
  return got;
}

static int b64v(int c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

// every BEGIN CERTIFICATE block of a PEM text
static int add_pem(const char *text) {
  int got = 0;
  const char *p = text;
  while ((p = strstr(p, "-----BEGIN CERTIFICATE-----"))) {
    p += 27;
    const char *end = strstr(p, "-----END CERTIFICATE-----");
    if (!end)
      break;
    uint8_t *der = malloc((size_t)(end - p));
    size_t n = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (const char *q = p; q < end; q++) {
      const int v = b64v((unsigned char)*q);
      if (v < 0)
        continue;
      acc = acc << 6 | (uint32_t)v;
      bits += 6;
      if (bits >= 8) {
        bits -= 8;
        der[n++] = (uint8_t)(acc >> bits);
      }
    }
    add(der, n);
    free(der);
    got++;
    p = end + 25;
  }
  return got;
}

static int load_folder(void) {
  char dir[600];
  snprintf(dir, sizeof dir, "%s/certs", path_base());
  DIR *d = opendir(dir);
  if (!d)
    return 0;
  int got = 0;
  struct dirent *e;
  while ((e = readdir(d))) {
    const char *dot = strrchr(e->d_name, '.');
    if (!dot || (strcasecmp(dot, ".pem") && strcasecmp(dot, ".crt") && strcasecmp(dot, ".cer") &&
                 strcasecmp(dot, ".der")))
      continue;
    char p[900];
    snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
    FILE *f = fopen(p, "rb");
    if (!f)
      continue;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n > 0 && n < (4 << 20)) {
      char *buf = malloc((size_t)n + 1);
      if (fread(buf, 1, (size_t)n, f) == (size_t)n) {
        buf[n] = 0;
        if (strstr(buf, "-----BEGIN CERTIFICATE-----"))
          got += add_pem(buf);
        else {
          add((const uint8_t *)buf, (size_t)n);
          got++;
        }
      }
      free(buf);
    }
    fclose(f);
  }
  closedir(d);
  return got;
}

// a certificate's DER holds the name (as its subject or issuer)
static int has_name(const CaCert *c, const char *name) {
  const size_t n = strlen(name);
  for (size_t i = 0; i + n <= c->size; i++)
    if (!memcmp(c->der + i, name, n))
      return 1;
  return 0;
}

void ca_store_load(void) {
  if (s_loaded)
    return;
  s_loaded = 1;
  const int fw = load_firmware();
  const int extra = load_folder();
  // the roots the game's servers chain to (Cloudflare's: Google Trust
  // Services, cross-signed by GlobalSign; Let's Encrypt's ISRG)
  static const char *const want[] = { "GlobalSign Root CA", "GTS Root R1", "GTS Root R4", "ISRG Root X1" };
  char have[160] = "";
  for (size_t w = 0; w < sizeof want / sizeof *want; w++)
    for (int i = 0; i < s_count; i++)
      if (has_name(&s_certs[i], want[w])) {
        const size_t l = strlen(have);
        snprintf(have + l, sizeof have - l, "%s%s", l ? ", " : "", want[w]);
        break;
      }
  log_printf("[tls] %d root certificates from the console, %d from certs/ (with %s)", fw, extra,
             have[0] ? have : "none of GlobalSign / GTS / ISRG");
}

int ca_store_count(void) {
  ca_store_load();
  return s_count;
}

const CaCert *ca_store_at(int i) {
  return i >= 0 && i < s_count ? &s_certs[i] : NULL;
}
