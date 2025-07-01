// Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "justlang/ast/parser.hpp"

#include <optional>
#include <string>
#include <unordered_map>

#include <catch2/catch_test_macros.hpp>

#include "justlang/ast/to_string_visitor.hpp"
#include "justlang/file_data.hpp"

namespace {

using file_map_t = std::unordered_map<std::string, std::string>;

[[nodiscard]] auto CreateMocReader(file_map_t const* files)
    -> justlang::FileData::reader_t {
    return [files](auto const& /*import_from*/,
                   auto const& file_path,
                   auto const* /*repo*/) -> std::optional<justlang::FileData> {
        if (auto found = files->find(file_path); found != files->end()) {
            return justlang::FileData{
                .location =
                    justlang::FileLocation{
                        .repo = "", .path = file_path, .content = nullptr},
                .content = found->second};
        }
        return std::nullopt;
    };
}

[[nodiscard]] auto nativeParser(std::string const& code,
                                std::string const& entry = "<stdin>",
                                file_map_t const& files = {}) -> std::string {
    auto file_data = justlang::FileData{
        .location = justlang::FileLocation{.repo = "", .path = entry},
        .content = code};
    auto parser = justlang::Parser::Create(CreateMocReader(&files));
    auto ast = parser->ParseData(file_data);
    return justlang::ASTToStringVisitor{}.Dump(*ast);
}

TEST_CASE("TestOperators", "[basic_operators]") {
    SECTION("simple") {
        auto const* code = R"(
            local foo = 'foo';
            {
              foo: foo,
            })";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: foo
- VALUE:
    STRING(foo)
- NEXT:
    MAP
    - KEY0:
        STRING(foo)
    - VAL0:
        READ
        - TARGET: "foo"
)");
    }

    SECTION("variables inlining other variables") {
        auto const* code = R"(
          local foo = 'foo';
          local bar = foo;
          {
            foo: bar,
          })";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: foo
- VALUE:
    STRING(foo)
- NEXT:
    LET
    - NAME: bar
    - VALUE:
        READ
        - TARGET: "foo"
    - NEXT:
        MAP
        - KEY0:
            STRING(foo)
        - VAL0:
            READ
            - TARGET: "bar"
)");
    }

    SECTION("variables in computed field") {
        auto const* code = R"(
          local foo = 'foo';
          {
            [foo]: foo,
          })";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: foo
- VALUE:
    STRING(foo)
- NEXT:
    MAP
    - KEY0:
        READ
        - TARGET: "foo"
    - VAL0:
        READ
        - TARGET: "foo"
)");
    }

    SECTION("computed variable field") {
        auto const* code = R"(
          local map = {[jst.env('NAME')]: 'foo'};
          {
            foo: map.foo,
          })";

        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: map
- VALUE:
    MAP
    - KEY0:
        CALL
        - TARGET:
            UNRESOLVED_LOOKUP
            - INDEX:
                STRING(env)
            - CONTAINER:
                READ
                - TARGET: "jst"
        - <arg0>:
            STRING(NAME)
    - VAL0:
        STRING(foo)
- NEXT:
    MAP
    - KEY0:
        STRING(foo)
    - VAL0:
        UNRESOLVED_LOOKUP
        - INDEX:
            STRING(foo)
        - CONTAINER:
            READ
            - TARGET: "map"
)");
    }
}

TEST_CASE("ast_parser", "[variables]") {
    SECTION("simple") {
        auto const* code = R"(
          local foo(x) = x;
          {
            foo: foo('foo'),
          }
        )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: foo
- VALUE:
    FUNC
    - x:
null
    - BODY:
        READ
        - TARGET: "x"
- NEXT:
    MAP
    - KEY0:
        STRING(foo)
    - VAL0:
        CALL
        - TARGET: "foo"
        - <arg0>:
            STRING(foo)
)");
    }

    SECTION("functions inlining other functions") {
        auto const* code = R"(
              local foo(x) = x;
              local bar(x) = foo(x);
              {
                foo: bar('foo'),
              }
            )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: foo
- VALUE:
    FUNC
    - x:
null
    - BODY:
        READ
        - TARGET: "x"
- NEXT:
    LET
    - NAME: bar
    - VALUE:
        FUNC
        - x:
null
        - BODY:
            CALL
            - TARGET: "foo"
            - <arg0>:
                READ
                - TARGET: "x"
    - NEXT:
        MAP
        - KEY0:
            STRING(foo)
        - VAL0:
            CALL
            - TARGET: "bar"
            - <arg0>:
                STRING(foo)
)");
    }

    SECTION("functions in computed field") {
        auto const* code = R"(
              local foo(x) = x;
              {
                [foo('foo')]: foo('foo'),
              }
            )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: foo
- VALUE:
    FUNC
    - x:
null
    - BODY:
        READ
        - TARGET: "x"
- NEXT:
    MAP
    - KEY0:
        CALL
        - TARGET: "foo"
        - <arg0>:
            STRING(foo)
    - VAL0:
        CALL
        - TARGET: "foo"
        - <arg0>:
            STRING(foo)
)");
    }

    SECTION("computed variable field") {
        auto const* code = R"(
        local map = {[jst.env('NAME')]: 'foo'};
        {
          foo: map.foo,
        })";

        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: map
- VALUE:
    MAP
    - KEY0:
        CALL
        - TARGET:
            UNRESOLVED_LOOKUP
            - INDEX:
                STRING(env)
            - CONTAINER:
                READ
                - TARGET: "jst"
        - <arg0>:
            STRING(NAME)
    - VAL0:
        STRING(foo)
- NEXT:
    MAP
    - KEY0:
        STRING(foo)
    - VAL0:
        UNRESOLVED_LOOKUP
        - INDEX:
            STRING(foo)
        - CONTAINER:
            READ
            - TARGET: "map"
)");
    }
    SECTION("variable shadows previous variable") {
        auto const* code = R"(
        local foo = 'bar';
        local foo = 'foo';
        {
          foo: foo,
        })";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: foo
- VALUE:
    STRING(bar)
- NEXT:
    LET
    - NAME: foo
    - VALUE:
        STRING(foo)
    - NEXT:
        MAP
        - KEY0:
            STRING(foo)
        - VAL0:
            READ
            - TARGET: "foo"
)");
    }
}

TEST_CASE("ast_parser", "[functions]") {

    SECTION("functions with default argument") {
        auto const* code = R"(
            local foo(x='test') = x;
            {
              foo: foo(),
              bar: foo('foo'),
            }
          )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: foo
- VALUE:
    FUNC
    - x:
        STRING(test)
    - BODY:
        READ
        - TARGET: "x"
- NEXT:
    MAP
    - KEY0:
        STRING(foo)
    - VAL0:
        CALL
        - TARGET: "foo"
    - KEY1:
        STRING(bar)
    - VAL1:
        CALL
        - TARGET: "foo"
        - <arg0>:
            STRING(foo)
)");
    }

    SECTION("functions with inlining default argument") {
        auto const* code = R"(
            local foo(x='test') = x;
            local bar(x=foo()) = x;
            {
              foo: bar(),
              bar: bar('foo'),
            }
          )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: foo
