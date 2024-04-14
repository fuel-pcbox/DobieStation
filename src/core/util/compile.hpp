#pragma once
#include <utility>

namespace util
{
    /* This header implements a set of compiler tools that allow
       for compile-time generation of data sets */
    template <typename T, T... S, typename F>
    constexpr void constexpr_for(std::integer_sequence<T, S...>, F f) 
    {
        (static_cast<void>(f(std::integral_constant<T, S>{})), ...);
    }

    template<auto N, typename F>
    constexpr void constexpr_for(F f) 
    {
        constexpr_for(std::make_integer_sequence<decltype(N), N>{}, f);
    }

    template <bool cond_v, typename Then, typename OrElse>
    decltype(auto) constexpr_if(Then&& then, OrElse&& or_else) 
    {
        if constexpr (cond_v)
            return std::forward<Then>(then);
        else
            return std::forward<OrElse>(or_else);
    }
}