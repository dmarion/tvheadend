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

#ifndef __CIPLUS_H__

#if ENABLE_LINUXDVB_CA

#define CIPLUS_APP_CC_RESOURCEID     MKRID(140, 64, 1)

#define CIPLUS_TAG_CC_OPEN_REQ  0x9f9001
#define CIPLUS_TAG_CC_OPEN_CNF  0x9f9002
#define CIPLUS_TAG_CC_DATA_REQ  0x9f9003
#define CIPLUS_TAG_CC_DATA_CNF  0x9f9004

#define CIPLUS_DATATYPE_ID_HOST_BRANDCERT      0x07
#define CIPLUS_DATATYPE_ID_DHPH                0x0d
#define CIPLUS_DATATYPE_ID_HOST_DEVCERT        0x0f
#define CIPLUS_DATATYPE_ID_NONCE_SIGNATURE_A   0x11
#define CIPLUS_DATATYPE_ID_NONCE               0x13

#define CIPLUS_TAG_CC_OPEN_CNF_ARRAY  0x9f, 0x90, 0x02


typedef struct ciplus_ctx * ciplus_ctx_t;
void ciplus_ctx_init(ciplus_ctx_t * ctx);
void ciplus_ctx_destroy(ciplus_ctx_t * ctx);
void ciplus_generate_cc_open_cnf(ciplus_ctx_t ctx, uint8_t **data, int *len);
void ciplus_generate_cc_data_cnf(ciplus_ctx_t ctx, uint8_t * req,
	                             uint8_t **cnf_data, int *cnf_len);
#endif
#endif /* __CIPLUS_H__ */