- VALUE:
    FUNC
    - x:
        STRING(test)
    - BODY:
        READ
        - TARGET: "x"
- NEXT:
    LET
    - NAME: bar
    - VALUE:
        FUNC
        - x:
            CALL
            - TARGET: "foo"
        - BODY:
            READ
            - TARGET: "x"
    - NEXT:
        MAP
        - KEY0:
            STRING(foo)
        - VAL0:
            CALL
            - TARGET: "bar"
        - KEY1:
            STRING(bar)
        - VAL1:
            CALL
            - TARGET: "bar"
            - <arg0>:
                STRING(foo)
)");
    }

    SECTION("functions shadow previous functions") {
        auto const* code = R"(
            local foo(x) = [x, 'foo'];
            local foo(x) = [x, 'bar'];
            {
              foo: foo('foo'),
            }
          )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: foo
- VALUE:
    FUNC
    - x:
null
    - BODY:
        [
          READ
          - TARGET: "x"
          STRING(foo)
        ]
- NEXT:
    LET
    - NAME: foo
    - VALUE:
        FUNC
        - x:
null
        - BODY:
            [
              READ
              - TARGET: "x"
              STRING(bar)
            ]
    - NEXT:
        MAP
        - KEY0:
            STRING(foo)
        - VAL0:
            CALL
            - TARGET: "foo"
            - <arg0>:
                STRING(foo)
)");
    }

    SECTION("variables should not shadow params in preceding functions") {
        auto const* code = R"(
            local a = 'bar';
            local foo(x) = [x, a];
            local a = 'foo';
            {
              foo: foo('foo'),
            }
          )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: a
- VALUE:
    STRING(bar)
- NEXT:
    LET
    - NAME: foo
    - VALUE:
        FUNC
        - x:
null
        - BODY:
            [
              READ
              - TARGET: "x"
              READ
              - TARGET: "a"
            ]
    - NEXT:
        LET
        - NAME: a
        - VALUE:
            STRING(foo)
        - NEXT:
            MAP
            - KEY0:
                STRING(foo)
            - VAL0:
                CALL
                - TARGET: "foo"
                - <arg0>:
                    STRING(foo)
)");
    }

    SECTION("no defintion should shadow function arguments") {
        auto const* code = R"(
            local x = 'bar';
            local foo(x) = [x, 'bar'];
            local x = 'baz';
            {
              foo: foo('foo'),
            }
          )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: x
- VALUE:
    STRING(bar)
- NEXT:
    LET
    - NAME: foo
    - VALUE:
        FUNC
        - x:
null
        - BODY:
            [
              READ
              - TARGET: "x"
              STRING(bar)
            ]
    - NEXT:
        LET
        - NAME: x
        - VALUE:
            STRING(baz)
        - NEXT:
            MAP
            - KEY0:
                STRING(foo)
            - VAL0:
                CALL
                - TARGET: "foo"
                - <arg0>:
                    STRING(foo)
)");
    }

    SECTION(
        "variables should not shadow default values in preceding functions") {
        auto const* code = R"(
            local y = 'foo';
            local foo(x=y) = [x, 'bar'];
            local y = 'bar';
            {
              foo: foo(),
            }
          )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: y
- VALUE:
    STRING(foo)
- NEXT:
    LET
    - NAME: foo
    - VALUE:
        FUNC
        - x:
            READ
            - TARGET: "y"
        - BODY:
            [
              READ
              - TARGET: "x"
              STRING(bar)
            ]
    - NEXT:
        LET
        - NAME: y
        - VALUE:
            STRING(bar)
        - NEXT:
            MAP
            - KEY0:
                STRING(foo)
            - VAL0:
                CALL
                - TARGET: "foo"
)");
    }

    SECTION("outer variables should not shadow inner function variables") {
        auto const* code = R"(
            local y = 'bar';
            local foo(x=y) =
              local x = 'foo';
              [x, 'bar'];
            local x = 'bar';
            {
              foo: foo(),
            }
          )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: y
- VALUE:
    STRING(bar)
- NEXT:
    LET
    - NAME: foo
    - VALUE:
        FUNC
        - x:
            READ
            - TARGET: "y"
        - BODY:
            LET
            - NAME: x
            - VALUE:
                STRING(foo)
            - NEXT:
                [
                  READ
                  - TARGET: "x"
                  STRING(bar)
                ]
    - NEXT:
        LET
        - NAME: x
        - VALUE:
            STRING(bar)
        - NEXT:
            MAP
            - KEY0:
                STRING(foo)
            - VAL0:
                CALL
                - TARGET: "foo"
)");
    }

    SECTION("inner function params should shadow outer function params") {
        auto const* code = R"(
            local foo(x) =
              local bar(x) = x;
              [bar('foo'), 'bar'];
            {
              foo: foo('bar'),
            }
          )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: foo
- VALUE:
    FUNC
    - x:
null
    - BODY:
        LET
        - NAME: bar
        - VALUE:
            FUNC
            - x:
null
            - BODY:
                READ
                - TARGET: "x"
        - NEXT:
            [
              CALL
              - TARGET: "bar"
              - <arg0>:
                  STRING(foo)
              STRING(bar)
            ]
- NEXT:
    MAP
    - KEY0:
        STRING(foo)
    - VAL0:
        CALL
        - TARGET: "foo"
        - <arg0>:
            STRING(bar)
)");
    }

    SECTION("function param defaults can access preceding params") {
        auto const* code = R"(
            local a = 'foo';
            local foo(a, b=a) = [a, b];
            {
              foo: foo('bar'),
            }
          )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: a
- VALUE:
    STRING(foo)
- NEXT:
    LET
    - NAME: foo
    - VALUE:
        FUNC
        - a:
null
        - b:
            READ
            - TARGET: "a"
        - BODY:
            [
              READ
              - TARGET: "a"
              READ
              - TARGET: "b"
            ]
    - NEXT:
        MAP
        - KEY0:
            STRING(foo)
        - VAL0:
            CALL
            - TARGET: "foo"
            - <arg0>:
                STRING(bar)
)");
    }

    SECTION("functions with nested variables") {
        auto const* code = R"(
        local foo(x) =
          local bar = jst.env(x); // x satisfies requirement to be string type
          bar;
        {
          foo: foo('foo')
        }
      )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: foo
- VALUE:
    FUNC
    - x:
