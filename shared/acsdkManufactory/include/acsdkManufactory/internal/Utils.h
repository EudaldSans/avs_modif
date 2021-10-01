/*
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *     http://aws.amazon.com/apache2.0/
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#ifndef ACSDKMANUFACTORY_INTERNAL_UTILS_H_
#define ACSDKMANUFACTORY_INTERNAL_UTILS_H_

#include <tuple>
#include <type_traits>

#include "acsdkManufactory/Import.h"
#include "acsdkManufactory/OptionalImport.h"
#include "acsdkManufactory/internal/CookBook.h"
#include "acsdkManufactory/internal/MakeOptional.h"

namespace alexaClientSDK {
namespace acsdkManufactory {
namespace internal {

/**
 * Template for performing an operation on all elements of a parameter pack.
 *
 * The template is invoked via Fold::Apply<Operation, Result, An...> where:
 *
 * Operation is a struct with a template named Operation::Apply of the form: Apply<Result, Type>
 * where Result is the incoming accumulated result and Type is the new type to operate
 * upon.  The type resulting from Operation::Apply must define a new result type that
 * defines 'type' as the result.
 *
 * Result is the Result value passed in to the first invocation of Operation::Apply.
 *
 * An... is the parameter pack whose types will be iterated over.
 *
 * Note that Fold uses explicit loop unrolling to minimize nesting of template expansions
 * and the resulting unfortunate behavior of some compilers.
 */
struct Fold {
    template <typename...>
    struct Apply;

    template <typename Operation, typename Result0>
    struct Apply<Operation, Result0> {
        using type = Result0;
    };

    template <typename Operation, typename Result0, typename A1>
    struct Apply<Operation, Result0, A1> {
        using type = typename Operation::template Apply<Result0, A1>::type;
    };

    template <typename Operation, typename Result0, typename A1, typename A2>
    struct Apply<Operation, Result0, A1, A2> {
        using Result1 = typename Operation::template Apply<Result0, A1>::type;
        using type = typename Operation::template Apply<Result1, A2>::type;
    };

    template <typename Operation, typename Result0, typename A1, typename A2, typename A3>
    struct Apply<Operation, Result0, A1, A2, A3> {
        using Result1 = typename Operation::template Apply<Result0, A1>::type;
        using Result2 = typename Operation::template Apply<Result1, A2>::type;
        using type = typename Operation::template Apply<Result2, A3>::type;
    };

    template <typename Operation, typename Result0, typename A1, typename A2, typename A3, typename A4, typename... An>
    struct Apply<Operation, Result0, A1, A2, A3, A4, An...> {
        using Result1 = typename Operation::template Apply<Result0, A1>::type;
        using Result2 = typename Operation::template Apply<Result1, A2>::type;
        using Result3 = typename Operation::template Apply<Result2, A3>::type;
        using Result4 = typename Operation::template Apply<Result3, A4>::type;
        using type = typename Apply<Operation, Result4, An...>::type;
    };

    template <
        typename Operation,
        typename Result0,
        typename A1,
        typename A2,
        typename A3,
        typename A4,
        typename A5,
        typename A6,
        typename A7,
        typename... An>
    struct Apply<Operation, Result0, A1, A2, A3, A4, A5, A6, A7, An...> {
        using Result1 = typename Operation::template Apply<Result0, A1>::type;
        using Result2 = typename Operation::template Apply<Result1, A2>::type;
        using Result3 = typename Operation::template Apply<Result2, A3>::type;
        using Result4 = typename Operation::template Apply<Result3, A4>::type;
        using Result5 = typename Operation::template Apply<Result4, A5>::type;
        using Result6 = typename Operation::template Apply<Result5, A6>::type;
        using Result7 = typename Operation::template Apply<Result6, A7>::type;
        using type = typename Apply<Operation, Result7, An...>::type;
    };

