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

#include "justlang/ast/inline.hpp"

#include <optional>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include "justlang/ast/builtin_functions.hpp"
#include "justlang/ast/parser.hpp"
#include "justlang/ast/to_json_visitor.hpp"
#include "justlang/file_data.hpp"

namespace {

[[nodiscard]] auto ParseAndInline(std::string const& code) -> nlohmann::json {
    auto moc_reader = [](auto const& /*import_from*/,
                         auto const& /*file_path*/,
                         auto const* /*repo*/) { return std::nullopt; };
    auto file_data = justlang::FileData{
        .location = justlang::FileLocation{.repo = "", .path = "<stdin>"},
        .content = code};
    auto parser = justlang::Parser::Create(std::move(moc_reader));
    auto just_ast = parser->ParseData(file_data);
    just_ast = ProvideBuiltinsToAST(just_ast);
    auto inl_ast = justlang::InlineAST(just_ast);
    return justlang::ASTToJsonVisitor{}.ToJson(*inl_ast);
}

}  // namespace

TEST_CASE("ast_inline", "[empty]") {
    auto expect = nlohmann::json::parse(R"({"type":"empty_map"})");
    auto output = ParseAndInline("{}");
    CHECK(output == expect);
}

TEST_CASE("ast_inline", "[variables]") {
    auto expect = nlohmann::json::parse(R"(
      { "type": "singleton_map"
      , "key": "foo"
      , "value": "foo"
      })");

    SECTION("simple") {
        auto const* code = R"(
          local foo = 'foo';
          {
            foo: foo,
          })";
        auto output = ParseAndInline(code);
        CHECK(output == expect);
    }

    SECTION("variables inlining other variables") {
        auto const* code = R"(
          local foo = 'foo';
          local bar = foo;
          {
            foo: bar,
          })";
        auto output = ParseAndInline(code);
        CHECK(output == expect);
    }

    SECTION("variables in computed field") {
        auto const* code = R"(
          local foo = 'foo';
          {
            [foo]: foo,
          })";
        auto output = ParseAndInline(code);
        CHECK(output == expect);
    }

    SECTION("computed variable field") {
        auto const expected_output = nlohmann::json::parse(R"(
          { "type": "singleton_map"
          , "key": "foo"
          , "value":
            { "type": "lookup"
            , "key": "foo"
            , "map":
              { "type": "singleton_map"
              , "key":
                { "name": "NAME"
                , "type": "var"
                }
              , "value": "foo"
              }
            }
          })");
        auto const* code = R"(
          local map = {[jst.env('NAME')]: 'foo'};
          {
            foo: map.foo,
          })";

        auto output = ParseAndInline(code);
        CHECK(output == expected_output);
    }

    SECTION("variable shadows previous variable") {
        auto const* code = R"(
          local foo = 'bar';
          local foo = 'foo';
          {
            foo: foo,
          })";
        auto output = ParseAndInline(code);
        CHECK(output == expect);
    }
}

