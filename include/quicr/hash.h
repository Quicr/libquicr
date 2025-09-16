// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "detail/utilities.h"

#include <array>
#include <concepts>
#include <cstdint>
#include <span>
#include <string_view>

namespace quicr {
    namespace detail {
        /**
         * @brief Computes a CityHash32 or CityHash64 hash based on the size of std::size_t.
         * @tparam Size The size of hash to use. Currently only 32 and 64 are supported.
         *
         * @note Original algorithm from https://github.com/google/cityhash
         */
        template<std::size_t Size = sizeof(std::size_t) * 8>
        class CityHash
        {
            static_assert((Size == 32 || Size == 64), "CityHash must be of valid size (32 or 64)");

            using uint128_t = std::array<std::uint64_t, 2>;

            static constexpr std::uint32_t c1 = 0xcc9e2d51;
            static constexpr std::uint32_t c2 = 0x1b873593;
            static constexpr std::uint64_t k0 = 0xc3a5c85c97cb3127ULL;
            static constexpr std::uint64_t k1 = 0xb492b66fbe98f273ULL;
            static constexpr std::uint64_t k2 = 0x9ae16a3b2f90404fULL;

          private:
            template<std::unsigned_integral T>
            constexpr T UnalignedLoad(std::span<const std::uint8_t> bytes)
            {
                T result = 0;
                if (std::is_constant_evaluated()) {
                    for (size_t i = 0; i < sizeof(T); ++i) {
                        result += bytes[i];
                        result <<= 8;
                    }
                } else {
                    std::memcpy(&result, bytes.data(), sizeof(T));
                }
                return result;
            }

            template<std::unsigned_integral T>
            constexpr T Fetch(std::span<const std::uint8_t> bytes)
            {
                return SwapBytes(UnalignedLoad<T>(bytes));
            }

            template<std::unsigned_integral T>
            constexpr std::uint32_t Rotate(T val, int shift)
            {
                return shift == 0 ? val : ((val >> shift) | (val << ((sizeof(T) * 8) - shift)));
            }

            constexpr std::uint32_t fmix(std::uint32_t h)
            {
                h ^= h >> 16;
                h *= 0x85ebca6b;
                h ^= h >> 13;
                h *= 0xc2b2ae35;
                h ^= h >> 16;
                return h;
            }

            constexpr std::uint32_t Mur(std::uint32_t a, std::uint32_t h)
            {
                a *= c1;
                a = Rotate(a, 17);
                a *= c2;
                h ^= a;
                h = Rotate(h, 19);
                return h * 5 + 0xe6546b64;
            }

            constexpr std::uint32_t Hash32Len13to24(std::span<const std::uint8_t> bytes)
            {
                const std::size_t kLen = bytes.size();

                std::uint32_t a = Fetch<std::uint32_t>(bytes.subspan((kLen >> 1) - 4));
                std::uint32_t b = Fetch<std::uint32_t>(bytes.subspan(4));
                std::uint32_t c = Fetch<std::uint32_t>(bytes.subspan(kLen - 8));
                std::uint32_t d = Fetch<std::uint32_t>(bytes.subspan(kLen >> 1));
                std::uint32_t e = Fetch<std::uint32_t>(bytes);
                std::uint32_t f = Fetch<std::uint32_t>(bytes.subspan(kLen - 4));
                std::uint32_t h = static_cast<std::uint32_t>(kLen);

                return fmix(Mur(f, Mur(e, Mur(d, Mur(c, Mur(b, Mur(a, h)))))));
            }

            constexpr std::uint32_t Hash32Len0to4(std::span<const std::uint8_t> bytes)
            {
                std::uint32_t b = 0;
                std::uint32_t c = 9;

                const std::size_t len = bytes.size();

                for (size_t i = 0; i < len; i++) {
                    signed char v = static_cast<signed char>(bytes[i]);
                    b = b * c1 + static_cast<std::uint32_t>(v);
                    c ^= b;
                }
                return fmix(Mur(b, Mur(static_cast<std::uint32_t>(len), c)));
            }

            constexpr std::uint32_t Hash32Len5to12(std::span<const std::uint8_t> bytes)
            {
                const std::size_t len = bytes.size();

                std::uint32_t a = static_cast<std::uint32_t>(len);
                std::uint32_t b = a * 5;
                std::uint32_t c = 9;
                std::uint32_t d = b;

                a += Fetch<std::uint32_t>(bytes);
                b += Fetch<std::uint32_t>(bytes.subspan(len - 4));
                c += Fetch<std::uint32_t>(bytes.subspan(((len >> 1) & 4)));

                return fmix(Mur(c, Mur(b, Mur(a, d))));
            }