    template <
        typename Operation,
        typename Result0,
        typename A1,
        typename A2,
        typename A3,
        typename A4,
        typename A5,
        typename A6,
        typename A7,
        typename A8,
        typename A9,
        typename A10,
        typename A11,
        typename A12,
        typename... An>
    struct Apply<Operation, Result0, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11, A12, An...> {
        using Result1 = typename Operation::template Apply<Result0, A1>::type;
        using Result2 = typename Operation::template Apply<Result1, A2>::type;
        using Result3 = typename Operation::template Apply<Result2, A3>::type;
        using Result4 = typename Operation::template Apply<Result3, A4>::type;
        using Result5 = typename Operation::template Apply<Result4, A5>::type;
        using Result6 = typename Operation::template Apply<Result5, A6>::type;
        using Result7 = typename Operation::template Apply<Result6, A7>::type;
        using Result8 = typename Operation::template Apply<Result7, A8>::type;
        using Result9 = typename Operation::template Apply<Result8, A9>::type;
        using Result10 = typename Operation::template Apply<Result9, A10>::type;
        using Result11 = typename Operation::template Apply<Result10, A11>::type;
        using Result12 = typename Operation::template Apply<Result11, A12>::type;
        using type = typename Apply<Operation, Result12, An...>::type;
    };
};

/**
 * Template for performing an operation on the types defining a std::tuple.
 *
 * The template is invoked via Fold::Apply<Operation, Result, std::tuple<An...>> where:
 *
 * Operation is a struct with a template named Operation::Apply of the form: Apply<Result, Type>
 * where Result is the incoming accumulated result and Type is the new type to operate
 * upon.  The type resulting from Operation::Apply must define a new result type that
 * defines 'type' as the result.
 *
 * Result is the Result value passed in to the first invocation of Operation::Apply.
 *
 * An... is the parameter pack whose types will be iterated over.
 */
struct FoldTupleTypes {
    template <typename...>
    struct Apply;

    template <typename Operation, typename Result0, typename... Types>
    struct Apply<Operation, Result0, std::tuple<Types...>> {
        using type = typename Fold::Apply<Operation, Result0, Types...>::type;
    };
};

/**
 * Template for determining at compile time if a parameter pack contains a specific type.
 *
 * If the parameter pack Types... contains Type, ContainsType<std::tuple<Types...>, Type>::value is true.
 *
 * @tparam ... Template parameters of the form <std::tuple<Types...>, Type> where @c Types is inspected to see
 * of it includes @c Type.
 */
template <typename...>
struct ContainsType;

template <typename Type, typename... ContainedTypes>
struct ContainsType<std::tuple<ContainedTypes...>, Type> {
    template <typename InType>
    struct IsFalse {
        constexpr static const bool value = false;
    };

    template <bool... InTypes>
    struct BoolValues;

    using FalseValues = BoolValues<IsFalse<ContainedTypes>::value...>;
    using SameValues = BoolValues<std::is_same<Type, ContainedTypes>::value...>;
    constexpr static const bool value = !std::is_same<FalseValues, SameValues>::value;
};

/**
 * Template for determining at compile time if a parameter pack defining a tuple contains all the elements of
 * another parameter pack.
 *
 * If the parameter pack (ContainerTypes...) contains all the types in another parameter pack (ContainedTypes...),
 * ContainsTypes<std::tuple<ContainerTypes...>, ContainedTypes...>::value is true.
 *
 * @tparam ... Template parameters of the form <std::tuple<ContainerTypes...>, ContainedTypes...>.
 */
template <typename...>
struct ContainsTypes;

template <typename Container, typename... Types>
struct ContainsTypes<Container, Types...> {
    template <typename InType>
    struct IsTrue {
        constexpr static const bool value = true;
    };

    template <bool... InTypes>
    struct BoolValues;

    using TrueValues = BoolValues<IsTrue<Types>::value...>;
    using ContainedValues = BoolValues<ContainsType<Container, Types>::value...>;
    constexpr static const bool value = std::is_same<TrueValues, ContainedValues>::value;
};

