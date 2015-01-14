// -*-c-*-
/*
  This file is part of cpp-ethereum.

  cpp-ethereum is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  cpp-ethereum is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/

/** @file daggerhashimoto.c
* @author Matthew Wampler-Doty <matt@w-d.org>
* @date 2014
*/

#include <stdlib.h>
#include "daggerhashimoto.h"
#include "sha3.h"


void sha3_1(uint8_t result[HASH_CHARS], const unsigned char previous_hash[HASH_CHARS]) {
    struct sha3_ctx ctx;
    sha3_init(&ctx, 256);
    sha3_update(&ctx, previous_hash, HASH_CHARS);
    sha3_finalize(&ctx, result);
}

void sha3_dag(uint64_t *dag, const unsigned char prevhash[HASH_CHARS]) {
    // DAG must be at least 256 bits long!
    uint8_t result[HASH_CHARS];
    int i, j;
    sha3_1(result, prevhash);
    for (i = 0; i < HASH_UINT64S; ++i) {
        dag[i] = 0;
        for (j = 0; j < 8; ++j) {
            dag[i] <<= 8;
            dag[i] += result[8 * i + j];
        }
    }
}

void uint64str(uint8_t result[8], uint64_t n) {
    for (int i = 0; i < 8; ++i) {
        result[i] = (uint8_t) n;
        n >>= 8;
    }
}

void sha3_nonce(uint64_t rands[HASH_UINT64S], const unsigned char prevhash[HASH_CHARS], const uint64_t nonce) {
    uint8_t result[HASH_CHARS], nonce_data[8];
    int i, j;
    struct sha3_ctx ctx;
    uint64str(nonce_data, nonce);
    sha3_init(&ctx, 256);
    sha3_update(&ctx, prevhash, HASH_CHARS);
    sha3_update(&ctx, nonce_data, 8);
    sha3_finalize(&ctx, result);
    sha3_1(result, prevhash);
    for (i = 0; i < HASH_UINT64S; ++i) {
        rands[i] = 0;
        for (j = 0; j < 8; ++j) {
            rands[i] <<= 8;
            rands[i] += result[8 * i + j];
        }
    }
}

void sha3_mix(unsigned char result[32], uint32_t *const mix) {
    struct sha3_ctx ctx;
    sha3_init(&ctx, 256);
    sha3_update(&ctx, (uint8_t *) mix, sizeof(mix));
    sha3_finalize(&ctx, result);
}

inline uint32_t cube_mod_safe_prime(uint32_t x) {
    uint64_t temp = x * x;
    temp = x * ((uint32_t) (temp % SAFE_PRIME));
    return (uint32_t) temp;
}

inline uint32_t cube_mod_safe_prime2(uint32_t x) {
    uint64_t temp = x * x;
    temp = x * ((uint32_t) (temp % SAFE_PRIME2));
    return (uint32_t) temp;
}

void produce_dag(
        uint64_t *dag,
        const parameters params,
        const unsigned char seed[HASH_CHARS]) {
    sha3_dag(dag, seed);
    uint32_t picker1 = (uint32_t) dag[0] % SAFE_PRIME,
            picker2, worker1, worker2;
    uint64_t x;
    size_t i;
    int j;
    picker1 = picker1 < 2 ? 2 : picker1;
    for (i = 8; i < params.n; ++i) {
        picker2 = cube_mod_safe_prime(picker1);
        worker1 = picker1 = cube_mod_safe_prime(picker2);
        x = (picker2 << 32) + picker1;
        x ^= dag[x % i];
        for (j = 0; j < params.w; ++j) {
            worker2 = cube_mod_safe_prime2(worker1);
            worker1 = cube_mod_safe_prime2(worker2);
            x ^= (worker2 << 32) + worker1;
        }
        dag[i] = x;
    }
}

uint32_t pow_mod(const uint32_t a, int b) {
    uint64_t r = 1, aa = a;
    while (1) {
        if (b & 1)
            r = (r * a) % SAFE_PRIME;
        b >>= 1;
        if (b == 0)
            break;
        aa = (aa * aa) % SAFE_PRIME;
    }
    return (uint32_t) r;
}


uint32_t quick_calc_cached(uint64_t *cache, const parameters params, uint64_t pos) {
    if (pos < params.cache_size)
        return cache[pos];
    else {
        uint32_t x = pow_mod(cache[0], pos + 1);
        for (int j = 0; j < params.w; ++j)
            x ^= cube_mod_safe_prime(x);
        return x;
    }
}

uint32_t quick_calc(
        parameters params,
        const unsigned char seed[HASH_CHARS],
        const uint64_t pos) {
    uint64_t cache[params.cache_size];
    params.n = params.cache_size;
    produce_dag(cache, params, seed);
    return quick_calc_cached(cache, params, pos);
}

void hashimoto(
        unsigned char result[HASH_CHARS],
        const uint32_t *dag,
        const parameters params,
        const unsigned char prevhash[HASH_CHARS],
        const uint64_t nonce) {
    const size_t m = params.n - WIDTH;
    size_t idx = sha3_nonce(prevhash, nonce) % m;
    uint32_t mix[WIDTH], c[WIDTH];

    for (int i = 0; i < WIDTH; ++i) {
        mix[i] = 0;
        c[i] = dag[idx + i];
    }

    for (int p = 0; p < params.accesses; ++p) {
        for (int i = 0; i < WIDTH; ++i)
            mix[i] ^= dag[idx + i];
        idx = (idx ^ ((uint64_t *) mix)[0]) % m;
    }
    sha3_mix(result, mix);
}

void hashimoto(
        unsigned char result[HASH_CHARS],
        const uint32_t *dag,
        const parameters params,
        const unsigned char prevhash[HASH_CHARS],
        const uint64_t nonce) {
    const uint64_t m = params.n - WIDTH;
    uint64_t idx = sha3_nonce(prevhash, nonce) % m;
    uint32_t mix[WIDTH];
    int i;
    for (i = 0; i < WIDTH; ++i)
        mix[i] = 0;
    for (int p = 0; p < params.accesses; ++p) {
        for (i = 0; i < WIDTH; ++i)
            mix[i] ^= dag[idx + i];
        idx = (idx ^ ((uint64_t *) mix)[0]) % m;
    }
    sha3_mix(result, mix);
}

void quick_hashimoto_cached(
        unsigned char result[32],
        uint64_t *cache,
        const parameters params,
        const unsigned char prevhash[32],
        const uint64_t nonce) {
    const uint64_t m = params.n - WIDTH;
    uint64_t idx = sha3_nonce(prevhash, nonce) % m;
    uint32_t mix[WIDTH];
    int i;
    for (i = 0; i < WIDTH; ++i)
        mix[i] = 0;
    for (int p = 0; p < params.accesses; ++p) {
        for (i = 0; i < WIDTH; ++i)
            mix[i] ^= quick_calc_cached(cache, params, idx + i);
        idx = (idx ^ ((uint64_t *) mix)[0]) % m;
    }
    sha3_mix(result, mix);
}

void quick_hashimoto(
        unsigned char result[32],
        const unsigned char seed[32],
        parameters params,
        const unsigned char prevhash[32],
        const uint64_t nonce) {
    const uint64_t original_n = params.n;
    uint64_t cache[params.cache_size];
    params.n = params.cache_size;
    produce_dag(cache, params, prevhash);
    params.n = original_n;
    return quick_hashimoto_cached(result, cache, params, prevhash, nonce);
}