TEST_CASE("ast_inline", "[functions]") {
    auto expect = nlohmann::json::parse(R"(
        { "type": "singleton_map"
        , "key": "foo"
        , "value": "foo"
        })");

    SECTION("simple") {
        auto const* code = R"(
              local foo(x) = x;
              {
                foo: foo('foo'),
              }
            )";
        auto output = ParseAndInline(code);
        CHECK(output == expect);
    }

    SECTION("functions inlining other functions") {
        auto const* code = R"(
              local foo(x) = x;
              local bar(x) = foo(x);
              {
                foo: bar('foo'),
              }
            )";
        auto output = ParseAndInline(code);
        CHECK(output == expect);
    }

    SECTION("functions in computed field") {
        auto const* code = R"(
              local foo(x) = x;
              {
                [foo('foo')]: foo('foo'),
              }
            )";
        auto output = ParseAndInline(code);
        CHECK(output == expect);
    }

    auto expect2 = nlohmann::json::parse(R"(
        { "type":"map_union"
        , "$1":
          [ { "type": "singleton_map"
            , "key": "foo"
            , "value": "test"
            }
          , { "type": "singleton_map"
            , "key": "bar"
            , "value": "foo"
            }
          ]
        })");

    SECTION("functions with default argument") {
        auto const* code = R"(
              local foo(x='test') = x;
              {
                foo: foo(),
                bar: foo('foo'),
              }
            )";
        auto output = ParseAndInline(code);
        CHECK(output == expect2);
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
        auto output = ParseAndInline(code);
        CHECK(output == expect2);
    }

    auto expect4 = nlohmann::json::parse(R"(
        { "type": "singleton_map"
        , "key": "foo"
        , "value": ["foo", "bar"]
        })");

    SECTION("functions shadow previous functions") {
        auto const* code = R"(
              local foo(x) = [x, 'foo'];
              local foo(x) = [x, 'bar'];
              {
                foo: foo('foo'),
              }
            )";
        auto output = ParseAndInline(code);
        CHECK(output == expect4);
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
        auto output = ParseAndInline(code);
        CHECK(output == expect4);
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
        auto output = ParseAndInline(code);
        CHECK(output == expect4);
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
        auto output = ParseAndInline(code);
        CHECK(output == expect4);
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
        auto output = ParseAndInline(code);
        CHECK(output == expect4);
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
        auto output = ParseAndInline(code);
        CHECK(output == expect4);
    }

    SECTION("function param defaults can access preceding params") {
        auto expect = nlohmann::json::parse(R"(
            { "type": "singleton_map"
            , "key": "foo"
            , "value": ["bar", "bar"]
            })");

        auto const* code = R"(
              local a = 'foo';
              local foo(a, b=a) = [a, b];
              {
                foo: foo('bar'),
              }
            )";
        auto output = ParseAndInline(code);
        CHECK(output == expect);
    }

    SECTION("functions with nested variables") {
        auto expect = nlohmann::json::parse(R"(
            { "type": "singleton_map"
            , "key": "foo"
            , "value": {"type": "var", "name": "foo"}
            })");
        auto const* code = R"(
              local foo(x) =
                local bar = jst.env(x); // x satisfies requirement to be string type
                bar;
              {
                foo: foo('foo')
              }
            )";
        auto output = ParseAndInline(code);
        CHECK(output == expect);
    }
}

TEST_CASE("ast_inline", "[retain var nodes]") {
    SECTION("var nodes from env") {
        auto expect = nlohmann::json::parse(R"(
            { "type": "singleton_map"
            , "key": "baz"
            , "value":
              { "type": "var"
              , "name": "baz"
              }
            })");
        auto const* code = R"(
            local foo = jst.env('baz'); // foo is full inlined, built-in resolved
            local bar(x='bar') = foo;   // bar() is partially inlined, 'x' unkown
            { baz: bar() }              // on call, bar() is fully inlined
            )";
        auto output = ParseAndInline(code);
        CHECK(output == expect);
    }

    SECTION("var nodes from array comprehension's index var") {
        auto expect = nlohmann::json::parse(R"(
            { "type": "singleton_map"
            , "key": "baz"
            , "value":
              { "type": "foreach"
              , "var": "foo"
              , "range": null
              , "body": {"type": "var", "name": "foo"}
              }
            })");
        auto const* code = R"(
            local foo =               // foo is full inlined, built-in resolved
              [foo for foo in null];
            local bar(x='bar') = foo; // bar() is partially inlined, 'x' unkown
            { baz: bar() }            // on call, bar() is fully inlined
            )";
        auto output = ParseAndInline(code);
        CHECK(output == expect);
    }
}