/**
 * Template for determining at compile time if a parameter pack defining a tuple contains all the elements of
 * a parameter pack defining a second tuple.
 *
 * If the parameter pack (ContainerTypes...) contains all the types in another parameter pack (ContainedTypes...),
 * ContainsTupleTypes<std::tuple<ContainerTypes...>, std::tuple<ContainedTypes...>>::value is true.
 *
 * @tparam ... Template parameters of the form <std::tuple<ContainerTypes...>, std::tuple<ContainedTypes...>>.
 */
template <typename...>
struct ContainsTupleTypes;

template <typename... ContainerTypes, typename... TupleTypes>
struct ContainsTupleTypes<std::tuple<ContainerTypes...>, std::tuple<TupleTypes...>>
        : ContainsTypes<std::tuple<ContainerTypes...>, TupleTypes...> {};

/**
 * Template for determining if a type is an imported type (i.e. Import<...> or OptionalImport<...>).
 *
 * If the specified type (Type) is an Import<type>, IsImport<Type>::value is true.
 *
 * @tparam ... Template parameters of the form <Type>.
 */
template <typename...>
struct IsImport;

template <typename Type>
struct IsImport<Import<Type>> : std::true_type {};

template <typename Type>
struct IsImport<OptionalImport<Type>> : std::true_type {};

template <typename Type>
struct IsImport<Type> : std::false_type {};

/**
 * Template for determining if a type is a required import type (i.e. Import<...> and not OptionalImport<...>).
 *
 * If the specified type (Type) is an Import<type>, IsImport<Type>::value is true.
 *
 * @tparam ... Template parameters of the form <Type>.
 */
template <typename...>
struct IsRequiredImport;

template <typename Type>
struct IsRequiredImport<Import<Type>> : std::true_type {};

template <typename Type>
struct IsRequiredImport<Type> : std::false_type {};

/**
 * Template to determine if a parameter pack contains any required imports types.
 *
 * If the parameter pack Types... includes any imports, HasRequiredImport<Types...>::value is true.
 *
 * @tparam ... Template parameters of the form <Types...>.  Where Types... is the parameter pack to inspect.
 */
template <typename... Types>
struct HasRequiredImport {
    template <typename InType>
    struct IsFalse {
        constexpr static const bool value = false;
    };

    template <bool... InTypes>
    struct BoolValues;

    using FalseValues = BoolValues<IsFalse<Types>::value...>;
    using IsImportValues = BoolValues<IsRequiredImport<Types>::value...>;
    constexpr static const bool value = !std::is_same<FalseValues, IsImportValues>::value;
};

/**
 * Template to eliminate duplicate types in a parameter pack.
 *
 * For the parameter pack Types..., DeDupTypes<std::Types...>::type is a std::tuple<Results...>
 * where Results... is a de-duped version of Types...
 *
 * @tparam ... Template parameters of the form <Types...> where Types... is the parameter pack
 * to de-dup.
 */
template <typename... Types>
struct DedupTypes {
    struct DedupOperation {
        template <typename...>
        struct Apply;

        template <typename... ResultTypes, typename Type>
        struct Apply<std::tuple<ResultTypes...>, Type> {
            using type = typename std::conditional<
                ContainsType<std::tuple<ResultTypes...>, Type>::value,
                std::tuple<ResultTypes...>,
                std::tuple<ResultTypes..., Type>>::type;
        };
    };

    using type = typename Fold::Apply<DedupOperation, std::tuple<>, Types...>::type;
};

/**
 * Template to remove types from a parameter pack.
 *
 * For the parameter pack UnwantedTypes... and Types...,
 * RemoveTypes<std::tuple<UnwantedTypes...>, std::tuple<Types...>>::type
 * is a std::tuple<Results...> where Results... is a version of Types... with all UnwantedTypes... removed.
 *
 * @tparam ... Template parameters of the form <std::tuple<Types...>, std::type<UnwantedTypes...>> where
 * UnwantedTypes... is the set of types to remove from Types...
 */