            constexpr std::uint64_t ShiftMix(std::uint64_t val) { return val ^ (val >> 47); }

            constexpr std::uint64_t Hash128to64(const uint128_t& x)
            {
                constexpr std::uint64_t kMul = 0x9ddfea08eb382d69ULL;
                std::uint64_t a = (x[0] ^ x[1]) * kMul;
                a ^= (a >> 47);
                std::uint64_t b = (x[1] ^ a) * kMul;
                b ^= (b >> 47);
                b *= kMul;
                return b;
            }

            constexpr std::uint64_t HashLen16(std::uint64_t u, std::uint64_t v)
            {
                return Hash128to64(uint128_t{ u, v });
            }

            constexpr std::uint64_t HashLen16(std::uint64_t u, std::uint64_t v, std::uint64_t mul)
            {
                // Murmur-inspired hashing.
                std::uint64_t a = (u ^ v) * mul;
                a ^= (a >> 47);
                std::uint64_t b = (v ^ a) * mul;
                b ^= (b >> 47);
                b *= mul;
                return b;
            }

            constexpr std::uint64_t HashLen0to16(std::span<const std::uint8_t> bytes)
            {
                const std::size_t len = bytes.size();

                if (len >= 8) {
                    std::uint64_t mul = k2 + len * 2;
                    std::uint64_t a = Fetch<std::uint64_t>(bytes) + k2;
                    std::uint64_t b = Fetch<std::uint64_t>(bytes.subspan(len - 8));
                    std::uint64_t c = Rotate(b, 37) * mul + a;
                    std::uint64_t d = (Rotate(a, 25) + b) * mul;
                    return HashLen16(c, d, mul);
                }
                if (len >= 4) {
                    std::uint64_t mul = k2 + len * 2;
                    std::uint64_t a = Fetch<std::uint32_t>(bytes);
                    return HashLen16(len + (a << 3), Fetch<std::uint32_t>(bytes.subspan(len - 4)), mul);
                }
                if (len > 0) {
                    std::uint8_t a = static_cast<std::uint8_t>(bytes[0]);
                    std::uint8_t b = static_cast<std::uint8_t>(bytes[len >> 1]);
                    std::uint8_t c = static_cast<std::uint8_t>(bytes[len - 1]);
                    std::uint32_t y = static_cast<std::uint32_t>(a) + (static_cast<std::uint32_t>(b) << 8);
                    std::uint32_t z = static_cast<std::uint32_t>(len) + (static_cast<std::uint32_t>(c) << 2);
                    return ShiftMix(y * k2 ^ z * k0) * k2;
                }
                return k2;
            }

            constexpr std::uint64_t HashLen17to32(std::span<const std::uint8_t> bytes)
            {
                const std::size_t len = bytes.size();

                std::uint64_t mul = k2 + len * 2;
                std::uint64_t a = Fetch<std::uint64_t>(bytes) * k1;
                std::uint64_t b = Fetch<std::uint64_t>(bytes.subspan(8));
                std::uint64_t c = Fetch<std::uint64_t>(bytes.subspan(len - 8)) * mul;
                std::uint64_t d = Fetch<std::uint64_t>(bytes.subspan(len - 16)) * k2;
                return HashLen16(Rotate(a + b, 43) + Rotate(c, 30) + d, a + Rotate(b + k2, 18) + c, mul);
            }

            constexpr uint128_t WeakHashLen32WithSeeds(std::uint64_t w,
                                                       std::uint64_t x,
                                                       std::uint64_t y,
                                                       std::uint64_t z,
                                                       std::uint64_t a,
                                                       std::uint64_t b)
            {
                a += w;
                b = Rotate(b + a + z, 21);
                std::uint64_t c = a;
                a += x;
                a += y;
                b += Rotate(a, 44);
                return { a + z, b + c };
            }

            constexpr uint128_t WeakHashLen32WithSeeds(std::span<const std::uint8_t> bytes,
                                                       std::uint64_t a,
                                                       std::uint64_t b)
            {
                return WeakHashLen32WithSeeds(Fetch<std::uint64_t>(bytes),
                                              Fetch<std::uint64_t>(bytes.subspan(8)),
                                              Fetch<std::uint64_t>(bytes.subspan(16)),
                                              Fetch<std::uint64_t>(bytes.subspan(24)),
                                              a,
                                              b);
            }