TEST_CASE("ast_inline", "[index]") {
    auto expect = nlohmann::json::parse(R"(
        { "type": "singleton_map"
        , "key": "foo"
        , "value": "foo"
        })");

    SECTION("simple") {
        auto const* code = R"(
          local foo = {foo: 'foo'};
          {
            foo: foo.foo,
          })";
        auto output = ParseAndInline(code);
        CHECK(output == expect);
    }

    SECTION("in computed field") {
        auto const* code = R"(
          local foo = {foo: 'foo'};
          {
            [foo.foo]: foo.foo,
          })";
        auto output = ParseAndInline(code);
        CHECK(output == expect);
    }

    SECTION("objects in objects") {
        auto const* code = R"(
          local foo = {foo: 'foo'};
          local baz = {bar: foo};
          {
            foo: baz.bar.foo,
          })";
        auto output = ParseAndInline(code);
        CHECK(output == expect);
    }

    SECTION("operator") {
        auto const* code = R"(
          local foo = {bar: 'foo'};
          local bar = 'bar';
          {
            foo: foo[bar],
          })";
        auto output = ParseAndInline(code);
        CHECK(output == expect);
    }

    SECTION("functions without params in objects") {
        auto const* code = R"(
          local foo = {bar(): 'foo'};
          {
            foo: foo.bar(),
          })";
        auto output = ParseAndInline(code);
        CHECK(output == expect);
    }

    SECTION("functions in objects") {
        auto const* code = R"(
          local foo = {bar(x): x};
          {
            foo: foo.bar('foo'),
          })";
        auto output = ParseAndInline(code);
        CHECK(output == expect);
    }

    SECTION("functions in nested objects") {
        auto const* code = R"(
          local foo = {bar: {baz(x): x}};
          {
            foo: foo.bar.baz('foo'),
          })";
        auto output = ParseAndInline(code);
        CHECK(output == expect);
    }

    SECTION("nested functions in objects") {
        auto const* code = R"(
          local foo = {bar(x='foo'): {baz(x=x): x}};
          {
            foo: foo.bar().baz(),
          })";
        auto output = ParseAndInline(code);
        CHECK(output == expect);
    }

    SECTION("nested functions in objects (first param)") {
        auto const* code = R"(
          local foo = {bar(x='bar'): {baz(x=x): x}};
          {
            foo: foo.bar('foo').baz(),
          })";
        auto output = ParseAndInline(code);
        CHECK(output == expect);
    }

    SECTION("nested functions in objects (second param)") {
        auto const* code = R"(
          local foo = {bar(x='bar'): {baz(x=x): x}};
          {
            foo: foo.bar().baz('foo'),
          })";
        auto output = ParseAndInline(code);
        CHECK(output == expect);
    }
}

TEST_CASE("ast_inline", "[built-in function evaluation]") {
    SECTION("keys()") {
        SECTION("static data") {
            auto expect = nlohmann::json::parse(R"(
                { "type": "singleton_map"
                , "key": "foo"
                , "value": ["key0", "key1"]
                })");
            auto const* code = R"(
              local test(x) = jst.keys(x);
              local map = {key0: 'val0', key1: 'val1'};
              {
                foo: test(map),
              }
            )";
            auto output = ParseAndInline(code);
            CHECK(output == expect);
        }

        SECTION("runtime data") {
            auto expect = nlohmann::json::parse(R"(
                { "type": "singleton_map"
                , "key": "foo"
                , "value":
                  { "type": "keys"
                  , "$1": {"type": "var", "name": "map"}
                  }
                })");
            auto const* code = R"(
              local test(x) = jst.keys(x);
              {
                foo: test(jst.env('map')),
              }
            )";
            auto output = ParseAndInline(code);
            CHECK(output == expect);
        }
    }
}

