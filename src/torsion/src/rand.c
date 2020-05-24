/*!
 * rand.c - RNG for libtorsion
 * Copyright (c) 2020, Christopher Jeffrey (MIT License).
 * https://github.com/bcoin-org/libtorsion
 */

/**
 * Random Number Generation
 *
 * We use a ChaCha20 RNG with a design inspired by
 * libsodium[1]. Our primary difference is a much
 * more complicated seeding procedure which ensures
 * strong randomness (similar to Bitcoin Core[2]).
 *
 * The seeding procedure uses a combination of OS
 * entropy, hardware entropy, and entropy manually
 * gathered from the environment. See entropy/ for
 * more information.
 *
 * We do not currently expose a global interface as
 * it would require locks and getpid() checks (in
 * order to be fork-aware). Instead, the programmer
 * is meant to instantiate a different RNG for each
 * thread/process. This achieves both reentrancy as
 * well as fork-awareness.
 *
 * The RNG below is not used anywhere internally,
 * and as such, libtorsion can build without it (in
 * the case that more portability is desired).
 *
 * [1] https://github.com/jedisct1/libsodium/blob/master/src/libsodium/randombytes/internal/randombytes_internal_random.c
 * [2] https://github.com/bitcoin/bitcoin/blob/master/src/random.cpp
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <torsion/chacha20.h>
#include <torsion/hash.h>
#include <torsion/rand.h>
#include <torsion/util.h>
#include "entropy/entropy.h"

/*
 * RNG
 */

int
rng_init(rng_t *rng) {
  unsigned char seed[64];
  sha512_t hash;
  uintptr_t ptr;
  uint64_t tsc;
  size_t i;

  memset(rng, 0, sizeof(*rng));

  sha512_init(&hash);

  ptr = (uintptr_t)((void *)rng);
  sha512_update(&hash, &ptr, sizeof(ptr));

  ptr = (uintptr_t)((void *)seed);
  sha512_update(&hash, &ptr, sizeof(ptr));

  tsc = torsion_rdtsc();
  sha512_update(&hash, &tsc, sizeof(tsc));

  /* OS entropy (64 bytes). */
  if (!torsion_sysrand(seed, 64))
    return 0;

  sha512_update(&hash, seed, 64);

  tsc = torsion_rdtsc();
  sha512_update(&hash, &tsc, sizeof(tsc));

  /* Hardware entropy (32 bytes). */
  if (torsion_hwrand(seed, 32))
    sha512_update(&hash, seed, 32);

  tsc = torsion_rdtsc();
  sha512_update(&hash, &tsc, sizeof(tsc));

  /* Manual entropy (64 bytes). */
  if (torsion_envrand(seed))
    sha512_update(&hash, seed, 64);

  tsc = torsion_rdtsc();
  sha512_update(&hash, &tsc, sizeof(tsc));

  /* At this point, only one of the above
     entropy sources needs to be strong in
     order for our RNG to work. It's extremely
     unlikely that all three would somehow
     be compromised. */
  sha512_final(&hash, seed);

  /* Strengthen the seed a bit. */
  for (i = 0; i < 500; i++) {
    sha512_init(&hash);
    sha512_update(&hash, seed, 64);

    if (i == 500 - 1) {
      tsc = torsion_rdtsc();
      sha512_update(&hash, &tsc, sizeof(tsc));
    }

    sha512_final(&hash, seed);
  }

  /* We use XChaCha20 to reduce the first
     48 bytes down to 32. This allows us to
     use the entire 64 byte hash as entropy. */
  chacha20_derive(seed, seed, 32, seed + 32);

  /* Read our initial ChaCha20 state. */
  memcpy(rng->key, seed, 32);
  memcpy(&rng->zero, seed + 48, 8);
  memcpy(&rng->nonce, seed + 56, 8);

  /* Cache the rdrand check. */
  rng->rdrand = torsion_has_rdrand();

  cleanse(seed, sizeof(seed));
  cleanse(&hash, sizeof(hash));

  return 1;
}

void
rng_generate(rng_t *rng, void *dst, size_t size) {
  unsigned char *key = (unsigned char *)rng->key;
  unsigned char *nonce = (unsigned char *)&rng->nonce;
  chacha20_t ctx;

  if (size == 0)
    return;

  memset(dst, 0, size);

  /* Read the keystream. */
  chacha20_init(&ctx, key, 32, nonce, 8, rng->zero);
  chacha20_encrypt(&ctx, dst, dst, size);

  /* Re-key immediately. */
  rng->key[0] ^= size;

  /* Mix in some hardware entropy. */
  if (rng->rdrand)
    rng->key[3] ^= torsion_rdrand();

  rng->nonce++;

  /* XOR the current key with the keystream. */
  chacha20_init(&ctx, key, 32, nonce, 8, rng->zero);
  chacha20_encrypt(&ctx, key, key, 32);

  /* Cleanse the chacha state. */
  cleanse(&ctx, sizeof(ctx));
}

uint32_t
rng_random(rng_t *rng) {
  if ((rng->pos & 15) == 0) {
    rng_generate(rng, rng->pool, 64);
    rng->pos = 0;
  }

  return rng->pool[rng->pos++];
}

uint32_t
rng_uniform(rng_t *rng, uint32_t max) {
  /* See: http://www.pcg-random.org/posts/bounded-rands.html */
  uint32_t x, r;

  if (max < 2)
    return 0;

  do {
    x = rng_random(rng);
    r = x % max;
  } while (x - r > (-max));

  return r;
}

/*
 * Entropy
 */

int
torsion_getentropy(void *dst, size_t size) {
  return torsion_sysrand(dst, size);
}