            constexpr std::uint64_t HashLen33to64(std::span<const std::uint8_t> bytes)
            {
                const std::size_t len = bytes.size();
                std::uint64_t mul = k2 + len * 2;
                std::uint64_t a = Fetch<std::uint64_t>(bytes) * k2;
                std::uint64_t b = Fetch<std::uint64_t>(bytes.subspan(8));
                std::uint64_t c = Fetch<std::uint64_t>(bytes.subspan(len - 24));
                std::uint64_t d = Fetch<std::uint64_t>(bytes.subspan(len - 32));
                std::uint64_t e = Fetch<std::uint64_t>(bytes.subspan(16)) * k2;
                std::uint64_t f = Fetch<std::uint64_t>(bytes.subspan(24)) * 9;
                std::uint64_t g = Fetch<std::uint64_t>(bytes.subspan(len - 8));
                std::uint64_t h = Fetch<std::uint64_t>(bytes.subspan(len - 16)) * mul;
                std::uint64_t u = Rotate(a + g, 43) + (Rotate(b, 30) + c) * 9;
                std::uint64_t v = ((a + g) ^ d) + f + 1;
                std::uint64_t w = SwapBytes((u + v) * mul) + h;
                std::uint64_t x = Rotate(e + f, 42) + c;
                std::uint64_t y = (SwapBytes((v + w) * mul) + g) * mul;
                std::uint64_t z = e + f + c;

                a = SwapBytes((x + z) * mul + y) + b;
                b = ShiftMix((z + a) * mul + d + h) * mul;

                return b + x;
            }

          public:
            constexpr std::size_t operator()(std::span<const std::uint8_t>) { return -1; }
        };

        template<>
        constexpr std::size_t CityHash<32>::operator()(std::span<const std::uint8_t> bytes)
        {
            const std::size_t len = bytes.size();

            if (len <= 4) {
                return Hash32Len0to4(bytes);
            } else if (len <= 12) {
                return Hash32Len5to12(bytes);
            } else if (len <= 24) {
                return Hash32Len13to24(bytes);
            }

            std::uint32_t a0 = Rotate(Fetch<std::uint32_t>(bytes.subspan(len - 4)) * c1, 17) * c2;
            std::uint32_t a1 = Rotate(Fetch<std::uint32_t>(bytes.subspan(len - 8)) * c1, 17) * c2;
            std::uint32_t a2 = Rotate(Fetch<std::uint32_t>(bytes.subspan(len - 16)) * c1, 17) * c2;
            std::uint32_t a3 = Rotate(Fetch<std::uint32_t>(bytes.subspan(len - 12)) * c1, 17) * c2;
            std::uint32_t a4 = Rotate(Fetch<std::uint32_t>(bytes.subspan(len - 20)) * c1, 17) * c2;

            std::uint32_t h = static_cast<std::uint32_t>(len);
            std::uint32_t g = c1 * h;
            std::uint32_t f = g;

            h ^= a0;
            h = Rotate(h, 19);
            h = h * 5 + 0xe6546b64;
            h ^= a2;
            h = Rotate(h, 19);
            h = h * 5 + 0xe6546b64;
            g ^= a1;
            g = Rotate(g, 19);
            g = g * 5 + 0xe6546b64;
            g ^= a3;
            g = Rotate(g, 19);
            g = g * 5 + 0xe6546b64;
            f += a4;
            f = Rotate(f, 19);
            f = f * 5 + 0xe6546b64;

            std::size_t iters = (len - 1) / 20;

            do {
                std::uint32_t a0 = Rotate(Fetch<std::uint32_t>(bytes) * c1, 17) * c2;
                std::uint32_t a1 = Fetch<std::uint32_t>(bytes.subspan(4));
                std::uint32_t a2 = Rotate(Fetch<std::uint32_t>(bytes.subspan(8)) * c1, 17) * c2;
                std::uint32_t a3 = Rotate(Fetch<std::uint32_t>(bytes.subspan(12)) * c1, 17) * c2;
                std::uint32_t a4 = Fetch<std::uint32_t>(bytes.subspan(16));
                h ^= a0;
                h = Rotate(h, 18);
                h = h * 5 + 0xe6546b64;
                f += a1;
                f = Rotate(f, 19);
                f = f * c1;
                g += a2;
                g = Rotate(g, 18);
                g = g * 5 + 0xe6546b64;
                h ^= a3 + a1;
                h = Rotate(h, 19);
                h = h * 5 + 0xe6546b64;
                g ^= a4;
                g = SwapBytes(g) * 5;
                h += a4 * 5;
                h = SwapBytes(h);
                f += a0;

                std::swap(f, h);
                std::swap(f, g);

                bytes = bytes.subspan(20);
            } while (--iters != 0);

            g = Rotate(g, 11) * c1;
            g = Rotate(g, 17) * c1;
            f = Rotate(f, 11) * c1;
            f = Rotate(f, 17) * c1;
            h = Rotate(h + g, 19);
            h = h * 5 + 0xe6546b64;
            h = Rotate(h, 17) * c1;
            h = Rotate(h + f, 19);
            h = h * 5 + 0xe6546b64;
            h = Rotate(h, 17) * c1;
            return h;
        }