TEST_CASE("ast_inline", "[array comprehension]") {
    SECTION("static range") {
        auto expect = nlohmann::json::parse(R"(
        { "type": "singleton_map"
        , "key": "foo"
        , "value": [["foo", "bar"], ["foo", "baz"]]
        })");

        SECTION("from literal array") {
            auto const* code = R"(
              local foo = 'foo';
              local bar(x) = x;
              {
                foo: [[foo, bar(x)] for x in ['bar', 'baz']],
              }
            )";
            auto output = ParseAndInline(code);
            CHECK(output == expect);
        }

        SECTION("from var") {
            auto const* code = R"(
              local foo = 'foo';
              local bar(x) = x;
              {
                foo:
                  local range = ['bar', 'baz'];
                  [[foo, bar(x)] for x in range],
              }
            )";
            auto output = ParseAndInline(code);
            CHECK(output == expect);
        }

        SECTION("from function parameter") {
            auto const* code = R"(
              local foo = 'foo';
              local bar(x) = [[foo, f] for f in x];
              local foo = 'fox';
              {
                foo: bar(['bar', 'baz']),
              }
            )";
            auto output = ParseAndInline(code);
            CHECK(output == expect);
        }
    }

    SECTION("runtime range") {
        auto expect = nlohmann::json::parse(R"(
        { "type": "singleton_map"
        , "key": "foo"
        , "value":
          { "type": "foreach"
          , "var": "x"
          , "range": {"type": "var", "name": "baz"}
          , "body": ["foo", {"type": "var", "name": "x"}]
          }
        })");

        SECTION("from env()") {
            auto const* code = R"(
              local foo = 'foo';
              local bar(x) = x;
              {
                foo: [[foo, bar(x)] for x in jst.env('baz')],
              }
            )";
            auto output = ParseAndInline(code);
            CHECK(output == expect);
        }

        SECTION("from env() as function parameter") {
            auto const* code = R"(
              local foo = 'foo';
              local bar(p) = [[foo, x] for x in p];
              local foo = 'fox';
              {
                foo: bar(jst.env('baz')),
              }
            )";
            auto output = ParseAndInline(code);
            CHECK(output == expect);
        }
    }

    SECTION("variable conflict") {
        SECTION("resolved via unrolling") {
            auto expect = nlohmann::json::parse(R"(
                { "type": "singleton_map"
                , "key": "foo"
                , "value":
                  [ ["bar", {"type": "var", "name": "x"}]
                  , ["baz", {"type": "var", "name": "x"}]
                  ]
                })");
            auto const* code = R"(
              {
                foo: [[x, jst.env('x')] for x in ['bar', 'baz']],
              }
            )";
            auto output = ParseAndInline(code);
            CHECK(output == expect);
        }

        SECTION("error: env variable is shadowed by loop variable") {
            auto const* code = R"(
              {
                foo: [jst.env('x') for x in jst.env('foo')],
              }
            )";
            CHECK_THROWS_AS(ParseAndInline(code), justlang::ASTInlineError);
        }
    }

    SECTION("loop over map") {
        SECTION("via keys() and index") {
            auto expect = nlohmann::json::parse(R"(
                { "type": "singleton_map"
                , "key": "foo"
                , "value":
                  [ ["key0", "val0"]
                  , ["key1", "val1"]
                  ]
                })");
            auto const* code = R"(
              local map = {key0: 'val0', key1: 'val1'};
              {
                foo: [[x, map[x]] for x in jst.keys(map)],
              }
            )";
            auto output = ParseAndInline(code);
            CHECK(output == expect);
        }
    }
}

