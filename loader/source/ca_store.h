/* ca_store.h -- the root certificates Unity's TLS trusts (ca_store.c)
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef CA_STORE_H
#define CA_STORE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  const uint8_t *der;
  size_t size;
} CaCert;

// The console's built-in web roots plus <port>/certs/*.pem|crt|cer|der.
// Loads once (needs the ssl service for a moment).
void ca_store_load(void);
int ca_store_count(void);
const CaCert *ca_store_at(int i);

#endif
