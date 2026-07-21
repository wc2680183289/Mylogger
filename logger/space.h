#pragma once

#include <ratio>
#include <cstddef>

namespace logger {

template <class _Rep, class _Period = std::ratio<1>>
class space;

template <typename _ToSpace, typename _Rep, typename _Period>
constexpr _ToSpace space_cast(const space<_Rep,_Period>& __d){
    auto __n = __d.count() * std::ratio_divide<_Period, typename _ToSpace::period>::num /
            std::ratio_divide<_Period, typename _ToSpace::period>::den;
    return _ToSpace(__n);
}

template <typename _Rep, typename _Period>
class space{
public:
    using rep = _Rep;
    using period = _Period;

    constexpr space() : _rep(){}

    template<typename _Rep2>
    constexpr space(const _Rep2& _r) : _rep(_r){}

    template<typename _Rep2, typename _Period2>
    constexpr space(const space<_Rep2, _Period2>&_d) : _rep(space_cast<space>(_d).count()){}

    constexpr _Rep count() const { return _rep; }

    constexpr space operator+() const { return *this; }

    constexpr space operator-() const { return space(-count()); }

    space& operator++() {
        ++_rep;
        return *this;
    }

    space operator++(int) {
        space _temp(*this);
        ++_rep;
        return _temp;
    }

    space& operator--() {
        --_rep;
        return *this;
    }

    space operator--(int) {
        space _temp(*this);
        --_rep;
        return _temp;
    }

    space& operator*=(const _Rep& _d) {
        _rep *= _d;
        return *this;
    }

    space& operator/=(const _Rep& _d) {
        _rep /= _d;
        return *this;
    }

    space& operator+=(const space& _d) {
        _rep += _d;
        return *this;
    }

    space& operator-=(const space& _d) {
        _rep -= _d;
        return *this;
    }

    space& operator%=(const _Rep& _d) {
        _rep %= _d;
        return *this;
    }
private:
    rep _rep;
};

template<typename _Rep, typename _Period>
constexpr space<_Rep, _Period> operator*(const space<_Rep, _Period>&_x, const _Rep&_s) {
    return space<_Rep, _Period>(_x.count() * _s);
}

template<typename _Rep, typename _Period>
constexpr space<_Rep, _Period> operator/(const space<_Rep, _Period>&_x, const _Rep&_s) {
    return space<_Rep, _Period>(_x.count() / _s);
}

template<typename _Rep, typename _Period>
constexpr space<_Rep, _Period> operator+(const space<_Rep, _Period>&_x, const space<_Rep, _Period>&_s) {
    return space<_Rep, _Period>(_x.count() + _s.count());
}

template<typename _Rep, typename _Period>
constexpr space<_Rep, _Period> operator-(const space<_Rep, _Period>&_x, const space<_Rep, _Period>&_s) {
    return space<_Rep, _Period>(_x.count() - _s.count());
}

using kilo = std::ratio<1024L, 1>;
using mega = std::ratio<1024L * 1024, 1>;
using giga = std::ratio<1024L * 1024 * 1024, 1>;
using tera = std::ratio<1024L * 1024 * 1024 * 1024, 1>;

using bytes = space<std::size_t>;
using kilobytes = space<std::size_t, kilo>;
using megabytes = space<std::size_t, mega>;
using gigabytes = space<std::size_t, giga>;
using terabytes = space<std::size_t, tera>;

}