TEST_CASE("ast_inline", "[object comprehension]") {
    SECTION("static range") {
        auto expect = nlohmann::json::parse(R"(
        { "type": "singleton_map"
        , "key": "foo"
        , "value":
          { "type": "map_union"
          , "$1":
            [ { "type": "singleton_map"
              , "key": "bar"
              , "value": "foo"
              }
            , { "type": "singleton_map"
              , "key": "baz"
              , "value": "foo"
              }
            ]
          }
        })");

        SECTION("from literal array") {
            auto const* code = R"(
              local foo = 'foo';
              local bar(x) = x;
              {
                foo: {[bar(x)]:foo for x in ['bar', 'baz']},
              }
            )";
            auto output = ParseAndInline(code);
            CHECK(output == expect);
        }

        SECTION("from var") {
            auto const* code = R"(
              local foo = 'foo';
              local bar(x) = x;
              {
                foo:
                  local range = ['bar', 'baz'];
                  {[bar(x)]:foo for x in range},
              }
            )";
            auto output = ParseAndInline(code);
            CHECK(output == expect);
        }

        SECTION("from function parameter") {
            auto const* code = R"(
              local foo = 'foo';
              local bar(x) = {[i]:foo for i in x};
              local foo = 'fox';
              {
                foo: bar(['bar', 'baz']),
              }
            )";
            auto output = ParseAndInline(code);
            CHECK(output == expect);
        }
    }

    SECTION("runtime range") {
        auto expect = nlohmann::json::parse(R"(
        { "type": "singleton_map"
        , "key": "foo"
        , "value":
          { "type": "map_union"
          , "$1":
            { "type": "foreach"
            , "var": "x"
            , "range": {"type": "var", "name": "baz"}
            , "body":
              { "type": "singleton_map"
              , "key": {"type": "var", "name": "x"}
              , "value": "foo"
              }
            }
          }
        })");

        SECTION("from env()") {
            auto const* code = R"(
              local foo = 'foo';
              local bar(x) = x;
              {
                foo: {[bar(x)]:foo for x in jst.env('baz')},
              }
            )";
            auto output = ParseAndInline(code);
            CHECK(output == expect);
        }

        SECTION("from env() as function parameter") {
            auto const* code = R"(
              local foo = 'foo';
              local bar(p) = {[x]:foo for x in p};
              local foo = 'fox';
              {
                foo: bar(jst.env('baz')),
              }
            )";
            auto output = ParseAndInline(code);
            CHECK(output == expect);
        }
    }

    SECTION("variable conflict") {
        SECTION("resolved via unrolling") {
            auto expect = nlohmann::json::parse(R"(
                { "type": "singleton_map"
                , "key": "foo"
                , "value":
                  { "type": "map_union"
                  , "$1":
                    [ { "type": "singleton_map"
                      , "key": "bar"
                      , "value": {"type": "var", "name": "x"}
                      }
                    , { "type": "singleton_map"
                      , "key": "baz"
                      , "value": {"type": "var", "name": "x"}
                      }
                    ]
                  }
                })");
            auto const* code = R"(
              {
                foo: {[x]:jst.env('x') for x in ['bar', 'baz']},
              }
            )";
            auto output = ParseAndInline(code);
            CHECK(output == expect);
        }

        SECTION("error: env variable is shadowed by loop variable") {
            auto const* code = R"(
              {
                foo: {[x]:jst.env('x') for x in jst.env('foo')},
              }
            )";
            CHECK_THROWS_AS(ParseAndInline(code), justlang::ASTInlineError);
        }
    }

    SECTION("loop over map") {
        SECTION("via keys() and index") {
            auto expect = nlohmann::json::parse(R"(
                { "type": "singleton_map"
                , "key": "foo"
                , "value":
                  { "type": "map_union"
                  , "$1":
                    [ { "type": "singleton_map"
                      , "key": "key0"
                      , "value": "val0"
                      }
                    , { "type": "singleton_map"
                      , "key": "key1"
                      , "value": "val1"
                      }
                    ]
                  }
                })");
            auto const* code = R"(
              local map = {key0: 'val0', key1: 'val1'};
              {
                foo: {[x]:map[x] for x in jst.keys(map)},
              }
            )";
            auto output = ParseAndInline(code);
            CHECK(output == expect);
        }
    }
}

TEST_CASE("ast_inline", "[error handling]") {
    SECTION("undeclared variables") {
        auto const* code = R"(
          local foo = 'foo';
          {
            foo: bar,
          })";
        CHECK_THROWS_AS(ParseAndInline(code), justlang::ASTInlineError);
    }

    SECTION("undeclared functions") {
        auto const* code = R"(
          local foo() = 'foo';
          {
            foo: bar(),
          })";
        CHECK_THROWS_AS(ParseAndInline(code), justlang::ASTInlineError);
    }
}

