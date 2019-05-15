/*!
 * ecies.js - ecies for javascript (crypto_secretbox_xsalsa20poly1305)
 * Copyright (c) 2018, Christopher Jeffrey (MIT License).
 * https://github.com/bcoin-org/bcrypto
 */

'use strict';

const assert = require('bsert');
const box = require('./secretbox');
const random = require('./random');

/*
 * ECIES
 */

const ECIES = {
  encrypt(curve, kdf, msg, pub, priv = null) {
    assert(curve && typeof curve.id === 'string');

    if (priv == null)
      priv = curve.privateKeyGenerate();

    const ourPub = curve.publicKeyCreate(priv);
    const secret = curve.derive(pub, priv);
    const key = box.derive(secret, kdf);
    const nonce = random.randomBytes(24);
    const sealed = box.seal(msg, key, nonce);

    return Buffer.concat([ourPub, nonce, sealed]);
  },

  decrypt(curve, kdf, msg, priv) {
    assert(curve && typeof curve.id === 'string');
    assert(Buffer.isBuffer(msg));

    const klen = curve.type === 'short'
      ? 1 + curve.size
      : curve.size;

    if (msg.length < klen + 24)
      throw new Error('Invalid ciphertext.');

    const theirPub = msg.slice(0, klen);
    const nonce = msg.slice(klen, klen + 24);
    const sealed = msg.slice(klen + 24);
    const secret = curve.derive(theirPub, priv);
    const key = box.derive(secret, kdf);

    return box.open(sealed, key, nonce);
  }
};

/*
 * Expose
 */

module.exports = ECIES;