null
    - BODY:
        LET
        - NAME: bar
        - VALUE:
            CALL
            - TARGET:
                UNRESOLVED_LOOKUP
                - INDEX:
                    STRING(env)
                - CONTAINER:
                    READ
                    - TARGET: "jst"
            - <arg0>:
                READ
                - TARGET: "x"
        - NEXT:
            READ
            - TARGET: "bar"
- NEXT:
    MAP
    - KEY0:
        STRING(foo)
    - VAL0:
        CALL
        - TARGET: "foo"
        - <arg0>:
            STRING(foo)
)");
    }
}

TEST_CASE("ast_parser", "[special strings]") {
    SECTION("ref-strings") {
        SECTION("plain reference") {
            auto const* code = R"(@':foo')";
            auto output = nativeParser(code);
            CHECK(output == R"(REF("foo")
)");
        }

        SECTION("reference with ''") {
            auto const* code = R"(@':foo''bar')";
            auto output = nativeParser(code);
            CHECK(output == R"(REF("foo'bar")
)");
        }

        SECTION("reference as JSON") {
            auto const* code = R"(@':"foo"')";
            auto output = nativeParser(code);
            CHECK(output == R"(REF("foo")
)");
        }

        SECTION("reference as JSON with special chars") {
            auto const* code = R"(@':"foo \"\n bar"')";
            auto output = nativeParser(code);
            CHECK(output == R"(REF("foo \"\n bar")
)");
        }
    }
}

TEST_CASE("ast_parser", "[built-in function evaluation]") {
    SECTION("keys()") {
        SECTION("static data") {
            auto const* code = R"(
            local test(x) = jst.keys(x);
            local map = {key0: 'val0', key1: 'val1'};
            {
              foo: test(map),
            }
          )";
            auto output = nativeParser(code);
            CHECK(output == R"(LET
- NAME: test
- VALUE:
    FUNC
    - x:
null
    - BODY:
        CALL
        - TARGET:
            UNRESOLVED_LOOKUP
            - INDEX:
                STRING(keys)
            - CONTAINER:
                READ
                - TARGET: "jst"
        - <arg0>:
            READ
            - TARGET: "x"
- NEXT:
    LET
    - NAME: map
    - VALUE:
        MAP
        - KEY0:
            STRING(key0)
        - VAL0:
            STRING(val0)
        - KEY1:
            STRING(key1)
        - VAL1:
            STRING(val1)
    - NEXT:
        MAP
        - KEY0:
            STRING(foo)
        - VAL0:
            CALL
            - TARGET: "test"
            - <arg0>:
                READ
                - TARGET: "map"
)");
        }

        SECTION("runtime data") {
            auto const* code = R"(
            local test(x) = jst.keys(x);
            {
              foo: test(jst.env('map')),
            }
          )";
            auto output = nativeParser(code);
            CHECK(output == R"(LET
- NAME: test
- VALUE:
    FUNC
    - x:
null
    - BODY:
        CALL
        - TARGET:
            UNRESOLVED_LOOKUP
            - INDEX:
                STRING(keys)
            - CONTAINER:
                READ
                - TARGET: "jst"
        - <arg0>:
            READ
            - TARGET: "x"
- NEXT:
    MAP
    - KEY0:
        STRING(foo)
    - VAL0:
        CALL
        - TARGET: "test"
        - <arg0>:
            CALL
            - TARGET:
                UNRESOLVED_LOOKUP
                - INDEX:
                    STRING(env)
                - CONTAINER:
                    READ
                    - TARGET: "jst"
            - <arg0>:
                STRING(map)
)");
        }
    }
}

