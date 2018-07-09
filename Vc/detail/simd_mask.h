/*  This file is part of the Vc library. {{{
Copyright © 2016-2017 Matthias Kretz <kretz@kde.org>

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the names of contributing organizations nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

}}}*/

#ifndef VC_SIMD_MASK_H_
#define VC_SIMD_MASK_H_

#include "synopsis.h"
#include "smart_reference.h"
#include <bitset>

Vc_VERSIONED_NAMESPACE_BEGIN

template <class T, class Abi> class simd_mask : public detail::traits<T, Abi>::mask_base
{
    using traits = detail::traits<T, Abi>;
    using impl = typename traits::mask_impl_type;
    using member_type = typename traits::mask_member_type;
    static constexpr detail::size_tag_type<T, Abi> size_tag = {};
    static constexpr T *type_tag = nullptr;
    friend typename traits::mask_base;
    friend class simd<T, Abi>;  // to construct masks on return
    friend impl;
    friend typename traits::simd_impl_type;  // to construct masks on return and
                                             // inspect data on masked operations

public:
    using value_type = bool;
    using reference = detail::smart_reference<member_type, impl, value_type>;
    using simd_type = simd<T, Abi>;
    using abi_type = Abi;

    static constexpr size_t size()
    {
        constexpr size_t N = size_tag;
        return N;
    }
    simd_mask() = default;
    simd_mask(const simd_mask &) = default;
    simd_mask(simd_mask &&) = default;
    simd_mask &operator=(const simd_mask &) = default;
    simd_mask &operator=(simd_mask &&) = default;

    // access to internal representation (suggested extension)
    explicit Vc_ALWAYS_INLINE simd_mask(typename traits::mask_cast_type init) : d{init} {}
    // conversions to internal type is done in mask_base

    // bitset interface
    static Vc_ALWAYS_INLINE simd_mask from_bitset(std::bitset<size()> bs)
    {
        return {detail::bitset_init, bs};
    }
    std::bitset<size()> Vc_ALWAYS_INLINE to_bitset() const {
        if constexpr (is_scalar()) {
            return unsigned(d);
        } else if constexpr (is_fixed()) {
            return d;
        } else {
            return detail::to_bitset(builtin());
        }
    }

    // explicit broadcast constructor
    explicit constexpr Vc_ALWAYS_INLINE simd_mask(value_type x) : d(broadcast(x)) {}

    // implicit type conversion constructor
    template <class U>
    Vc_ALWAYS_INLINE simd_mask(
        const simd_mask<U, simd_abi::fixed_size<size()>> &x,
        std::enable_if_t<detail::all<std::is_same<abi_type, simd_abi::fixed_size<size()>>,
                                     std::is_same<U, U>>::value,
                         detail::nullarg_t> = detail::nullarg)
        : simd_mask{detail::bitset_init, detail::data(x)}
    {
    }
    /* reference implementation for explicit simd_mask casts
    template <class U>
    simd_mask(const simd_mask<U, Abi> &x,
         enable_if<
             (size() == simd_mask<U, Abi>::size()) &&
             detail::all<std::is_integral<T>, std::is_integral<U>,
             detail::negation<std::is_same<Abi, simd_abi::fixed_size<size()>>>,
             detail::negation<std::is_same<T, U>>>::value> = nullarg)
        : d{x.d}
    {
    }
    template <class U, class Abi2>
    simd_mask(const simd_mask<U, Abi2> &x,
         enable_if<detail::all<
         detail::negation<std::is_same<abi_type, Abi2>>,
             std::is_same<abi_type, simd_abi::fixed_size<size()>>>::value> = nullarg)
    {
        x.copy_to(&d[0], vector_aligned);
    }
    */


    // load impl
private:
    static constexpr bool is_scalar() { return std::is_same_v<abi_type, simd_abi::scalar>; }
    static constexpr bool is_sse() { return std::is_same_v<abi_type, simd_abi::__sse>; }
    static constexpr bool is_avx() { return std::is_same_v<abi_type, simd_abi::__avx>; }
    static constexpr bool is_avx512() { return std::is_same_v<abi_type, simd_abi::__avx512>; }
    static constexpr bool is_fixed() { return detail::is_fixed_size_abi_v<abi_type>; }

    template <class F>
    static Vc_INTRINSIC member_type load_wrapper(const value_type *mem, F f)
    {
        if constexpr (is_scalar()) {
            return mem[0];
        } else if constexpr (is_fixed()) {
            const fixed_size_simd<unsigned char, size()> bools(
                reinterpret_cast<const detail::may_alias<unsigned char> *>(mem), f);
            return detail::data(bools != 0);
        } else if constexpr (is_sse()) {
            if constexpr (size() == 2 && detail::have_sse2) {
                return detail::to_storage(_mm_set_epi32(-int(mem[1]), -int(mem[1]),
                                                        -int(mem[0]), -int(mem[0])));
            } else if constexpr (size() == 4 && detail::have_sse2) {
                __m128i k = _mm_cvtsi32_si128(*reinterpret_cast<const int *>(mem));
                k = _mm_cmpgt_epi16(_mm_unpacklo_epi8(k, k), _mm_setzero_si128());
                return detail::to_storage(_mm_unpacklo_epi16(k, k));
            } else if constexpr (size() == 4 && detail::have_mmx) {
                __m128 k =
                    _mm_cvtpi8_ps(_mm_cvtsi32_si64(*reinterpret_cast<const int *>(mem)));
                _mm_empty();
                return detail::to_storage(_mm_cmpgt_ps(k, __m128()));
            } else if constexpr (size() == 8 && detail::have_sse2) {
                const auto k = detail::make_builtin<long long>(
                    *reinterpret_cast<const detail::may_alias<long long> *>(mem), 0);
                if constexpr (detail::have_sse2) {
                    return detail::to_storage(
                        detail::builtin_cast<short>(_mm_unpacklo_epi8(k, k)) != 0);
                }
            } else if constexpr (size() == 16 && detail::have_sse2) {
                return _mm_cmpgt_epi8(detail::builtin_load<long long, 2>(mem, f),
                                      __m128i());
            } else {
                detail::assert_unreachable<F>();
            }
        } else if constexpr (is_avx()) {
            if constexpr (size() == 4 && detail::have_avx) {
                int bool4;
                if constexpr (detail::is_aligned_v<F, 4>) {
                    bool4 = *reinterpret_cast<const detail::may_alias<int> *>(mem);
                } else {
                    std::memcpy(&bool4, mem, 4);
                }
                const auto k = detail::to_intrin(
                    (detail::builtin_broadcast<4>(bool4) &
                     detail::make_builtin<int>(0x1, 0x100, 0x10000, 0x1000000)) != 0);
                return detail::to_storage(
                    detail::concat(_mm_unpacklo_epi32(k, k), _mm_unpackhi_epi32(k, k)));
            } else if constexpr (size() == 8 && detail::have_avx) {
                auto k = detail::builtin_load<long long, 2, 8>(mem, f);
                k = _mm_cmpgt_epi16(_mm_unpacklo_epi8(k, k), __m128i());
                return detail::to_storage(
                    detail::concat(_mm_unpacklo_epi16(k, k), _mm_unpackhi_epi16(k, k)));
            } else if constexpr (size() == 16 && detail::have_avx) {
                const auto k =
                    _mm_cmpgt_epi8(detail::builtin_load<long long, 2>(mem, f), __m128i());
                return detail::concat(_mm_unpacklo_epi8(k, k), _mm_unpackhi_epi8(k, k));
            } else if constexpr (size() == 32 && detail::have_avx2) {
                return _mm256_cmpgt_epi8(detail::builtin_load<long long, 4>(mem, f),
                                         __m256i());
            } else {
                detail::assert_unreachable<F>();
            }
        } else if constexpr (is_avx512()) {
            if constexpr (size() == 8) {
                const auto a = detail::builtin_load<long long, 2, 8>(mem, f);
                if constexpr (detail::have_avx512bw_vl) {
                    return _mm_test_epi8_mask(a, a);
                } else {
                    const auto b = _mm512_cvtepi8_epi64(a);
                    return _mm512_test_epi64_mask(b, b);
                }
            } else if constexpr (size() == 16) {
                const auto a = detail::builtin_load<long long, 2>(mem, f);
                if constexpr (detail::have_avx512bw_vl) {
                    return _mm_test_epi8_mask(a, a);
                } else {
                    const auto b = _mm512_cvtepi8_epi32(a);
                    return _mm512_test_epi32_mask(b, b);
                }
            } else if constexpr (size() == 32) {
                if constexpr (detail::have_avx512bw_vl) {
                    const auto a = detail::builtin_load<long long, 4>(mem, f);
                    return _mm256_test_epi8_mask(a, a);
                } else {
                    const auto a =
                        _mm512_cvtepi8_epi32(detail::builtin_load<long long, 2>(mem, f));
                    const auto b = _mm512_cvtepi8_epi32(
                        detail::builtin_load<long long, 2>(mem + 16, f));
                    return _mm512_test_epi32_mask(a, a) |
                           (_mm512_test_epi32_mask(b, b) << 16);
                }
            } else if constexpr (size() == 64) {
                if constexpr (detail::have_avx512bw) {
                    const auto a = detail::builtin_load<long long, 8>(mem, f);
                    return _mm512_test_epi8_mask(a, a);
                } else {
                    const auto a =
                        _mm512_cvtepi8_epi32(detail::builtin_load<long long, 2>(mem, f));
                    const auto b = _mm512_cvtepi8_epi32(
                        detail::builtin_load<long long, 2>(mem + 16, f));
                    const auto c = _mm512_cvtepi8_epi32(
                        detail::builtin_load<long long, 2>(mem + 32, f));
                    const auto d = _mm512_cvtepi8_epi32(
                        detail::builtin_load<long long, 2>(mem + 48, f));
                    return _mm512_test_epi32_mask(a, a) |
                           (_mm512_test_epi32_mask(b, b) << 16) |
                           (_mm512_test_epi32_mask(b, b) << 32) |
                           (_mm512_test_epi32_mask(b, b) << 48);
                }
            } else {
                detail::assert_unreachable<F>();
            }
        } else {
            detail::assert_unreachable<F>();
        }
        detail::unused(f);  // not true, see PR85827
    }

public :
    // load constructor
    template <class Flags>
    Vc_ALWAYS_INLINE simd_mask(const value_type *mem, Flags f) : d(load_wrapper(mem, f))
    {
    }
    template <class Flags>
    Vc_ALWAYS_INLINE simd_mask(const value_type *mem, simd_mask k, Flags f) : d{}
    {
        d = impl::masked_load(d, k.d, mem, f, size_tag);
    }

    // loads [simd_mask.load]
    template <class Flags> Vc_ALWAYS_INLINE void copy_from(const value_type *mem, Flags f)
    {
        d = load_wrapper(mem, f);
    }

    // stores [simd_mask.store]
    template <class Flags> Vc_ALWAYS_INLINE void copy_to(value_type *mem, Flags f) const
    {
        impl::store(d, mem, f, size_tag);
    }

    // scalar access
    Vc_ALWAYS_INLINE reference operator[](size_t i) { return {d, int(i)}; }
    Vc_ALWAYS_INLINE value_type operator[](size_t i) const {
        if constexpr (is_scalar()) {
            Vc_ASSERT(i == 0);
            detail::unused(i);
            return d;
        } else {
            return d[i];
        }
    }

    // negation
    Vc_ALWAYS_INLINE simd_mask operator!() const
    {
        if constexpr (is_scalar()) {
            return {detail::private_init, !d};
        } else if constexpr (is_avx512() || is_fixed()) {
            return simd_mask(detail::private_init, ~builtin());
        } else {
            return {detail::private_init,
                    detail::to_storage(~detail::builtin_cast<uint>(builtin()))};
        }
    }

    // simd_mask binary operators [simd_mask.binary]
    friend Vc_ALWAYS_INLINE simd_mask operator&&(const simd_mask &x, const simd_mask &y)
    {
        return {detail::private_init, impl::logical_and(x.d, y.d)};
    }
    friend Vc_ALWAYS_INLINE simd_mask operator||(const simd_mask &x, const simd_mask &y)
    {
        return {detail::private_init, impl::logical_or(x.d, y.d)};
    }

    friend Vc_ALWAYS_INLINE simd_mask operator&(const simd_mask &x, const simd_mask &y)
    {
        return {detail::private_init, impl::bit_and(x.d, y.d)};
    }
    friend Vc_ALWAYS_INLINE simd_mask operator|(const simd_mask &x, const simd_mask &y)
    {
        return {detail::private_init, impl::bit_or(x.d, y.d)};
    }
    friend Vc_ALWAYS_INLINE simd_mask operator^(const simd_mask &x, const simd_mask &y)
    {
        return {detail::private_init, impl::bit_xor(x.d, y.d)};
    }

    friend Vc_ALWAYS_INLINE simd_mask &operator&=(simd_mask &x, const simd_mask &y)
    {
        x.d = impl::bit_and(x.d, y.d);
        return x;
    }
    friend Vc_ALWAYS_INLINE simd_mask &operator|=(simd_mask &x, const simd_mask &y)
    {
        x.d = impl::bit_or(x.d, y.d);
        return x;
    }
    friend Vc_ALWAYS_INLINE simd_mask &operator^=(simd_mask &x, const simd_mask &y)
    {
        x.d = impl::bit_xor(x.d, y.d);
        return x;
    }

    // simd_mask compares [simd_mask.comparison]
    friend Vc_ALWAYS_INLINE simd_mask operator==(const simd_mask &x, const simd_mask &y)
    {
        return !operator!=(x, y);
    }
    friend Vc_ALWAYS_INLINE simd_mask operator!=(const simd_mask &x, const simd_mask &y)
    {
        return {detail::private_init, impl::bit_xor(x.d, y.d)};
    }

    // "private" because of the first arguments's namespace
    Vc_INTRINSIC simd_mask(detail::private_init_t, typename traits::mask_member_type init)
        : d(init)
    {
    }

    // "private" because of the first arguments's namespace
    template <class F, class = decltype(bool(std::declval<F>()(size_t())))>
    Vc_INTRINSIC simd_mask(detail::private_init_t, F &&gen)
    {
        for (size_t i = 0; i < size(); ++i) {
            impl::set(d, i, gen(i));
        }
    }

    // "private" because of the first arguments's namespace
    Vc_INTRINSIC simd_mask(detail::bitset_init_t, std::bitset<size()> init)
        : d(impl::from_bitset(init, type_tag))
    {
    }

private:
    static constexpr Vc_INTRINSIC member_type broadcast(value_type x)
    {
        if constexpr (is_scalar()) {
            return x;
        } else if constexpr (is_fixed()) {
            return x ? ~member_type() : member_type();
        } else if constexpr (is_avx512()) {
            using mmask_type = typename detail::bool_storage_member_type<size()>::type;
            return x ? static_cast<mmask_type>(~mmask_type()) : mmask_type();
        } else {
            using U = detail::builtin_type_t<detail::int_for_sizeof_t<T>, size()>;
            return detail::to_storage(x ? ~U() : U());
        }
    }

    auto intrin() const
    {
        if constexpr (!is_scalar() && !is_fixed()) {
            return detail::to_intrin(d.d);
        }
    }

    auto &builtin() {
        if constexpr (is_scalar() || is_fixed()) {
            return d;
        } else {
            return d.d;
        }
    }
    const auto &builtin() const
    {
        if constexpr (is_scalar() || is_fixed()) {
            return d;
        } else {
            return d.d;
        }
    }

    friend const auto &detail::data<T, abi_type>(const simd_mask &);
    friend auto &detail::data<T, abi_type>(simd_mask &);
    alignas(traits::mask_member_alignment) member_type d;
};

namespace detail
{
template <class T, class A>
constexpr Vc_INTRINSIC const auto &data(const simd_mask<T, A> &x)
{
    return x.d;
}
template <class T, class A> constexpr Vc_INTRINSIC auto &data(simd_mask<T, A> &x)
{
    return x.d;
}
}  // namespace detail

Vc_VERSIONED_NAMESPACE_END
#endif  // VC_SIMD_MASK_H_

// vim: foldmethod=marker
