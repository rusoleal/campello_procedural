#pragma once

// Noise functions ported from MaterialX's mx_noise.glsl, so baked patterns
// match what MaterialX renders for the same graph.
//
// Copyright Contributors to the MaterialX Project
// SPDX-License-Identifier: Apache-2.0
// https://github.com/AcademySoftwareFoundation/MaterialX
//
// Changes: GLSL to C++ (uint32_t arithmetic for uint, std::floor), only the
// functions the evaluator uses.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace systems::leal::campello_procedural::noise {

using Vec2 = std::array<float, 2>;
using Vec3 = std::array<float, 3>;

inline float select(bool b, float t, float f) { return b ? t : f; }
inline float negateIf(float v, bool b) { return b ? -v : v; }

inline int floorInt(float x) { return int(std::floor(x)); }
inline float floorFrac(float x, int& i) {
    i = floorInt(x);
    return x - float(i);
}

inline float bilerp(float v0, float v1, float v2, float v3, float s, float t) {
    const float s1 = 1.0f - s;
    return (1.0f - t) * (v0 * s1 + v1 * s) + t * (v2 * s1 + v3 * s);
}

inline float trilerp(float v0, float v1, float v2, float v3, float v4, float v5, float v6, float v7, float s, float t,
                     float r) {
    const float s1 = 1.0f - s;
    const float t1 = 1.0f - t;
    const float r1 = 1.0f - r;
    return r1 * (t1 * (v0 * s1 + v1 * s) + t * (v2 * s1 + v3 * s)) + r * (t1 * (v4 * s1 + v5 * s) + t * (v6 * s1 + v7 * s));
}

inline float gradient(uint32_t hash, float x, float y) {
    const uint32_t h = hash & 7u;
    const float u = select(h < 4u, x, y);
    const float v = 2.0f * select(h < 4u, y, x);
    return negateIf(u, (h & 1u) != 0) + negateIf(v, (h & 2u) != 0);
}

inline float gradient(uint32_t hash, float x, float y, float z) {
    const uint32_t h = hash & 15u;
    const float u = select(h < 8u, x, y);
    const float v = select(h < 4u, y, select(h == 12u || h == 14u, x, z));
    return negateIf(u, (h & 1u) != 0) + negateIf(v, (h & 2u) != 0);
}

inline float gradientScale2d(float v) { return 0.6616f * v; }
inline float gradientScale3d(float v) { return 0.9820f * v; }

inline uint32_t rotl32(uint32_t x, int k) { return (x << k) | (x >> (32 - k)); }

inline void bjmix(uint32_t& a, uint32_t& b, uint32_t& c) {
    a -= c; a ^= rotl32(c, 4);  c += b;
    b -= a; b ^= rotl32(a, 6);  a += c;
    c -= b; c ^= rotl32(b, 8);  b += a;
    a -= c; a ^= rotl32(c, 16); c += b;
    b -= a; b ^= rotl32(a, 19); a += c;
    c -= b; c ^= rotl32(b, 4);  b += a;
}

inline uint32_t bjfinal(uint32_t a, uint32_t b, uint32_t c) {
    c ^= b; c -= rotl32(b, 14);
    a ^= c; a -= rotl32(c, 11);
    b ^= a; b -= rotl32(a, 25);
    c ^= b; c -= rotl32(b, 16);
    a ^= c; a -= rotl32(c, 4);
    b ^= a; b -= rotl32(a, 14);
    c ^= b; c -= rotl32(b, 24);
    return c;
}

inline float bitsTo01(uint32_t bits) { return float(bits) / float(0xffffffffu); }

inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

inline uint32_t seed(uint32_t len) { return 0xdeadbeefu + (len << 2u) + 13u; }

inline uint32_t hashInt(int x, int y) {
    uint32_t a = seed(2), b = a, c = a;
    a += uint32_t(x);
    b += uint32_t(y);
    return bjfinal(a, b, c);
}

inline uint32_t hashInt(int x, int y, int z) {
    uint32_t a = seed(3), b = a, c = a;
    a += uint32_t(x);
    b += uint32_t(y);
    c += uint32_t(z);
    return bjfinal(a, b, c);
}

inline uint32_t hashInt(int x, int y, int z, int xx) {
    uint32_t a = seed(4), b = a, c = a;
    a += uint32_t(x);
    b += uint32_t(y);
    c += uint32_t(z);
    bjmix(a, b, c);
    a += uint32_t(xx);
    return bjfinal(a, b, c);
}

inline std::array<uint32_t, 3> hashVec3(int x, int y) {
    const uint32_t h = hashInt(x, y);
    return {h & 0xFFu, (h >> 8) & 0xFFu, (h >> 16) & 0xFFu};
}

inline std::array<uint32_t, 3> hashVec3(int x, int y, int z) {
    const uint32_t h = hashInt(x, y, z);
    return {h & 0xFFu, (h >> 8) & 0xFFu, (h >> 16) & 0xFFu};
}

inline float perlin(Vec2 p) {
    int X, Y;
    const float fx = floorFrac(p[0], X);
    const float fy = floorFrac(p[1], Y);
    const float u = fade(fx);
    const float v = fade(fy);
    const float result = bilerp(gradient(hashInt(X, Y), fx, fy), gradient(hashInt(X + 1, Y), fx - 1.0f, fy),
                                gradient(hashInt(X, Y + 1), fx, fy - 1.0f),
                                gradient(hashInt(X + 1, Y + 1), fx - 1.0f, fy - 1.0f), u, v);
    return gradientScale2d(result);
}

