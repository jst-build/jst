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

#include "justlang/ast/native/parser.hpp"
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

[[nodiscard]] auto jsonnetParser(std::string const& code,
                                 std::string const& entry = "<stdin>",
                                 file_map_t const& files = {}) -> std::string {
    auto file_data = justlang::FileData{
        .location = justlang::FileLocation{.repo = "", .path = entry},
        .content = code};
    auto org_parser = justlang::Parser::Create(CreateMocReader(&files));
    auto org_ast = org_parser->ParseData(file_data);
    return justlang::ASTToStringVisitor{}.Dump(*org_ast);
}

[[nodiscard]] auto nativeParser(std::string const& code,
                                std::string const& entry = "<stdin>",
                                file_map_t const& files = {}) -> std::string {
    auto file_data = justlang::FileData{
        .location = justlang::FileLocation{.repo = "", .path = entry},
        .content = code};
    auto parser = justlang::NativeParser::Create(CreateMocReader(&files));
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
        auto expect = jsonnetParser(code);
        CHECK(output == expect);
    }

    SECTION("variables inlining other variables") {
        auto const* code = R"(
          local foo = 'foo';
          local bar = foo;
          {
            foo: bar,
          })";
        auto output = nativeParser(code);
        auto expect = jsonnetParser(code);
        CHECK(output == expect);
    }

    SECTION("variables in computed field") {
        auto const* code = R"(
          local foo = 'foo';
          {
            [foo]: foo,
          })";
        auto output = nativeParser(code);
        auto expect = jsonnetParser(code);
        CHECK(output == expect);
    }

    SECTION("computed variable field") {
        auto const* code = R"(
          local map = {[jst.env('NAME')]: 'foo'};
          {
            foo: map.foo,
          })";

        auto output = nativeParser(code);
        auto expect = jsonnetParser(code);
        CHECK(output == expect);
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
        auto expect = jsonnetParser(code);
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
        auto output = nativeParser(code);
        auto expect = jsonnetParser(code);
        CHECK(output == expect);
    }

    SECTION("functions in computed field") {
        auto const* code = R"(
              local foo(x) = x;
              {
                [foo('foo')]: foo('foo'),
              }
            )";
        auto output = nativeParser(code);
        auto expect = jsonnetParser(code);
        CHECK(output == expect);
    }

    SECTION("computed variable field") {
        auto const* code = R"(
        local map = {[jst.env('NAME')]: 'foo'};
        {
          foo: map.foo,
        })";

        auto output = nativeParser(code);
        auto expect = jsonnetParser(code);
        CHECK(output == expect);
    }
    SECTION("variable shadows previous variable") {
        auto const* code = R"(
        local foo = 'bar';
        local foo = 'foo';
        {
          foo: foo,
        })";
        auto output = nativeParser(code);
        auto expect = jsonnetParser(code);
        CHECK(output == expect);
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
        auto expect = jsonnetParser(code);
        CHECK(output == expect);
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
        auto expect = jsonnetParser(code);
        CHECK(output == expect);
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
        auto expect = jsonnetParser(code);
        CHECK(output == expect);
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
        auto expect = jsonnetParser(code);
        CHECK(output == expect);
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
        auto expect = jsonnetParser(code);
        CHECK(output == expect);
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
        auto expect = jsonnetParser(code);
        CHECK(output == expect);
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
        auto expect = jsonnetParser(code);
        CHECK(output == expect);
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
        auto expect = jsonnetParser(code);
        CHECK(output == expect);
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
        auto expect = jsonnetParser(code);
        CHECK(output == expect);
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
        auto expect = jsonnetParser(code);
        CHECK(output == expect);
    }
}

