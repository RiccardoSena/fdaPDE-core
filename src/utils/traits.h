// This file is part of fdaPDE, a C++ library for physics-informed
// spatial and functional data analysis.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#ifndef __FDAPDE_TRAITS_H__
#define __FDAPDE_TRAITS_H__

#include <tuple>
#include <type_traits>

namespace fdapde {

// deduces the return type of the subscript operator with arguments Args... applied to type T
template <typename T, typename... Args> struct subscript_result_of {
    using type = decltype(std::declval<T>().operator[](std::declval<Args>()...));
};

// trait to detect if a type is a base of a template
template <template <typename...> typename B, typename D> struct is_base_of_template {
    using U = typename std::decay<D>::type;
    // valid match (derived-to-base conversion applies)
    template <typename... Args> static std::true_type test(B<Args...>&);
    // any other match is false (D cannot be converted to its base type B)
    static std::false_type test(...);
    static constexpr bool value = decltype(test(std::declval<U&>()))::value;
};

// returns true if type T is instance of template E<F> with F some type.
template <typename T, template <typename...> typename E> struct is_instance_of : std::false_type { };
template <typename... T, template <typename...> typename E>   // valid match
struct is_instance_of<E<T...>, E> : std::true_type { };

// metaprogramming routines for working on std::tuple<>-based typelists

// returns std::true_type if tuple contains type T
template <typename T, typename Tuple> struct has_type { };
// an empty tuple cannot contain T, return false
template <typename T> struct has_type<T, std::tuple<>> : std::false_type { };
// if the head of the tuple is not of type T, go on recursively on the remaining types
template <typename T, typename U, typename... Args>
struct has_type<T, std::tuple<U, Args...>> : has_type<T, std::tuple<Args...>> { };
// in case the head of the tuple is type T, end of recursion and return true
template <typename T, typename... Args> struct has_type<T, std::tuple<T, Args...>> : std::true_type { };

// returns std::true_type if tuple contains an instantiation of template E<F>
template <template <typename F> typename E, typename Tuple> struct has_instance_of { };
template <template <typename F> typename E>   // empty tuple cannot contain anything
struct has_instance_of<E, std::tuple<>> : std::false_type { };
template <typename F, template <typename> typename E, typename... Tail>   // type found, stop recursion
struct has_instance_of<E, std::tuple<E<F>, Tail...>> : std::true_type { };
template <typename U, template <typename> typename E, typename... Tail>   // recursive step
struct has_instance_of<E, std::tuple<U, Tail...>> {
    static constexpr bool value = has_instance_of<E, std::tuple<Tail...>>::value;
};

// trait to detect whether all types in a parameter pack are unique
template <typename... Ts> struct unique_types;
// consider a pair of types and develop a tree of matches starting from them
template <typename T1, typename T2, typename... Ts> struct unique_types<T1, T2, Ts...> {
    static constexpr bool value =
      !std::is_same<T1, T2>::value && unique_types<T1, Ts...>::value && unique_types<T2, Ts...>::value;
};
// end of recursion
template <typename T1, typename T2> struct unique_types<T1, T2> {
    static constexpr bool value = !std::is_same<T1, T2>::value;
};
// degenerate case (a list made of one type is unique)
template <typename T1> struct unique_types<T1> {
    static constexpr bool value = true;
};

// obtain index of type in tuple (assume types are unique in the std::tuple)
template <typename T, typename tuple> struct index_of;
template <typename T, typename... Ts> struct index_of<T, std::tuple<Ts...>> {
   private:
    template <std::size_t... idx> static constexpr int find_idx(std::index_sequence<idx...>) {
        // if a type in the parameter pack Ts matches (std::is_same<> returns true) return idx+1-1 = idx, which
        // corresponds to the index of type in the tuple. Otherwise if type not found returns -1.
        return -1 + ((std::is_same<T, Ts>::value ? idx + 1 : 0) + ...);
    }
   public:
    static constexpr int index = find_idx(std::index_sequence_for<Ts...> {});
};

// trait to detect if a type is contained in a tuple by checking if the index_of the type in the tuple is not -1
template <typename T, typename Tuple> struct has_type;
template <typename T, typename... Ts> struct has_type<T, std::tuple<Ts...>> {
    static constexpr bool value = index_of<T, std::tuple<Ts...>>::index != -1;
};

// evaluate metafunction based on condition
template <bool b, typename T, typename F> struct eval_if {
    using type = typename T::type;
};
template <typename T, typename F> struct eval_if<false, T, F> {
    using type = typename F::type;
};

// a compile time switch for selecting between multiple types based on condition
template <bool b, typename T> struct switch_type_case {   // case type
    static constexpr bool value = b;
    using type = T;
};
template <typename Head, typename... Tail> struct switch_type {
    using type = typename eval_if<Head::value, Head, switch_type<Tail...>>::type;
};
template <typename Head> struct switch_type<Head> {   // end of recursion
    static_assert(Head::value, "no true condition in switch_type statement");
    using type = typename Head::type;
};

}   // namespace fdapde

#endif   // __FDAPDE_TRAITS_H__
