/*
 *  Tvheadend - CI+ support
 *
 *  Copyright (C) 2015 Damjan Marion
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <ctype.h>
#include "tvheadend.h"

#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "input/mpegts/ciplus.h"

struct ciplus_ctx {
  X509_STORE *store;

  X509 *host_cust_cert;
  X509 *host_device_cert;
  X509 *cam_cust_cert;
  X509 *cam_device_cert;

  uint8_t nonce[32];
};

void
ciplus_ctx_init(ciplus_ctx_t * ctx_ptr)
{
  ciplus_ctx_t ctx;
  ctx = calloc(1, sizeof(struct ciplus_ctx));
  FILE *f;

  if (!ctx)
    return;

  if (!(ctx->store = X509_STORE_new())) {
    tvhtrace("ciplus", "failed to create X509 store");
    return;
  }

  if (X509_STORE_load_locations(ctx->store, "root.pem", NULL) != 1) {
    tvhtrace("ciplus", "failed to load root certificate");
    return;
  }

  f = fopen("device.pem", "r");
  if (!f) {
    tvhtrace("ciplus", "failed to open device certificate");
    return;
  }

  ctx->host_device_cert = PEM_read_X509(f, NULL, NULL, NULL);

  if (!ctx->host_device_cert) {
    tvhtrace("ciplus", "failed to load device certificate");
    return;
  }

  unsigned char *cert_der = NULL;
  int cert_len;
  cert_len = i2d_X509(ctx->host_device_cert, &cert_der);

  if (cert_len<1) {
    tvhtrace("ciplus", "failed to get cert in DER format");
    return;
  }
  tvhlog_hexdump("ciplus", cert_der, cert_len);

  tvhtrace("ciplus", "context init");

  *ctx_ptr = ctx;
}

void
ciplus_ctx_destroy(ciplus_ctx_t * ctx)
{
  free(*ctx);
  *ctx = NULL;

  tvhtrace("ciplus", "context destroy");
}

void
ciplus_generate_cc_open_cnf(ciplus_ctx_t ctx, uint8_t **data, int *len)

{
  uint8_t * d = malloc(5);
  d[0] = 0x9f;
  d[1] = 0x90;
  d[2] = 0x02;
  d[3] = 0x01;
  d[4] = 0x01;
  *data = d;
  *len = 5;
}

/*
9F 90 03 2A 01 01 13 00 20 FB 37 02 29 C1 C2 92 ...*.... .7.)...
7D 6F 8D 01 71 CC A4 31 E6 B0 B1 B3 77 07 3E 38 }o..q..1....w.>8
42 F5 B4 CA CE 63 C2 91 BD 04 0D 11 0F 07       B....c........////

9F 90 03 = tag cc_data_req
2A       = length (42)
01       = cc_system_id_bitmask
01       = send_datatype_num

13 00 20
FB 37 02 29 C1 C2 92 7D 6F 8D 01 71 CC A4 31 E6
B0 B1 B3 77 07 3E 38 42 F5 B4 CA CE 63 C2 91 BD
  13     = datatype_di
  00 20  = datatype len
  [20]   = data

04
  0D
  11
  0F
  07

*/

void
ciplus_generate_cc_data_cnf(ciplus_ctx_t ctx, uint8_t * req, uint8_t **cnf_data,
                            int *cnf_len)
{
//  uint8_t len;
  uint8_t send_datatype_nbr, request_datatype_nbr;
  int i, p;

  /* ASN.1 coded length can take 1, 2 or 3 bytes - en50221 section 7 */
  if((req[3] & 0x80) == 0)
    p = 5;
  else if ((req[3] & 0x7F) == 1)
    p = 6;
  else if ((req[3] & 0x7F) == 2)
    p = 7;
  else
    return;

  //len = data[3];
  p = 5;
  send_datatype_nbr = req[p++];

  for (i = 0; i < send_datatype_nbr; i++) {
    uint8_t datatype_id = req[p];
    uint16_t datatype_length = req[p+1] << 8 | req[p+2];

    tvhtrace("ciplus", "send datatype_id %02x len %u", datatype_id, datatype_length);
    tvhlog_hexdump("ciplus", &req[p+3], datatype_length);
    switch (datatype_id) {
      case CIPLUS_DATATYPE_ID_NONCE:
        memcpy(ctx->nonce, &req[p+3], sizeof(ctx->nonce));
    }
    p += datatype_length + 3;
  }

  request_datatype_nbr = req[p++];

  for (i = 0; i < request_datatype_nbr; i++) {
    uint8_t datatype_id = req[p];

    tvhtrace("ciplus", "request datatype_id %02x", datatype_id);

    p++;
  }
}