TEST_CASE("ast_parser", "[special strings]") {
    SECTION("ref-strings") {
        SECTION("plain reference") {
            auto const* code = R"(@':foo')";
            auto output = nativeParser(code);
            auto expect = jsonnetParser(code);
            CHECK(output == expect);
        }

        SECTION("reference with ''") {
            auto const* code = R"(@':foo''bar')";
            auto output = nativeParser(code);
            auto expect = jsonnetParser(code);
            CHECK(output == expect);
        }

        SECTION("reference as JSON") {
            auto const* code = R"(@':"foo"')";
            auto output = nativeParser(code);
            auto expect = jsonnetParser(code);
            CHECK(output == expect);
        }

        SECTION("reference as JSON with special chars") {
            auto const* code = R"(@':"foo \"\n bar"')";
            auto output = nativeParser(code);
            auto expect = jsonnetParser(code);
            CHECK(output == expect);
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
            auto expect = jsonnetParser(code);
            CHECK(output == expect);
        }

        SECTION("runtime data") {
            auto const* code = R"(
            local test(x) = jst.keys(x);
            {
              foo: test(jst.env('map')),
            }
          )";
            auto output = nativeParser(code);
            auto expect = jsonnetParser(code);
            CHECK(output == expect);
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
            auto expect = jsonnetParser(code);
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
            auto output = nativeParser(code);
            auto expect = jsonnetParser(code);
            CHECK(output == expect);
        }

        SECTION("loop variable") {
            auto const* code = R"(
            {
              foo: [x + x for x in ['bar', 'baz']],
            }
          )";
            auto output = nativeParser(code);
            auto expect = jsonnetParser(code);
            CHECK(output == expect);
        }

        SECTION("sum numbers") {
            auto const* code = R"(
            {
              foo: 5 + 6
            })";
            auto output = nativeParser(code);
            auto expect = jsonnetParser(code);
            CHECK(output == expect);
        }

        SECTION("builtin functions") {

            auto const* code = R"(
            {
              foo: 'foo' + jst.join(['bar']), // join evaluates to string
            }
          )";
            auto output = nativeParser(code);
            auto expect = jsonnetParser(code);
            CHECK(output == expect);
        }

        SECTION("builtin functions in function body") {
            auto const* code = R"(
            local foo(x) = jst.join([x]) + jst.join([x]);
            {
              foo: foo('foo')
            }
          )";
            auto output = nativeParser(code);
            auto expect = jsonnetParser(code);
            CHECK(output == expect);
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
            auto expect = jsonnetParser(code);
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
            auto output = nativeParser(code);
            auto expect = jsonnetParser(code);
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
            auto output = nativeParser(code);
            auto expect = jsonnetParser(code);
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
            auto output = nativeParser(code);
            auto expect = jsonnetParser(code);
            CHECK(output == expect);
        }

        SECTION("loop variable") {
            auto const* code = R"(
            local bar = 'bar';
            {
              foo: [bar + x for x in jst.env('baz')],
            }
          )";
            auto output = nativeParser(code);
            auto expect = jsonnetParser(code);
            CHECK(output == expect);
        }

        SECTION("sum numbers") {
            auto const* code = R"(
            local var = jst.env('NUMBER');
            {
              foo: var + 6
            })";
            auto output = nativeParser(code);
            auto expect = jsonnetParser(code);
            CHECK(output == expect);
        }

        SECTION("unary minus in function body") {
            auto const* code = R"(
            local foo(x) = -x;
            {
              foo: foo(2)
            })";
            auto output = nativeParser(code);
            auto expect = jsonnetParser(code);
            CHECK(output == expect);
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
        auto expect = jsonnetParser(code);
        CHECK(output == expect);
    }

    SECTION("negative indexing - string indexing") {
        auto const* code = R"(
            local list = ['foo', 'bar'];
            {
              foo: list["-2"],
            }
          )";
        auto output = nativeParser(code);
        auto expect = jsonnetParser(code);
        CHECK(output == expect);
    }

    SECTION("simple full inlining - number indexing") {
        auto const* code = R"(
            local list = ['foo', 'bar'];
            {
              foo: list[1.0],
            }
          )";
        auto output = nativeParser(code);
        auto expect = jsonnetParser(code);
        CHECK(output == expect);
    }

    SECTION("negative indexing - number indexing") {
        auto const* code = R"(
            local list = ['foo', 'bar'];
            {
              foo: list[-1.0],
            }
          )";
        auto output = nativeParser(code);
        auto expect = jsonnetParser(code);
        CHECK(output == expect);
    }

    SECTION("computed fields full inlining") {
        auto const* code = R"(
            local list = ['foo', 'bar'];
            {
              [list[0]]: list["1"],
            }
          )";
        auto output = nativeParser(code);
        auto expect = jsonnetParser(code);
        CHECK(output == expect);
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
        auto expect = jsonnetParser(code);
        CHECK(output == expect);
    }

    SECTION("index var") {
        auto const* code = R"(
            local list = ['foo', 'bar'];
            {
              foo: list[jst.env("INDEX")],
            }
          )";
        auto output = nativeParser(code);
        auto expect = jsonnetParser(code);
        CHECK(output == expect);
    }

    SECTION("field var full inlining") {

        auto const* code = R"(
            local list = [jst.env("INDEX"), 'bar'];
            {
              foo: list["0"],
            }
          )";
        auto output = nativeParser(code);
        auto expect = jsonnetParser(code);
        CHECK(output == expect);
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
        auto expect = jsonnetParser(code);
        CHECK(output == expect);
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
        auto expect = jsonnetParser(code);
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
        auto output = nativeParser(code);
        auto expect = jsonnetParser(code);
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
        auto output = nativeParser(code);
        auto expect = jsonnetParser(code);
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
        auto output = nativeParser(code);
        auto expect = jsonnetParser(code);
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
        auto output = nativeParser(code);
        auto expect = jsonnetParser(code);
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
        auto output = nativeParser(code);
        auto expect = jsonnetParser(code);
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
        auto output = nativeParser(code);
        auto expect = jsonnetParser(code);
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
        auto output = nativeParser(code);
        auto expect = jsonnetParser(code);
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
        auto output = nativeParser(code);
        auto expect = jsonnetParser(code);
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
        auto output = nativeParser(code);
        auto expect = jsonnetParser(code);
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
        auto output = nativeParser(code);
        auto expect = jsonnetParser(code);
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
        auto output = nativeParser(code);
        auto expect = jsonnetParser(code);
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
        auto output = nativeParser(code);
        auto expect = jsonnetParser(code);
        CHECK(output == expect);
    }
}
TEST_CASE("ast_parser", "[get functions]") {
    SECTION("get() with mandatory arguments") {
        {
            auto const* code = "jst.get('foo', {})";
            auto output = nativeParser(code);
            auto expect = jsonnetParser(code);
            CHECK(output == expect);
        }

        {
            auto const* code = "jst.get('foo', map={})";
            auto output = nativeParser(code);
            auto expect = jsonnetParser(code);
            CHECK(output == expect);
        }

        {
            auto const* code = "jst.get(map={}, key='foo')";
            auto output = nativeParser(code);
            auto expect = jsonnetParser(code);
            CHECK(output == expect);
        }

        {
            auto const* code = "jst.get(key='foo', map={})";
            auto output = nativeParser(code);
            auto expect = jsonnetParser(code);
            CHECK(output == expect);
        }
        {
            auto const* code = R"(
        jst.nub_right([
          if jst.env('foo') then "foo" else "bar",
          if jst.env('bar') then "bar" else "foo",
        ])
      )";
            auto output = nativeParser(code);
            auto expect = jsonnetParser(code);
            CHECK(output == expect);
        }
        {
            auto const* code = "jst.at(list=[jst.env('foo'), 'bar'],index=-1)";
            auto output = nativeParser(code);
            auto expect = jsonnetParser(code);
            CHECK(output == expect);
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
            auto expect = jsonnetParser(code);
            CHECK(output == expect);
        }
        {
            auto const* code = "{ foo: 5 - 3 }";
            auto output = nativeParser(code);
            auto expect = jsonnetParser(code);
            CHECK(output == expect);
        }
        {
            auto const* code = "jst.get('foo', {foo: null}, default='bar')";
            auto expect = jsonnetParser(code);
            auto output = nativeParser(code);
            CHECK(output == expect);
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
        auto expect = jsonnetParser(files["foo.jst"], "foo.jst", files);
        auto output = nativeParser(files["foo.jst"], "foo.jst", files);
        CHECK(output == expect);
    }

    SECTION("cycle via self import") {
        auto files = file_map_t{};
        files["foo.jst"] = R"(
          local foo = import 'foo.jst';
          foo
        )";
        CHECK_THROWS(jsonnetParser(files["foo.jst"], "foo.jst", files));
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
        CHECK_THROWS(jsonnetParser(files["foo.jst"], "foo.jst", files));
        CHECK_THROWS(nativeParser(files["foo.jst"], "foo.jst", files));
    }
}

}  // namespace