TEST_CASE("ast_inline", "[type deduction]") {
    SECTION("both operands with static type info") {
        auto expect = nlohmann::json::parse(R"(
            { "type": "singleton_map"
            , "key": "foo"
            , "value": "foobar"
            })");

        SECTION("variables") {
            auto const* code = R"(
              local foo = 'foo';
              local bar = 'bar';
              {
                foo: foo + bar,
              }
            )";
            auto output = ParseAndInline(code);
            CHECK(output == expect);
        }

        SECTION("functions") {
            auto const* code = R"(
              local foo() = 'foo';
              local bar() = 'bar';
              {
                foo: foo() + bar(),
              }
            )";
            auto output = ParseAndInline(code);
            CHECK(output == expect);
        }

        SECTION("loop variable") {
            auto expect_loop = nlohmann::json::parse(R"(
                { "type": "singleton_map"
                , "key": "foo"
                , "value": ["barbar", "bazbaz"]
                })");

            auto const* code = R"(
              {
                foo: [x + x for x in ['bar', 'baz']],
              }
            )";
            auto output = ParseAndInline(code);
            CHECK(output == expect_loop);
        }

        SECTION("sum numbers") {
            auto const expect_sum = nlohmann::json::parse(R"(
            { "type": "singleton_map"
            , "key": "foo"
            , "value": 11.0
            })");
            auto const* code = R"(
              {
                foo: 5 + 6
              })";
            auto output = ParseAndInline(code);
            CHECK(output == expect_sum);
        }

        SECTION("builtin functions") {
            auto expect_bt = nlohmann::json::parse(R"(
                { "type": "singleton_map"
                , "key": "foo"
                , "value": "foobar"
                })");

            auto const* code = R"(
              {
                foo: 'foo' + jst.join(['bar']), // join evaluates to string
              }
            )";
            auto output = ParseAndInline(code);
            CHECK(output == expect_bt);
        }

        SECTION("builtin functions in function body") {
            auto expect_bt = nlohmann::json::parse(R"(
                { "type": "singleton_map"
                , "key": "foo"
                , "value": "foofoo"
                })");

            auto const* code = R"(
              local foo(x) = jst.join([x]) + jst.join([x]);
              {
                foo: foo('foo')
              }
            )";
            auto output = ParseAndInline(code);
            CHECK(output == expect_bt);
        }
    }

    SECTION("one operand with static type info") {
        auto expect = nlohmann::json::parse(R"(
            { "type": "singleton_map"
            , "key": "foo"
            , "value":
              { "type": "++"
              , "$1": [["foo"], {"type": "var", "name": "bar"}]
              }
            })");

        SECTION("variables") {
            auto const* code = R"(
              local foo = ['foo'];
              local bar = jst.env('bar');
              {
                foo: foo + bar,
              }
            )";
            auto output = ParseAndInline(code);
            CHECK(output == expect);
        }

        SECTION("functions") {
            auto const* code = R"(
              local foo() = ['foo'];
              local bar() = jst.env('bar');
              {
                foo: foo() + bar(),
              }
            )";
            auto output = ParseAndInline(code);
            CHECK(output == expect);
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
            auto output = ParseAndInline(code);
            CHECK(output == expect);
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
            auto output = ParseAndInline(code);
            CHECK(output == expect);
        }

        SECTION("loop variable") {
            auto expect_loop = nlohmann::json::parse(R"(
                { "type": "singleton_map"
                , "key": "foo"
                , "value":
                  { "type": "foreach"
                  , "var": "x"
                  , "range": {"type": "var", "name": "baz"}
                  , "body":
                    { "type": "join"
                    , "$1": ["bar", {"type": "var", "name": "x"}]
                    }
                  }
                })");

            auto const* code = R"(
              local bar = 'bar';
              {
                foo: [bar + x for x in jst.env('baz')],
              }
            )";
            auto output = ParseAndInline(code);
            CHECK(output == expect_loop);
        }

        SECTION("sum numbers") {
            auto const expect_sum = nlohmann::json::parse(R"(
            { "type": "singleton_map"
            , "key": "foo"
            , "value":
              { "type": "+"
              , "$1": [ {"type": "var", "name": "NUMBER"}, 6.0 ]
              }
            })");
            auto const* code = R"(
              local var = jst.env('NUMBER');
              {
                foo: var + 6
              })";
            auto output = ParseAndInline(code);
            CHECK(output == expect_sum);
        }
    }

    SECTION("no static type info") {
        SECTION("local variables") {
            auto const* code = R"(
              local foo = jst.env('foo');
              {
                foo:
                  local bar = jst.env('bar');
                  foo + bar,
              }
            )";
            CHECK_THROWS_AS(ParseAndInline(code), justlang::ASTInlineError);
        }

        SECTION("loop variable") {
            auto const* code = R"(
              {
                foo: [x + x for x in jst.env('bar')],
              }
            )";
            CHECK_THROWS_AS(ParseAndInline(code), justlang::ASTInlineError);
        }
    }

    SECTION("arithmetics") {
        SECTION("multiplication of unsupported types") {
            CHECK_NOTHROW(ParseAndInline("{ foo: 5 * 5 }"));

            CHECK_THROWS_AS(ParseAndInline("{ foo: 5 * '5' }"),
                            justlang::ASTInlineError);
            CHECK_THROWS_AS(ParseAndInline("{ foo: 5 * {} }"),
                            justlang::ASTInlineError);
            CHECK_THROWS_AS(ParseAndInline("{ foo: [] * 5 }"),
                            justlang::ASTInlineError);
            CHECK_THROWS_AS(ParseAndInline("{ foo: 'foo' * 'bar' }"),
                            justlang::ASTInlineError);
            auto const* two_vars = R"(
              local var1 = jst.env("VAR1");
              local var2 = jst.env("VAR2");
              {
                foo: var1 * var2
              }
            )";
            // multiplication assumes number types for runtime vars
            CHECK_NOTHROW(ParseAndInline(two_vars));
        }
        SECTION("unary minus with unsupported types") {
            CHECK_NOTHROW(ParseAndInline("{ foo: -5 }"));

            CHECK_THROWS_AS(ParseAndInline("{ foo: -'5' }"),
                            justlang::ASTInlineError);
            CHECK_THROWS_AS(ParseAndInline("{ foo: -{} }"),
                            justlang::ASTInlineError);
            CHECK_THROWS_AS(ParseAndInline("{ foo: -[] }"),
                            justlang::ASTInlineError);
            CHECK_THROWS_AS(ParseAndInline("{ foo: -'foo' }"),
                            justlang::ASTInlineError);
        }
        SECTION("unary minus in function body") {
            auto expect = nlohmann::json::parse(R"(
            { "type": "singleton_map"
            , "key": "foo"
            , "value": -2.0
            })");
            auto const* code = R"(
            local foo(x) = -x;
            {
              foo: foo(2)
            })";
            auto output = ParseAndInline(code);
            CHECK(output == expect);
        }
        SECTION("subtraction of unsupported types") {
            CHECK_NOTHROW(ParseAndInline("{ foo: -5 - 3 }"));

            CHECK_THROWS_AS(ParseAndInline("{ foo: 5 - '5' }"),
                            justlang::ASTInlineError);
            CHECK_THROWS_AS(ParseAndInline("{ foo: 5 - {} }"),
                            justlang::ASTInlineError);
            CHECK_THROWS_AS(ParseAndInline("{ foo: 5 - [] }"),
                            justlang::ASTInlineError);
            CHECK_THROWS_AS(ParseAndInline("{ foo: 5 - 'foo' }"),
                            justlang::ASTInlineError);
            auto const* two_vars = R"(
              local var1 = jst.env("VAR1");
              local var2 = jst.env("VAR2");
              {
                foo: var1 - var2
              }
            )";
            // subtraction assumes number types for runtime vars
            CHECK_NOTHROW(ParseAndInline(two_vars));
        }
    }
}