TEST_CASE("ast_parser", "[type deduction]") {
    SECTION("both operands with static type info") {
        SECTION("variables") {
            auto const* code = R"(
            local foo = 'foo';
            local bar = 'bar';
            {
              foo: foo + bar,
            }
          )";
            auto output = nativeParser(code);
            CHECK(output == R"(LET
- NAME: foo
- VALUE:
    STRING(foo)
- NEXT:
    LET
    - NAME: bar
    - VALUE:
        STRING(bar)
    - NEXT:
        MAP
        - KEY0:
            STRING(foo)
        - VAL0:
            BINARY_OPERATION(unknown)
            - LHS:
                READ
                - TARGET: "foo"
            - RHS:
                READ
                - TARGET: "bar"
)");
        }

        SECTION("functions") {
            auto const* code = R"(
            local foo() = 'foo';
            local bar() = 'bar';
            {
              foo: foo() + bar(),
            }
          )";
            auto output = nativeParser(code);
            CHECK(output == R"(LET
- NAME: foo
- VALUE:
    FUNC
    - BODY:
        STRING(foo)
- NEXT:
    LET
    - NAME: bar
    - VALUE:
        FUNC
        - BODY:
            STRING(bar)
    - NEXT:
        MAP
        - KEY0:
            STRING(foo)
        - VAL0:
            BINARY_OPERATION(unknown)
            - LHS:
                CALL
                - TARGET: "foo"
            - RHS:
                CALL
                - TARGET: "bar"
)");
        }

        SECTION("loop variable") {
            auto const* code = R"(
            {
              foo: [x + x for x in ['bar', 'baz']],
            }
          )";
            auto output = nativeParser(code);
            CHECK(output == R"(MAP
- KEY0:
    STRING(foo)
- VAL0:
    FOREACH
    - VAR: x
    - RANGE:
        [
          STRING(bar)
          STRING(baz)
        ]
    - BODY:
        BINARY_OPERATION(unknown)
        - LHS:
            READ
            - TARGET: "x"
        - RHS:
            READ
            - TARGET: "x"
)");
        }

        SECTION("sum numbers") {
            auto const* code = R"(
            {
              foo: 5 + 6
            })";
            auto output = nativeParser(code);
            CHECK(output == R"(MAP
- KEY0:
    STRING(foo)
- VAL0:
    BINARY_OPERATION(unknown)
    - LHS:
        NUMBER(5)
    - RHS:
        NUMBER(6)
)");
        }

        SECTION("builtin functions") {

            auto const* code = R"(
            {
              foo: 'foo' + jst.join(['bar']), // join evaluates to string
            }
          )";
            auto output = nativeParser(code);
            CHECK(output == R"(MAP
- KEY0:
    STRING(foo)
- VAL0:
    BINARY_OPERATION(unknown)
    - LHS:
        STRING(foo)
    - RHS:
        CALL
        - TARGET:
            UNRESOLVED_LOOKUP
            - INDEX:
                STRING(join)
            - CONTAINER:
                READ
                - TARGET: "jst"
        - <arg0>:
            [
              STRING(bar)
            ]
)");
        }

        SECTION("builtin functions in function body") {
            auto const* code = R"(
            local foo(x) = jst.join([x]) + jst.join([x]);
            {
              foo: foo('foo')
            }
          )";
            auto output = nativeParser(code);
            CHECK(output == R"(LET
- NAME: foo
- VALUE:
    FUNC
    - x:
null
    - BODY:
        BINARY_OPERATION(unknown)
        - LHS:
            CALL
            - TARGET:
                UNRESOLVED_LOOKUP
                - INDEX:
                    STRING(join)
                - CONTAINER:
                    READ
                    - TARGET: "jst"
            - <arg0>:
                [
                  READ
                  - TARGET: "x"
                ]
        - RHS:
            CALL
            - TARGET:
                UNRESOLVED_LOOKUP
                - INDEX:
                    STRING(join)
                - CONTAINER:
                    READ
                    - TARGET: "jst"
            - <arg0>:
                [
                  READ
                  - TARGET: "x"
                ]
- NEXT:
    MAP
    - KEY0:
        STRING(foo)
    - VAL0:
        CALL
        - TARGET: "foo"
        - <arg0>:
            STRING(foo)
)");
        }
    }

    SECTION("one operand with static type info") {

        SECTION("variables") {
            auto const* code = R"(
            local foo = ['foo'];
            local bar = jst.env('bar');
            {
              foo: foo + bar,
            }
          )";
            auto output = nativeParser(code);
            CHECK(output == R"(LET
- NAME: foo
- VALUE:
    [
      STRING(foo)
    ]
- NEXT:
    LET
    - NAME: bar
    - VALUE:
        CALL
        - TARGET:
            UNRESOLVED_LOOKUP
            - INDEX:
                STRING(env)
            - CONTAINER:
                READ
                - TARGET: "jst"
        - <arg0>:
            STRING(bar)
    - NEXT:
        MAP
        - KEY0:
            STRING(foo)
        - VAL0:
            BINARY_OPERATION(unknown)
            - LHS:
                READ
                - TARGET: "foo"
            - RHS:
                READ
                - TARGET: "bar"
)");
        }

        SECTION("functions") {
            auto const* code = R"(
            local foo() = ['foo'];
            local bar() = jst.env('bar');
            {
              foo: foo() + bar(),
            }
          )";
            auto output = nativeParser(code);
            CHECK(output == R"(LET
- NAME: foo
- VALUE:
    FUNC
    - BODY:
        [
          STRING(foo)
        ]
- NEXT:
    LET
    - NAME: bar
    - VALUE:
        FUNC
        - BODY:
            CALL
            - TARGET:
                UNRESOLVED_LOOKUP
                - INDEX:
                    STRING(env)
                - CONTAINER:
                    READ
                    - TARGET: "jst"
            - <arg0>:
                STRING(bar)
    - NEXT:
        MAP
        - KEY0:
            STRING(foo)
        - VAL0:
            BINARY_OPERATION(unknown)
            - LHS:
                CALL
                - TARGET: "foo"
            - RHS:
                CALL
                - TARGET: "bar"
)");
        }

        SECTION("local variable") {
            auto const* code = R"(
            {
              foo:
                local foo = ['foo'];
                local bar = jst.env('bar');
                foo + bar,
            }
          )";
            auto output = nativeParser(code);
            CHECK(output == R"(MAP
- KEY0:
    STRING(foo)
- VAL0:
    LET
    - NAME: foo
    - VALUE:
        [
          STRING(foo)
        ]
    - NEXT:
        LET
        - NAME: bar
        - VALUE:
            CALL
            - TARGET:
                UNRESOLVED_LOOKUP
                - INDEX:
                    STRING(env)
                - CONTAINER:
                    READ
                    - TARGET: "jst"
            - <arg0>:
                STRING(bar)
        - NEXT:
            BINARY_OPERATION(unknown)
            - LHS:
                READ
                - TARGET: "foo"
            - RHS:
                READ
                - TARGET: "bar"
)");
        }

        SECTION("local function") {
            auto const* code = R"(
            {
              foo:
                local foo() = ['foo'];
                local bar() = jst.env('bar');
                foo() + bar(),
            }
          )";
            auto output = nativeParser(code);
            CHECK(output == R"(MAP
- KEY0:
    STRING(foo)
- VAL0:
    LET
    - NAME: foo
    - VALUE:
        FUNC
        - BODY:
            [
              STRING(foo)
            ]
    - NEXT:
        LET
        - NAME: bar
        - VALUE:
            FUNC
            - BODY:
                CALL
                - TARGET:
                    UNRESOLVED_LOOKUP
                    - INDEX:
                        STRING(env)
                    - CONTAINER:
                        READ
                        - TARGET: "jst"
                - <arg0>:
                    STRING(bar)
        - NEXT:
            BINARY_OPERATION(unknown)
            - LHS:
                CALL
                - TARGET: "foo"
            - RHS:
                CALL
                - TARGET: "bar"
)");
        }

        SECTION("loop variable") {
            auto const* code = R"(
            local bar = 'bar';
            {
              foo: [bar + x for x in jst.env('baz')],
            }
          )";
            auto output = nativeParser(code);
            CHECK(output == R"(LET
- NAME: bar
- VALUE:
    STRING(bar)
- NEXT:
    MAP
    - KEY0:
        STRING(foo)
    - VAL0:
        FOREACH
        - VAR: x
        - RANGE:
            CALL
            - TARGET:
                UNRESOLVED_LOOKUP
                - INDEX:
                    STRING(env)
                - CONTAINER:
                    READ
                    - TARGET: "jst"
            - <arg0>:
                STRING(baz)
        - BODY:
            BINARY_OPERATION(unknown)
            - LHS:
                READ
                - TARGET: "bar"
            - RHS:
                READ
                - TARGET: "x"
)");
        }

        SECTION("sum numbers") {
            auto const* code = R"(
            local var = jst.env('NUMBER');
            {
              foo: var + 6
            })";
            auto output = nativeParser(code);
            CHECK(output == R"(LET
- NAME: var
- VALUE:
    CALL
    - TARGET:
        UNRESOLVED_LOOKUP
        - INDEX:
            STRING(env)
        - CONTAINER:
            READ
            - TARGET: "jst"
    - <arg0>:
        STRING(NUMBER)
- NEXT:
    MAP
    - KEY0:
        STRING(foo)
    - VAL0:
        BINARY_OPERATION(unknown)
        - LHS:
            READ
            - TARGET: "var"
        - RHS:
            NUMBER(6)
)");
        }

        SECTION("unary minus in function body") {
            auto const* code = R"(
            local foo(x) = -x;
            {
              foo: foo(2)
            })";
            auto output = nativeParser(code);
            CHECK(output == R"(LET
- NAME: foo
- VALUE:
    FUNC
    - x:
null
    - BODY:
        NEGATE
          READ
          - TARGET: "x"
- NEXT:
    MAP
    - KEY0:
        STRING(foo)
    - VAL0:
        CALL
        - TARGET: "foo"
        - <arg0>:
            NUMBER(2)
)");
        }
    }
}