template <typename...>
struct RemoveTypes;

template <typename... Types, typename... Unwanted>
struct RemoveTypes<std::tuple<Types...>, std::tuple<Unwanted...>> {
    struct RemoveTypesOperation {
        template <typename...>
        struct Apply;

        template <typename... ResultTypes, typename Type>
        struct Apply<std::tuple<ResultTypes...>, Type> {
            using type = typename std::conditional<
                ContainsType<std::tuple<Unwanted...>, Type>::value,
                std::tuple<ResultTypes...>,
                std::tuple<ResultTypes..., Type>>::type;
        };
    };

    using type = typename Fold::Apply<RemoveTypesOperation, std::tuple<>, Types...>::type;
};

/**
 * Template to help extract the set of imported and exported types from a @c Component<> or @c ComponentAllocator<>
 * parameter pack.
 *
 * For the parameter pack Types..., GetImportsAndExportsHelper<std::tuple<>, std::tuple<>, Types...> the members
 * @c exports and @c imports are std::tuple<Results...> where Results... are the lists of types exported and imported.
 *
 * @tparam ... template parameters of the form <std::tuple<>, std::tuple<>, Types...> where Types... is the
 * set of types to split into exports and imports.
 */
template <typename... Types>
struct GetImportsAndExports {
    using UniqueParameters = typename DedupTypes<Types...>::type;

    struct GetExportsOperation {
        template <typename...>
        struct Apply;

        template <typename... ResultTypes, typename Type>
        struct Apply<std::tuple<ResultTypes...>, Type> {
            using type = typename std::
                conditional<IsImport<Type>::value, std::tuple<ResultTypes...>, std::tuple<ResultTypes..., Type>>::type;
        };
    };

    using Exports = typename FoldTupleTypes::Apply<GetExportsOperation, std::tuple<>, UniqueParameters>::type;

    struct GetMakeOptionalImportsOperation {
        template <typename...>
        struct Apply;

        template <typename... ResultTypes, typename Type>
        struct Apply<std::tuple<ResultTypes...>, MakeOptional<Type>> {
            using type = std::tuple<ResultTypes..., Type>;
        };

        template <typename... ResultTypes, typename Type>
        struct Apply<std::tuple<ResultTypes...>, Type> {
            using type = std::tuple<ResultTypes...>;
        };
    };

    using MakeOptionalImports =
        typename FoldTupleTypes::Apply<GetMakeOptionalImportsOperation, std::tuple<>, UniqueParameters>::type;

    struct GetOptionalImportsOperation {
        template <typename...>
        struct Apply;

        template <typename... ResultTypes, typename Type>
        struct Apply<std::tuple<ResultTypes...>, OptionalImport<Type>> {
            using type = std::tuple<ResultTypes..., Type>;
        };

        template <typename... ResultTypes, typename Type>
        struct Apply<std::tuple<ResultTypes...>, MakeOptional<Type>> {
            using type = std::tuple<ResultTypes..., Type>;
        };

        template <typename... ResultTypes, typename Type>
        struct Apply<std::tuple<ResultTypes...>, Type> {
            using type = std::tuple<ResultTypes...>;
        };
    };

    /**
     * Set of optional imports is everything marked with MakeOptional<> and OptionalImport<> tags.
     */
    using OptionalImports =
        typename FoldTupleTypes::Apply<GetOptionalImportsOperation, std::tuple<>, UniqueParameters>::type;

    struct GetRequiredImportsOperation {
        template <typename...>
        struct Apply;

        template <typename... ResultTypes, typename Type>
        struct Apply<std::tuple<ResultTypes...>, Import<Type>> {
            using type = std::tuple<ResultTypes..., Type>;
        };

        template <typename... ResultTypes, typename Type>
        struct Apply<std::tuple<ResultTypes...>, Type> {
            using type = std::tuple<ResultTypes...>;
        };
    };