        template<>
        constexpr std::size_t CityHash<64>::operator()(std::span<const std::uint8_t> bytes)
        {
            std::size_t len = bytes.size();

            if (len <= 16) {
                return HashLen0to16(bytes);
            } else if (len <= 32) {
                return HashLen17to32(bytes);
            } else if (len <= 64) {
                return HashLen33to64(bytes);
            }

            // For strings over 64 bytes we hash the end first, and then as we
            // loop we keep 56 bytes of state: v, w, x, y, and z.
            std::uint64_t x = Fetch<std::uint64_t>(bytes.subspan(len - 40));
            std::uint64_t y =
              Fetch<std::uint64_t>(bytes.subspan(len - 16)) + Fetch<std::uint64_t>(bytes.subspan(len - 56));
            std::uint64_t z = HashLen16(Fetch<std::uint64_t>(bytes.subspan(len - 48)) + len,
                                        Fetch<std::uint64_t>(bytes.subspan(len - 24)));
            uint128_t v = WeakHashLen32WithSeeds(bytes.subspan(len - 64), len, z);
            uint128_t w = WeakHashLen32WithSeeds(bytes.subspan(len - 32), y + k1, x);
            x = x * k1 + Fetch<std::uint64_t>(bytes);

            // Decrease len to the nearest multiple of 64, and operate on 64-byte chunks.
            len = (len - 1) & ~static_cast<size_t>(63);
            do {
                x = Rotate(x + y + v[0] + Fetch<std::uint64_t>(bytes.subspan(8)), 37) * k1;
                y = Rotate(y + v[1] + Fetch<std::uint64_t>(bytes.subspan(48)), 42) * k1;
                x ^= w[1];
                y += v[0] + Fetch<std::uint64_t>(bytes.subspan(40));
                z = Rotate(z + w[0], 33) * k1;
                v = WeakHashLen32WithSeeds(bytes, v[1] * k1, x + w[0]);
                w = WeakHashLen32WithSeeds(bytes.subspan(32), z + w[1], y + Fetch<std::uint64_t>(bytes.subspan(16)));
                std::swap(z, x);
                bytes = bytes.subspan(64);
                len -= 64;
            } while (len != 0);

            return HashLen16(HashLen16(v[0], w[0]) + ShiftMix(y) * k1 + z, HashLen16(v[1], w[1]) + x);
        }
    }

    /**
     * @brief Hash a span of bytes to a 64bit number.
     * @param bytes The bytes to hash.
     * @returns The 64bit hash of the given given bytes.
     */
    constexpr std::uint64_t hash(std::span<const std::uint8_t> bytes)
    {
        return detail::CityHash{}(bytes);
    }

    /**
     * @brief Combine (aka add) hash to existing hash
     *
     * @details Adds/combines new hash to existing hash. Existing hash will
     *       be updated.
     *
     * @param[in,out]   seed    Existing hash to update
     * @param[in]       value   New hash to add to the existing (combine)
     */
    constexpr void hash_combine(std::uint64_t& seed, const std::uint64_t& value)
    {
        seed ^= value + 0x9e3779b9 + (seed << 6) + (value >> 2);
    }
}

template<>
struct std::hash<std::span<const std::uint8_t>>
{
    constexpr std::size_t operator()(std::span<const std::uint8_t> bytes) const { return quicr::hash(bytes); }
};