TEST_CASE("list_at_parsing", "[index]") {

    SECTION("simple full inlining - string indexing") {
        auto const* code = R"(
            local list = ['foo', 'bar'];
            {
              foo: list["1"],
            }
          )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: list
- VALUE:
    [
      STRING(foo)
      STRING(bar)
    ]
- NEXT:
    MAP
    - KEY0:
        STRING(foo)
    - VAL0:
        UNRESOLVED_LOOKUP
        - INDEX:
            STRING(1)
        - CONTAINER:
            READ
            - TARGET: "list"
)");
    }

    SECTION("negative indexing - string indexing") {
        auto const* code = R"(
            local list = ['foo', 'bar'];
            {
              foo: list["-2"],
            }
          )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: list
- VALUE:
    [
      STRING(foo)
      STRING(bar)
    ]
- NEXT:
    MAP
    - KEY0:
        STRING(foo)
    - VAL0:
        UNRESOLVED_LOOKUP
        - INDEX:
            STRING(-2)
        - CONTAINER:
            READ
            - TARGET: "list"
)");
    }

    SECTION("simple full inlining - number indexing") {
        auto const* code = R"(
            local list = ['foo', 'bar'];
            {
              foo: list[1.0],
            }
          )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: list
- VALUE:
    [
      STRING(foo)
      STRING(bar)
    ]
- NEXT:
    MAP
    - KEY0:
        STRING(foo)
    - VAL0:
        UNRESOLVED_LOOKUP
        - INDEX:
            NUMBER(1)
        - CONTAINER:
            READ
            - TARGET: "list"
)");
    }

    SECTION("negative indexing - number indexing") {
        auto const* code = R"(
            local list = ['foo', 'bar'];
            {
              foo: list[-1.0],
            }
          )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: list
- VALUE:
    [
      STRING(foo)
      STRING(bar)
    ]
- NEXT:
    MAP
    - KEY0:
        STRING(foo)
    - VAL0:
        UNRESOLVED_LOOKUP
        - INDEX:
            NEGATE
              NUMBER(1)
        - CONTAINER:
            READ
            - TARGET: "list"
)");
    }

    SECTION("computed fields full inlining") {
        auto const* code = R"(
            local list = ['foo', 'bar'];
            {
              [list[0]]: list["1"],
            }
          )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: list
- VALUE:
    [
      STRING(foo)
      STRING(bar)
    ]
- NEXT:
    MAP
    - KEY0:
        UNRESOLVED_LOOKUP
        - INDEX:
            NUMBER(0)
        - CONTAINER:
            READ
            - TARGET: "list"
    - VAL0:
        UNRESOLVED_LOOKUP
        - INDEX:
            STRING(1)
        - CONTAINER:
            READ
            - TARGET: "list"
)");
    }

    SECTION("inlining static expressions") {
        auto const* code = R"(
            local foo = {index: '1'};
            local baz = {bar: foo};
            local list = ['foo', 'bar'];
            {
              foo: list[baz.bar.index],
            }
          )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: foo
- VALUE:
    MAP
    - KEY0:
        STRING(index)
    - VAL0:
        STRING(1)
- NEXT:
    LET
    - NAME: baz
    - VALUE:
        MAP
        - KEY0:
            STRING(bar)
        - VAL0:
            READ
            - TARGET: "foo"
    - NEXT:
        LET
        - NAME: list
        - VALUE:
            [
              STRING(foo)
              STRING(bar)
            ]
        - NEXT:
            MAP
            - KEY0:
                STRING(foo)
            - VAL0:
                UNRESOLVED_LOOKUP
                - INDEX:
                    UNRESOLVED_LOOKUP
                    - INDEX:
                        STRING(index)
                    - CONTAINER:
                        UNRESOLVED_LOOKUP
                        - INDEX:
                            STRING(bar)
                        - CONTAINER:
                            READ
                            - TARGET: "baz"
                - CONTAINER:
                    READ
                    - TARGET: "list"
)");
    }

    SECTION("index var") {
        auto const* code = R"(
            local list = ['foo', 'bar'];
            {
              foo: list[jst.env("INDEX")],
            }
          )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: list
- VALUE:
    [
      STRING(foo)
      STRING(bar)
    ]
- NEXT:
    MAP
    - KEY0:
        STRING(foo)
    - VAL0:
        UNRESOLVED_LOOKUP
        - INDEX:
            CALL
            - TARGET:
                UNRESOLVED_LOOKUP
                - INDEX:
                    STRING(env)
                - CONTAINER:
                    READ
                    - TARGET: "jst"
            - <arg0>:
                STRING(INDEX)
        - CONTAINER:
            READ
            - TARGET: "list"
)");
    }

    SECTION("field var full inlining") {
        auto const* code = R"(
            local list = [jst.env("INDEX"), 'bar'];
            {
              foo: list["0"],
            }
          )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: list
- VALUE:
    [
      CALL
      - TARGET:
          UNRESOLVED_LOOKUP
          - INDEX:
              STRING(env)
          - CONTAINER:
              READ
              - TARGET: "jst"
      - <arg0>:
          STRING(INDEX)
      STRING(bar)
    ]
- NEXT:
    MAP
    - KEY0:
        STRING(foo)
    - VAL0:
        UNRESOLVED_LOOKUP
        - INDEX:
            STRING(0)
        - CONTAINER:
            READ
            - TARGET: "list"
)");
    }
}

TEST_CASE("ast_parser", "[read-call-separation]") {
    SECTION("read variable and call function") {
        auto const* code = R"(
        local foo = 'foo';
        local bar() = 'bar';
        foo + bar()
      )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: foo
- VALUE:
    STRING(foo)
- NEXT:
    LET
    - NAME: bar
    - VALUE:
        FUNC
        - BODY:
            STRING(bar)
    - NEXT:
        BINARY_OPERATION(unknown)
        - LHS:
            READ
            - TARGET: "foo"
        - RHS:
            CALL
            - TARGET: "bar"
)");
    }
}

TEST_CASE("ast_parser", "[function objects]") {
    SECTION("call scope object without parameters") {
        auto const* code = R"(
        local foo = 'foo';
        local bar = function() 'bar';
        {
          foo: bar(),
        }
      )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: foo
- VALUE:
    STRING(foo)
- NEXT:
    LET
    - NAME: bar
    - VALUE:
        FUNC
        - BODY:
            STRING(bar)
    - NEXT:
        MAP
        - KEY0:
            STRING(foo)
        - VAL0:
            CALL
            - TARGET: "bar"
)");
    }

    SECTION("call scope object with parameters") {
        auto const* code = R"(
        local foo = 'foo';
        local bar = function(x) if x then 'bar' else 'baz';
        {
          foo: bar(foo),
        }
      )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: foo
- VALUE:
    STRING(foo)