inline float perlin(Vec3 p) {
    int X, Y, Z;
    const float fx = floorFrac(p[0], X);
    const float fy = floorFrac(p[1], Y);
    const float fz = floorFrac(p[2], Z);
    const float u = fade(fx);
    const float v = fade(fy);
    const float w = fade(fz);
    const float result = trilerp(
        gradient(hashInt(X, Y, Z), fx, fy, fz), gradient(hashInt(X + 1, Y, Z), fx - 1.0f, fy, fz),
        gradient(hashInt(X, Y + 1, Z), fx, fy - 1.0f, fz), gradient(hashInt(X + 1, Y + 1, Z), fx - 1.0f, fy - 1.0f, fz),
        gradient(hashInt(X, Y, Z + 1), fx, fy, fz - 1.0f), gradient(hashInt(X + 1, Y, Z + 1), fx - 1.0f, fy, fz - 1.0f),
        gradient(hashInt(X, Y + 1, Z + 1), fx, fy - 1.0f, fz - 1.0f),
        gradient(hashInt(X + 1, Y + 1, Z + 1), fx - 1.0f, fy - 1.0f, fz - 1.0f), u, v, w);
    return gradientScale3d(result);
}

// Three channels: each from its own byte of the corner hashes.
inline Vec3 perlinVec3(Vec2 p) {
    int X, Y;
    const float fx = floorFrac(p[0], X);
    const float fy = floorFrac(p[1], Y);
    const float u = fade(fx);
    const float v = fade(fy);
    const auto h00 = hashVec3(X, Y), h10 = hashVec3(X + 1, Y), h01 = hashVec3(X, Y + 1), h11 = hashVec3(X + 1, Y + 1);
    Vec3 out;
    for (int c = 0; c < 3; ++c) {
        out[size_t(c)] = gradientScale2d(bilerp(gradient(h00[size_t(c)], fx, fy), gradient(h10[size_t(c)], fx - 1.0f, fy),
                                                gradient(h01[size_t(c)], fx, fy - 1.0f),
                                                gradient(h11[size_t(c)], fx - 1.0f, fy - 1.0f), u, v));
    }
    return out;
}

inline Vec3 perlinVec3(Vec3 p) {
    int X, Y, Z;
    const float fx = floorFrac(p[0], X);
    const float fy = floorFrac(p[1], Y);
    const float fz = floorFrac(p[2], Z);
    const float u = fade(fx);
    const float v = fade(fy);
    const float w = fade(fz);
    std::array<std::array<uint32_t, 3>, 8> h;
    for (int i = 0; i < 8; ++i) h[size_t(i)] = hashVec3(X + (i & 1), Y + ((i >> 1) & 1), Z + ((i >> 2) & 1));
    Vec3 out;
    for (int c = 0; c < 3; ++c) {
        const auto g = [&](int i) {
            return gradient(h[size_t(i)][size_t(c)], fx - float(i & 1), fy - float((i >> 1) & 1), fz - float((i >> 2) & 1));
        };
        out[size_t(c)] = gradientScale3d(trilerp(g(0), g(1), g(2), g(3), g(4), g(5), g(6), g(7), u, v, w));
    }
    return out;
}

inline float cell(Vec2 p) { return bitsTo01(hashInt(floorInt(p[0]), floorInt(p[1]))); }
inline float cell(Vec3 p) { return bitsTo01(hashInt(floorInt(p[0]), floorInt(p[1]), floorInt(p[2]))); }

inline Vec3 cellVec3(Vec2 p) {
    const int ix = floorInt(p[0]), iy = floorInt(p[1]);
    return {bitsTo01(hashInt(ix, iy, 0)), bitsTo01(hashInt(ix, iy, 1)), bitsTo01(hashInt(ix, iy, 2))};
}

inline Vec3 cellVec3(Vec3 p) {
    const int ix = floorInt(p[0]), iy = floorInt(p[1]), iz = floorInt(p[2]);
    return {bitsTo01(hashInt(ix, iy, iz, 0)), bitsTo01(hashInt(ix, iy, iz, 1)), bitsTo01(hashInt(ix, iy, iz, 2))};
}

// Distance to the nearest jittered feature point (F1, Euclidean).
inline float worley(Vec2 p, float jitter) {
    int X, Y;
    const Vec2 local{floorFrac(p[0], X), floorFrac(p[1], Y)};
    float best = 1e6f;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            const Vec3 tmp = cellVec3(Vec2{float(x + X), float(y + Y)});
            const float ox = (tmp[0] - 0.5f) * jitter + 0.5f;
            const float oy = (tmp[1] - 0.5f) * jitter + 0.5f;
            const float dx = float(x) + ox - local[0];
            const float dy = float(y) + oy - local[1];
            best = std::min(best, dx * dx + dy * dy);
        }
    }
    return std::sqrt(best);
}

inline float worley(Vec3 p, float jitter) {
    int X, Y, Z;
    const Vec3 local{floorFrac(p[0], X), floorFrac(p[1], Y), floorFrac(p[2], Z)};
    float best = 1e6f;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            for (int z = -1; z <= 1; ++z) {
                const Vec3 tmp = cellVec3(Vec3{float(x + X), float(y + Y), float(z + Z)});
                const float dx = float(x) + (tmp[0] - 0.5f) * jitter + 0.5f - local[0];
                const float dy = float(y) + (tmp[1] - 0.5f) * jitter + 0.5f - local[1];
                const float dz = float(z) + (tmp[2] - 0.5f) * jitter + 0.5f - local[2];
                best = std::min(best, dx * dx + dy * dy + dz * dz);
            }
        }
    }
    return std::sqrt(best);
}

}  // namespace systems::leal::campello_procedural::noise