TEST_CASE("list_at_inlining", "[index]") {
    auto const fully_inlined = nlohmann::json::parse(R"(
              { "type": "singleton_map"
              , "key": "foo"
              , "value": "bar"
              })");

    SECTION("simple full inlining - string indexing") {
        auto const* code = R"(
              local list = ['foo', 'bar'];
              {
                foo: list["1"],
              }
            )";
        auto output = ParseAndInline(code);
        CHECK(output == fully_inlined);
    }

    SECTION("negative indexing - string indexing") {
        auto const expected = nlohmann::json::parse(R"(
              { "type": "singleton_map"
              , "key": "foo"
              , "value": "foo"
              })");
        auto const* code = R"(
              local list = ['foo', 'bar'];
              {
                foo: list["-2"],
              }
            )";
        auto output = ParseAndInline(code);
        CHECK(output == expected);
    }

    SECTION("simple full inlining - number indexing") {
        auto const* code = R"(
              local list = ['foo', 'bar'];
              {
                foo: list[1.0],
              }
            )";
        auto output = ParseAndInline(code);
        CHECK(output == fully_inlined);
    }

    SECTION("negative indexing - number indexing") {
        auto const expected = nlohmann::json::parse(R"(
              { "key": "foo"
              , "type": "singleton_map"
              , "value": "bar"
              })");
        auto const* code = R"(
              local list = ['foo', 'bar'];
              {
                foo: list[-1.0],
              }
            )";
        auto output = ParseAndInline(code);
        CHECK(output == expected);
    }

    SECTION("computed fields full inlining") {
        auto const* code = R"(
              local list = ['foo', 'bar'];
              {
                [list[0]]: list["1"],
              }
            )";
        auto output = ParseAndInline(code);
        CHECK(output == fully_inlined);
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
        auto output = ParseAndInline(code);
        CHECK(output == fully_inlined);
    }

    SECTION("index var") {
        auto expect = nlohmann::json::parse(R"(
              { "type": "singleton_map"
              , "key": "foo"
              , "value": 
                { "type": "[]"
                , "list": [ "foo", "bar" ]
                , "index": 
                  { "name": "INDEX"
                  , "type": "var"
                  }
                }
              })");

        auto const* code = R"(
              local list = ['foo', 'bar'];
              {
                foo: list[jst.env("INDEX")],
              }
            )";
        auto output = ParseAndInline(code);
        CHECK(output == expect);
    }

    SECTION("field var full inlining") {
        auto expect = nlohmann::json::parse(R"(
              { "type": "singleton_map"
              , "key": "foo"
              , "value": 
                { "name": "INDEX"
                , "type": "var"
                }
              })");

        auto const* code = R"(
              local list = [jst.env("INDEX"), 'bar'];
              {
                foo: list["0"],
              }
            )";
        auto output = ParseAndInline(code);
        CHECK(output == expect);
    }
}