- NEXT:
    LET
    - NAME: bar
    - VALUE:
        FUNC
        - x:
null
        - BODY:
            IF
            - COND:
                READ
                - TARGET: "x"
            - THEN:
                STRING(bar)
            - ELSE:
                STRING(baz)
    - NEXT:
        MAP
        - KEY0:
            STRING(foo)
        - VAL0:
            CALL
            - TARGET: "bar"
            - <arg0>:
                READ
                - TARGET: "foo"
)");
    }

    SECTION("call field object without parameters") {
        auto const* code = R"(
        local foo = 'foo';
        local bar = { bar: function() 'bar' };
        {
          foo: bar.bar(),
        }
      )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: foo
- VALUE:
    STRING(foo)
- NEXT:
    LET
    - NAME: bar
    - VALUE:
        MAP
        - KEY0:
            STRING(bar)
        - VAL0:
            FUNC
            - BODY:
                STRING(bar)
    - NEXT:
        MAP
        - KEY0:
            STRING(foo)
        - VAL0:
            CALL
            - TARGET:
                UNRESOLVED_LOOKUP
                - INDEX:
                    STRING(bar)
                - CONTAINER:
                    READ
                    - TARGET: "bar"
)");
    }

    SECTION("call field object with parameters") {
        auto const* code = R"(
        local foo = 'foo';
        local bar = { bar: function(x) if x then 'bar' else 'baz' };
        {
          foo: bar.bar(foo),
        }
      )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: foo
- VALUE:
    STRING(foo)
- NEXT:
    LET
    - NAME: bar
    - VALUE:
        MAP
        - KEY0:
            STRING(bar)
        - VAL0:
            FUNC
            - x:
null
            - BODY:
                IF
                - COND:
                    READ
                    - TARGET: "x"
                - THEN:
                    STRING(bar)
                - ELSE:
                    STRING(baz)
    - NEXT:
        MAP
        - KEY0:
            STRING(foo)
        - VAL0:
            CALL
            - TARGET:
                UNRESOLVED_LOOKUP
                - INDEX:
                    STRING(bar)
                - CONTAINER:
                    READ
                    - TARGET: "bar"
            - <arg0>:
                READ
                - TARGET: "foo"
)");
    }

    SECTION("pass scope function as parameter for calling") {
        auto const* code = R"(
        local foo = 'foo';
        local bar(x) = if x then 'bar' else 'baz';
        local launcher(func) = func(foo);
        {
          foo: launcher(bar),
        }
      )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: foo
- VALUE:
    STRING(foo)
- NEXT:
    LET
    - NAME: bar
    - VALUE:
        FUNC
        - x:
null
        - BODY:
            IF
            - COND:
                READ
                - TARGET: "x"
            - THEN:
                STRING(bar)
            - ELSE:
                STRING(baz)
    - NEXT:
        LET
        - NAME: launcher
        - VALUE:
            FUNC
            - func:
null
            - BODY:
                CALL
                - TARGET: "func"
                - <arg0>:
                    READ
                    - TARGET: "foo"
        - NEXT:
            MAP
            - KEY0:
                STRING(foo)
            - VAL0:
                CALL
                - TARGET: "launcher"
                - <arg0>:
                    READ
                    - TARGET: "bar"
)");
    }

    SECTION("pass inline object as parameter for calling") {
        auto const* code = R"(
        local foo = 'foo';
        local launcher(func) = func(foo);
        {
          foo: launcher(function(x) if x then 'bar' else 'baz'),
        }
      )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: foo
- VALUE:
    STRING(foo)
- NEXT:
    LET
    - NAME: launcher
    - VALUE:
        FUNC
        - func:
null
        - BODY:
            CALL
            - TARGET: "func"
            - <arg0>:
                READ
                - TARGET: "foo"
    - NEXT:
        MAP
        - KEY0:
            STRING(foo)
        - VAL0:
            CALL
            - TARGET: "launcher"
            - <arg0>:
                FUNC
                - x:
null
                - BODY:
                    IF
                    - COND:
                        READ
                        - TARGET: "x"
                    - THEN:
                        STRING(bar)
                    - ELSE:
                        STRING(baz)
)");
    }

    SECTION("pass scope object as parameter for calling") {
        auto const* code = R"(
        local foo = 'foo';
        local bar = function(x) if x then 'bar' else 'baz';
        local launcher(func) = func(foo);
        {
          foo: launcher(bar),
        }
      )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: foo
- VALUE:
    STRING(foo)
- NEXT:
    LET
    - NAME: bar
    - VALUE:
        FUNC
        - x:
null
        - BODY:
            IF
            - COND:
                READ
                - TARGET: "x"
            - THEN:
                STRING(bar)
            - ELSE:
                STRING(baz)
    - NEXT:
        LET
        - NAME: launcher
        - VALUE:
            FUNC
            - func:
null
            - BODY:
                CALL
                - TARGET: "func"
                - <arg0>:
                    READ
                    - TARGET: "foo"
        - NEXT:
            MAP
            - KEY0:
                STRING(foo)
            - VAL0:
                CALL
                - TARGET: "launcher"
                - <arg0>:
                    READ
                    - TARGET: "bar"
)");
    }

    SECTION("pass field function as parameter for calling") {
        auto const* code = R"(
        local foo = 'foo';
        local bar = { bar(x): if x then 'bar' else 'baz' };
        local launcher(func) = func(foo);
        {
          foo: launcher(bar.bar),
        }
      )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: foo
- VALUE:
    STRING(foo)
- NEXT:
    LET
    - NAME: bar
    - VALUE:
        MAP
        - KEY0:
            STRING(bar)
        - VAL0:
            FUNC
            - x:
null
            - BODY:
                IF
                - COND:
                    READ
                    - TARGET: "x"
                - THEN:
                    STRING(bar)
                - ELSE:
                    STRING(baz)
    - NEXT:
        LET
        - NAME: launcher
        - VALUE:
            FUNC
            - func:
null
            - BODY:
                CALL
                - TARGET: "func"
                - <arg0>:
                    READ
                    - TARGET: "foo"
        - NEXT:
            MAP
            - KEY0:
                STRING(foo)
            - VAL0:
                CALL
                - TARGET: "launcher"
                - <arg0>:
                    UNRESOLVED_LOOKUP
                    - INDEX:
                        STRING(bar)
                    - CONTAINER:
                        READ
                        - TARGET: "bar"
)");
    }

    SECTION("pass field object as parameter for calling") {
        auto const* code = R"(
        local foo = 'foo';
        local bar = { bar: function(x) if x then 'bar' else 'baz' };
        local launcher(func) = func(foo);
        {
          foo: launcher(bar.bar),
        }
      )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: foo
- VALUE:
    STRING(foo)
- NEXT:
    LET
    - NAME: bar
    - VALUE:
        MAP
        - KEY0:
            STRING(bar)
        - VAL0:
            FUNC
            - x:
null
            - BODY:
                IF
                - COND:
                    READ
                    - TARGET: "x"
                - THEN:
                    STRING(bar)
                - ELSE:
                    STRING(baz)
    - NEXT:
        LET
        - NAME: launcher
        - VALUE:
            FUNC
            - func:
null
            - BODY:
                CALL
                - TARGET: "func"
                - <arg0>:
                    READ
                    - TARGET: "foo"
        - NEXT:
            MAP
            - KEY0:
                STRING(foo)
            - VAL0:
                CALL
                - TARGET: "launcher"
                - <arg0>:
                    UNRESOLVED_LOOKUP
                    - INDEX:
                        STRING(bar)
                    - CONTAINER:
                        READ
                        - TARGET: "bar"
)");
    }

    SECTION("call scope function from function return") {
        auto const* code = R"(
        local foo = 'foo';
        local bar(x) = if x then 'bar' else 'baz';
        local get_func(foo) = if foo then bar else foo;
        local launcher(func) = func(foo);
        {
          foo: launcher(get_func(foo)),
        }
      )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: foo
- VALUE:
    STRING(foo)
- NEXT:
    LET
    - NAME: bar
    - VALUE:
        FUNC
        - x:
null
        - BODY:
            IF
            - COND:
                READ
                - TARGET: "x"
            - THEN:
                STRING(bar)
            - ELSE:
                STRING(baz)
    - NEXT:
        LET
        - NAME: get_func
        - VALUE:
            FUNC
            - foo:
null
            - BODY:
                IF
                - COND:
                    READ
                    - TARGET: "foo"
                - THEN:
                    READ
                    - TARGET: "bar"
                - ELSE:
                    READ
                    - TARGET: "foo"
        - NEXT:
            LET
            - NAME: launcher
            - VALUE:
                FUNC
                - func:
null
                - BODY:
                    CALL
                    - TARGET: "func"
                    - <arg0>:
                        READ
                        - TARGET: "foo"
            - NEXT:
                MAP
                - KEY0:
                    STRING(foo)
                - VAL0:
                    CALL
                    - TARGET: "launcher"
                    - <arg0>:
                        CALL
                        - TARGET: "get_func"
                        - <arg0>:
                            READ
                            - TARGET: "foo"
)");
    }

    SECTION("call scope object from function return") {
        auto const* code = R"(
        local foo = 'foo';
        local bar = function(x) if x then 'bar' else 'baz';
        local get_func(foo) = if foo then bar else foo;
        local launcher(func) = func(foo);
        {
          foo: launcher(get_func(foo)),
        }
      )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: foo
- VALUE:
    STRING(foo)
- NEXT:
    LET
    - NAME: bar
    - VALUE:
        FUNC
        - x:
null
        - BODY:
            IF
            - COND:
                READ
                - TARGET: "x"
            - THEN:
                STRING(bar)
            - ELSE:
                STRING(baz)
    - NEXT:
        LET
        - NAME: get_func
        - VALUE:
            FUNC
            - foo:
null
            - BODY:
                IF
                - COND:
                    READ
                    - TARGET: "foo"
                - THEN:
                    READ
                    - TARGET: "bar"
                - ELSE:
                    READ
                    - TARGET: "foo"
        - NEXT:
            LET
            - NAME: launcher
            - VALUE:
                FUNC
                - func:
null
                - BODY:
                    CALL
                    - TARGET: "func"
                    - <arg0>:
                        READ
                        - TARGET: "foo"
            - NEXT:
                MAP
                - KEY0:
                    STRING(foo)
                - VAL0:
                    CALL
                    - TARGET: "launcher"
                    - <arg0>:
                        CALL
                        - TARGET: "get_func"
                        - <arg0>:
                            READ
                            - TARGET: "foo"
)");
    }

    SECTION("call field function from function return") {
        auto const* code = R"(
        local foo = 'foo';
        local bar = { bar(x): if x then 'bar' else 'baz' };
        local get_func(foo) = if foo then bar.bar else foo;
        local launcher(func) = func(foo);
        {
          foo: launcher(get_func(foo)),
        }
      )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: foo
- VALUE:
    STRING(foo)
- NEXT:
    LET
    - NAME: bar
    - VALUE:
        MAP
        - KEY0:
            STRING(bar)
        - VAL0:
            FUNC
            - x:
null
            - BODY:
                IF
                - COND:
                    READ
                    - TARGET: "x"
                - THEN:
                    STRING(bar)
                - ELSE:
                    STRING(baz)
    - NEXT:
        LET
        - NAME: get_func
        - VALUE:
            FUNC
            - foo:
null
            - BODY:
                IF
                - COND:
                    READ
                    - TARGET: "foo"
                - THEN:
                    UNRESOLVED_LOOKUP
                    - INDEX:
                        STRING(bar)
                    - CONTAINER:
                        READ
                        - TARGET: "bar"
                - ELSE:
                    READ
                    - TARGET: "foo"
        - NEXT:
            LET
            - NAME: launcher
            - VALUE:
                FUNC
                - func:
null
                - BODY:
                    CALL
                    - TARGET: "func"
                    - <arg0>:
                        READ
                        - TARGET: "foo"
            - NEXT:
                MAP
                - KEY0:
                    STRING(foo)
                - VAL0:
                    CALL
                    - TARGET: "launcher"
                    - <arg0>:
                        CALL
                        - TARGET: "get_func"
                        - <arg0>:
                            READ
                            - TARGET: "foo"
)");
    }

    SECTION("call field object from function return") {
        auto const* code = R"(
        local foo = 'foo';
        local bar = { bar: function(x) if x then 'bar' else 'baz' };
        local get_func(foo) = if foo then bar.bar else foo;
        local launcher(func) = func(foo);
        {
          foo: launcher(get_func(foo)),
        }
      )";
        auto output = nativeParser(code);
        CHECK(output == R"(LET
- NAME: foo
- VALUE:
    STRING(foo)
- NEXT:
    LET
    - NAME: bar
    - VALUE:
        MAP
        - KEY0:
            STRING(bar)
        - VAL0:
            FUNC
            - x:
null
            - BODY:
                IF
                - COND:
                    READ
                    - TARGET: "x"
                - THEN:
                    STRING(bar)
                - ELSE:
                    STRING(baz)
    - NEXT:
        LET
        - NAME: get_func
        - VALUE:
            FUNC
            - foo:
null
            - BODY:
                IF
                - COND:
                    READ
                    - TARGET: "foo"
                - THEN:
                    UNRESOLVED_LOOKUP
                    - INDEX:
                        STRING(bar)
                    - CONTAINER:
                        READ
                        - TARGET: "bar"
                - ELSE:
                    READ
                    - TARGET: "foo"
        - NEXT:
            LET
            - NAME: launcher
            - VALUE:
                FUNC
                - func:
null
                - BODY:
                    CALL
                    - TARGET: "func"
                    - <arg0>:
                        READ
                        - TARGET: "foo"
            - NEXT:
                MAP
                - KEY0:
                    STRING(foo)
                - VAL0:
                    CALL
                    - TARGET: "launcher"
                    - <arg0>:
                        CALL
                        - TARGET: "get_func"
                        - <arg0>:
                            READ
                            - TARGET: "foo"
)");
    }
}
TEST_CASE("ast_parser", "[get functions]") {
    SECTION("get() with mandatory arguments") {
        {
            auto const* code = "jst.get('foo', {})";
            auto output = nativeParser(code);
            CHECK(output == R"(CALL
- TARGET:
    UNRESOLVED_LOOKUP
    - INDEX:
        STRING(get)
    - CONTAINER:
        READ
        - TARGET: "jst"
- <arg0>:
    STRING(foo)
- <arg1>:
    MAP
)");
        }

        {
            auto const* code = "jst.get('foo', map={})";
            auto output = nativeParser(code);
            CHECK(output == R"(CALL
- TARGET:
    UNRESOLVED_LOOKUP
    - INDEX:
        STRING(get)
    - CONTAINER:
        READ
        - TARGET: "jst"
- <arg0>:
    STRING(foo)
- map:
    MAP
)");
        }

        {
            auto const* code = "jst.get(map={}, key='foo')";
            auto output = nativeParser(code);
            CHECK(output == R"(CALL
- TARGET:
    UNRESOLVED_LOOKUP
    - INDEX:
        STRING(get)
    - CONTAINER:
        READ
        - TARGET: "jst"
- map:
    MAP
- key:
    STRING(foo)
)");
        }

        {
            auto const* code = "jst.get(key='foo', map={})";
            auto output = nativeParser(code);
            CHECK(output == R"(CALL
- TARGET:
    UNRESOLVED_LOOKUP
    - INDEX:
        STRING(get)
    - CONTAINER:
        READ
        - TARGET: "jst"
- key:
    STRING(foo)
- map:
    MAP
)");
        }
        {
            auto const* code = R"(
        jst.nub_right([
          if jst.env('foo') then "foo" else "bar",
          if jst.env('bar') then "bar" else "foo",
        ])
      )";
            auto output = nativeParser(code);
            CHECK(output == R"(CALL
- TARGET:
    UNRESOLVED_LOOKUP
    - INDEX:
        STRING(nub_right)
    - CONTAINER:
        READ
        - TARGET: "jst"
- <arg0>:
    [
      IF
      - COND:
          CALL
          - TARGET:
              UNRESOLVED_LOOKUP
              - INDEX:
                  STRING(env)
              - CONTAINER:
                  READ
                  - TARGET: "jst"
          - <arg0>:
              STRING(foo)
      - THEN:
          STRING(foo)
      - ELSE:
          STRING(bar)
      IF
      - COND:
          CALL
          - TARGET:
              UNRESOLVED_LOOKUP
              - INDEX:
                  STRING(env)
              - CONTAINER:
                  READ
                  - TARGET: "jst"
          - <arg0>:
              STRING(bar)
      - THEN:
          STRING(bar)
      - ELSE:
          STRING(foo)
    ]
)");
        }
        {
            auto const* code = "jst.at(list=[jst.env('foo'), 'bar'],index=-1)";
            auto output = nativeParser(code);
            CHECK(output == R"(CALL
- TARGET:
    UNRESOLVED_LOOKUP
    - INDEX:
        STRING(at)
    - CONTAINER:
        READ
        - TARGET: "jst"
- list:
    [
      CALL
      - TARGET:
          UNRESOLVED_LOOKUP
          - INDEX:
              STRING(env)
          - CONTAINER:
              READ
              - TARGET: "jst"
      - <arg0>:
          STRING(foo)
      STRING(bar)
    ]
- index:
    NEGATE
      NUMBER(1)
)");
        }
        {
            auto const* code = R"(
        jst.to_subdir(
          msg='some error',
          flat=true,
          subdir='baz',
          map={'a/foo': 'foo', 'b/bar': 'bar'},
        )
      )";
            auto output = nativeParser(code);
            CHECK(output == R"(CALL
- TARGET:
    UNRESOLVED_LOOKUP
    - INDEX:
        STRING(to_subdir)
    - CONTAINER:
        READ
        - TARGET: "jst"
- msg:
    STRING(some error)
- flat:
    BOOL(true)
- subdir:
    STRING(baz)
- map:
    MAP
    - KEY0:
        STRING(a/foo)
    - VAL0:
        STRING(foo)
    - KEY1:
        STRING(b/bar)
    - VAL1:
        STRING(bar)
)");
        }
        {
            auto const* code = "{ foo: 5 - 3 }";
            auto output = nativeParser(code);
            CHECK(output == R"(MAP
- KEY0:
    STRING(foo)
- VAL0:
    BINARY_OPERATION(+)
    - LHS:
        NUMBER(5)
    - RHS:
        NEGATE
          NUMBER(3)
)");
        }
        {
            auto const* code = "jst.get('foo', {foo: null}, default='bar')";
            auto output = nativeParser(code);
            CHECK(output == R"(CALL
- TARGET:
    UNRESOLVED_LOOKUP
    - INDEX:
        STRING(get)
    - CONTAINER:
        READ
        - TARGET: "jst"
- <arg0>:
    STRING(foo)
- <arg1>:
    MAP
    - KEY0:
        STRING(foo)
    - VAL0:
        NULL
- default:
    STRING(bar)
)");
        }
    }
}

TEST_CASE("ast_parser", "[imports]") {
    SECTION("simple import") {
        auto files = file_map_t{};
        files["foo.jst"] = R"(
          local foo = import 'bar.jst';
          foo
        )";
        files["bar.jst"] = R"("bar")";
        auto output = nativeParser(files["foo.jst"], "foo.jst", files);
        CHECK(output == R"(LET
- NAME: foo
- VALUE:
    FOREIGN
      STRING(bar)
- NEXT:
    READ
    - TARGET: "foo"
)");
    }

    SECTION("cycle via self import") {
        auto files = file_map_t{};
        files["foo.jst"] = R"(
          local foo = import 'foo.jst';
          foo
        )";
        CHECK_THROWS(nativeParser(files["foo.jst"], "foo.jst", files));
    }

    SECTION("cycle via cross import") {
        auto files = file_map_t{};
        files["foo.jst"] = R"(
          local foo = import 'bar.jst';
          foo
        )";
        files["bar.jst"] = R"(
          local bar = import 'foo.jst';
          bar
        )";
        CHECK_THROWS(nativeParser(files["foo.jst"], "foo.jst", files));
    }
}

}  // namespace
