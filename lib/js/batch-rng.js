/*!
 * batch-rng.js - batch rng for bcrypto
 * Copyright (c) 2019-2020, Christopher Jeffrey (MIT License).
 * https://github.com/bcoin-org/bcrypto
 *
 * Parts of this software are based on ElementsProject/secp256k1-zkp:
 *   Copyright (c) 2013, Pieter Wuille.
 *   https://github.com/ElementsProject/secp256k1-zkp
 *
 * Resources:
 *   https://github.com/ElementsProject/secp256k1-zkp/blob/11af701/src/modules/schnorrsig/main_impl.h#L166
 *   https://github.com/ElementsProject/secp256k1-zkp/blob/11af701/src/scalar_4x64_impl.h#L972
 *   https://github.com/ElementsProject/secp256k1-zkp/blob/11af701/src/scalar_8x32_impl.h#L747
 */

'use strict';

const assert = require('../internal/assert');
const BN = require('../bn');
const ChaCha20 = require('../chacha20');

/**
 * BatchRNG
 */

class BatchRNG {
  constructor(options, encode = key => key) {
    this.curve = options.curve;
    this.hash = options.hash;
    this.encode = encode;
    this.chacha = new ChaCha20();
    this.key = Buffer.alloc(32, 0x00);
    this.iv = Buffer.alloc(8, 0x00);
    this.cache = [new BN(1), new BN(1)];
  }

  init(batch) {
    assert(Array.isArray(batch));

    // eslint-disable-next-line
    const h = new this.hash();

    h.init();

    for (const [msg, sig, key] of batch) {
      h.update(sig);
      h.update(this.hash.digest(msg));
      h.update(this.encode(key));
    }

    let key = h.final(32);

    if (key.length > 32)
      key = key.slice(0, 32);

    assert(key.length === 32);

    this.key = key;
    this.cache[0] = new BN(1);
    this.cache[1] = new BN(1);

    return this;
  }

  encrypt(counter) {
    const size = (this.curve.scalarSize * 2 + 63) & -64;
    const data = Buffer.alloc(size, 0x00);
    const left = data.slice(0, this.curve.scalarSize);
    const right = data.slice(this.curve.scalarSize);

    this.chacha.init(this.key, this.iv, counter);
    this.chacha.encrypt(data);

    return [
      this.curve.decodeScalar(left),
      this.curve.decodeScalar(right)
    ];
  }

  refresh(counter) {
    let overflow = 0;

    for (;;) {
      // First word is always zero.
      this.iv[4] = overflow;
      this.iv[5] = overflow >>> 8;
      this.iv[6] = overflow >>> 16;
      this.iv[7] = overflow >>> 24;

      overflow += 1;

      const [s1, s2] = this.encrypt(counter);

      if (s1.isZero() || s1.cmp(this.curve.n) >= 0)
        continue;

      if (s2.isZero() || s2.cmp(this.curve.n) >= 0)
        continue;

      this.cache[0] = s1;
      this.cache[1] = s2;

      break;
    }
  }

  generate(index) {
    assert((index >>> 0) === index);

    if (index & 1)
      this.refresh(index >>> 1);

    return this.cache[index & 1];
  }
}

/*
 * Expose
 */

module.exports = BatchRNG;