TEST_CASE("ast_inline", "[read-call-separation]") {
    auto expect = nlohmann::json::parse(R"("foobar")");

    SECTION("read variable and call function") {
        auto const* code = R"(
          local foo = 'foo';
          local bar() = 'bar';
          foo + bar()
        )";
        auto output = ParseAndInline(code);
        CHECK(output == expect);
    }

    SECTION("read variable and function") {
        auto const* code = R"(
          local foo = 'foo';
          local bar() = 'bar';
          foo + bar
        )";
        CHECK_THROWS_AS(ParseAndInline(code), justlang::ASTInlineError);
    }

    SECTION("call variable and function") {
        auto const* code = R"(
          local foo = 'foo';
          local bar() = 'bar';
          foo() + bar()
        )";
        CHECK_THROWS_AS(ParseAndInline(code), justlang::ASTInlineError);
    }
}

TEST_CASE("ast_inline", "[function objects]") {
    auto expect = nlohmann::json::parse(R"(
        { "type": "singleton_map"
        , "key": "foo"
        , "value": "bar"
        })");

    SECTION("call scope object without parameters") {
        auto const* code = R"(
          local foo = 'foo';
          local bar = function() 'bar';
          {
            foo: bar(),
          }
        )";
        auto output = ParseAndInline(code);
        CHECK(output == expect);
    }

    SECTION("call scope object with parameters") {
        auto const* code = R"(
          local foo = 'foo';
          local bar = function(x) if x then 'bar' else 'baz';
          {
            foo: bar(foo),
          }
        )";
        auto output = ParseAndInline(code);
        CHECK(output == expect);
    }

    SECTION("call field object without parameters") {
        auto const* code = R"(
          local foo = 'foo';
          local bar = { bar: function() 'bar' };
          {
            foo: bar.bar(),
          }
        )";
        auto output = ParseAndInline(code);
        CHECK(output == expect);
    }

    SECTION("call field object with parameters") {
        auto const* code = R"(
          local foo = 'foo';
          local bar = { bar: function(x) if x then 'bar' else 'baz' };
          {
            foo: bar.bar(foo),
          }
        )";
        auto output = ParseAndInline(code);
        CHECK(output == expect);
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
        auto output = ParseAndInline(code);
        CHECK(output == expect);
    }

    SECTION("pass inline object as parameter for calling") {
        auto const* code = R"(
          local foo = 'foo';
          local launcher(func) = func(foo);
          {
            foo: launcher(function(x) if x then 'bar' else 'baz'),
          }
        )";
        auto output = ParseAndInline(code);
        CHECK(output == expect);
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
        auto output = ParseAndInline(code);
        CHECK(output == expect);
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
        auto output = ParseAndInline(code);
        CHECK(output == expect);
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
        auto output = ParseAndInline(code);
        CHECK(output == expect);
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
        auto output = ParseAndInline(code);
        CHECK(output == expect);
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
        auto output = ParseAndInline(code);
        CHECK(output == expect);
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
        auto output = ParseAndInline(code);
        CHECK(output == expect);
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
        auto output = ParseAndInline(code);
        CHECK(output == expect);
    }
}
