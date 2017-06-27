/*
 * ngtcp2
 *
 * Copyright (c) 2017 ngtcp2 contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "ngtcp2_ppe.h"
#include "ngtcp2_pkt.h"
#include "ngtcp2_str.h"
#include "ngtcp2_conv.h"
#include "ngtcp2_mem.h"
#include "ngtcp2_conn.h"

int ngtcp2_ppe_init(ngtcp2_ppe *ppe, uint8_t *out, size_t outlen,
                    ngtcp2_crypto_ctx *cctx, ngtcp2_mem *mem) {
  uint8_t *nonce;
  size_t plen;

  if (cctx->aead_overhead > outlen) {
    return NGTCP2_ERR_INVALID_ARGUMENT;
  }

  plen = outlen - cctx->aead_overhead;
  nonce = ngtcp2_mem_malloc(mem, cctx->ckm->ivlen + plen);
  if (nonce == NULL) {
    return NGTCP2_ERR_NOMEM;
  }

  ngtcp2_buf_init(&ppe->pbuf, nonce + cctx->ckm->ivlen, plen);
  ngtcp2_buf_init(&ppe->cbuf, out, outlen);

  ppe->nonce = nonce;
  ppe->ctx = cctx;
  ppe->mem = mem;

  return 0;
}

void ngtcp2_ppe_free(ngtcp2_ppe *ppe) {
  if (ppe == NULL) {
    return;
  }

  ngtcp2_mem_free(ppe->mem, ppe->nonce);
}

int ngtcp2_ppe_encode_hd(ngtcp2_ppe *ppe, const ngtcp2_pkt_hd *hd) {
  ssize_t rv;
  ngtcp2_buf *buf = &ppe->cbuf;

  if (hd->flags & NGTCP2_PKT_FLAG_LONG_FORM) {
    rv = ngtcp2_pkt_encode_hd_long(buf->last, ngtcp2_buf_left(buf), hd);
  } else {
    rv = ngtcp2_pkt_encode_hd_short(buf->last, ngtcp2_buf_left(buf), hd);
  }
  if (rv < 0) {
    return (int)rv;
  }

  buf->last += rv;

  ppe->pkt_num = hd->pkt_num;

  return 0;
}

int ngtcp2_ppe_encode_frame(ngtcp2_ppe *ppe, const ngtcp2_frame *fr) {
  ssize_t rv;
  ngtcp2_buf *buf = &ppe->pbuf;

  rv = ngtcp2_pkt_encode_frame(buf->last, ngtcp2_buf_left(buf), fr);
  if (rv < 0) {
    return (int)rv;
  }

  buf->last += rv;

  return 0;
}

ssize_t ngtcp2_ppe_final(ngtcp2_ppe *ppe, const uint8_t **ppkt) {
  ssize_t rv;
  ngtcp2_buf *cbuf = &ppe->cbuf;
  ngtcp2_buf *pbuf = &ppe->pbuf;
  ngtcp2_crypto_ctx *ctx = ppe->ctx;
  ngtcp2_conn *conn = ctx->user_data;

  ngtcp2_crypto_create_nonce(ppe->nonce, ctx->ckm->iv, ctx->ckm->ivlen,
                             ppe->pkt_num);

  rv = ppe->ctx->encrypt(conn, cbuf->last, ngtcp2_buf_left(cbuf), pbuf->pos,
                         ngtcp2_buf_len(pbuf), ctx->ckm->key, ctx->ckm->keylen,
                         ppe->nonce, ctx->ckm->ivlen, cbuf->begin,
                         ngtcp2_buf_len(cbuf), conn->user_data);
  if (rv < 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  cbuf->last += rv;

  if (ppkt != NULL) {
    *ppkt = cbuf->begin;
  }

  return (ssize_t)ngtcp2_buf_len(cbuf);
}