    /**
     * Set of required imports is everything marked with Import<> tag that hasn't been marked with MakeOptional<>.
     */
    using DeclaredRequiredImports =
        typename FoldTupleTypes::Apply<GetRequiredImportsOperation, std::tuple<>, UniqueParameters>::type;

    using RequiredImports = typename RemoveTypes<DeclaredRequiredImports, MakeOptionalImports>::type;

    /**
     * Set of unsatisfied required imports is the set of required imports that are not being exported.
     */
    using UnsatisfiedRequiredImports = typename RemoveTypes<RequiredImports, Exports>::type;

    /**
     * Set of unsatisfied optional imports is the set of optional imports that are not being exported and that's not
     * required.
     */
    using UnsatisfiedOptionalImports =
        typename RemoveTypes<typename RemoveTypes<OptionalImports, Exports>::type, UnsatisfiedRequiredImports>::type;

    struct type {
        using exports = Exports;
        using required = UnsatisfiedRequiredImports;
        using optional = UnsatisfiedOptionalImports;
    };
};

/**
 * This structure instantiate empty instances for the provided types and add them to the given cook book.
 *
 * This structure is used to implement optional import when the dependency is not available.
 * @tparam Types The types that will be inspected. The default values will be added only to types that match
 * OptionalImport<...>.
 */
template <typename... Types>
struct DefaultValues;

template <typename Type, typename... Types>
struct DefaultValues<OptionalImport<Type>, Types...> {
    /**
     * Apply the default value for the optional @c Type provided, and continue to inspect the remainder types.
     *
     * @param cookBook The cook book that will hold the new empty instance.
     */
    static inline void apply(internal::CookBook& cookBook) {
        cookBook.addInstance(Type());
        DefaultValues<Types...>::apply(cookBook);
    }
};

template <typename Type, typename... Types>
struct DefaultValues<Type, Types...> {
    /**
     * Skip @c Type since it's not optional, and continue to inspect the remainder types.
     *
     * @param cookBook The cook book that will hold any new empty instance.
     */
    static inline void apply(internal::CookBook& cookBook) {
        DefaultValues<Types...>::apply(cookBook);
    }
};

template <>
struct DefaultValues<> {
    /**
     * This is the base case that is used to finish the type inspection.
     *
     * @param cookBook The cook book that may have been modified to carry any new empty instance.
     */
    static inline void apply(internal::CookBook& cookBook) {
    }
};

/**
 * This class and its specializations can be used to print missing export types as a compilation error.
 *
 * Usage:
 *   PrintMissingExport<ListOfTypes...>()();
 *   PrintMissingExport<std::tuple<ListOfTypes>>()();
 *
 * Both cases will be a no-op if ListOfTypes resolves to an empty list; otherwise, compiler will fail since operator()
 * is not defined in the generic class.
 *
 * @tparam Types List of missing exports.
 */
template <typename... Types>
struct PrintMissingExport {};

template <>
struct PrintMissingExport<> {
    void operator()() {
    }
};

template <>
struct PrintMissingExport<std::tuple<>> : PrintMissingExport<> {};

/**
 * This class and its specializations can be used to print missing import types as a compilation error.
 *
 * Usage:
 *   PrintMissingImport<ListOfTypes...>()();
 *   PrintMissingImport<std::tuple<ListOfTypes>>()();
 *
 * Both cases will be a no-op if ListOfTypes resolves to an empty list; otherwise, compiler will fail since operator()
 * is not defined in the generic class.
 *
 * @tparam Types List of missing exports.
 */
template <typename... Types>
struct PrintMissingImport {};

template <>
struct PrintMissingImport<> {
    void operator()() {
    }
};

template <>
struct PrintMissingImport<std::tuple<>> : PrintMissingImport<> {};

}  // namespace internal
}  // namespace acsdkManufactory
}  // namespace alexaClientSDK

#endif  // ACSDKMANUFACTORY_INTERNAL_UTILS_H_