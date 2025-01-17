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

#include "justlang/preprocessor.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>  // IWYU pragma: keep
#include <nlohmann/json_fwd.hpp>

#include "justlang/ast/ast.hpp"
#include "justlang/file_data.hpp"

namespace {

using file_map_t = std::unordered_map<std::string, std::string>;

[[nodiscard]] auto Preprocess(std::string const& global_repo,
                              std::filesystem::path const& file_path,
                              file_map_t const& files) {
    auto file_map_reader =
        [&files](auto const& import_from,
                 auto const& file_path,
                 auto const* repo) -> std::optional<justlang::FileData> {
        auto data = justlang::FileData{};
        data.location.repo = repo == nullptr ? import_from.repo : *repo;
        data.location.path = (repo == nullptr and file_path.is_relative()
                                  ? import_from.path.parent_path() / file_path
                                  : file_path)
                                 .lexically_proximate("/")
                                 .lexically_normal();
        if (auto file_it = files.find(data.location.ToString());
            file_it != files.end()) {
            data.content = file_it->second;
            return data;
        }
        return std::nullopt;
    };
    auto preproc = justlang::Preprocessor{
        std::move(file_map_reader),
        [](auto /*type*/, auto const& msg) { std::cerr << msg; }};
    return preproc.Process(global_repo, file_path, justlang::FileType::Plain);
}

[[nodiscard]] auto Preprocess(std::string const& code) {
    auto input_file = justlang::FileLocation{.repo = "", .path = "<none>"};
    return Preprocess(input_file.repo,
                      input_file.path,
                      file_map_t{{input_file.ToString(), code}});
}

[[nodiscard]] auto ToJson(justlang::ASTNodePtr const& ast) {
    auto moc_reader = [](auto const&, auto const&, auto const*)
        -> std::optional<justlang::FileData> { return std::nullopt; };
    auto preproc = justlang::Preprocessor{
        std::move(moc_reader),
        [](auto /*type*/, auto const& msg) { std::cerr << msg; }};
    return preproc.Serialize(ast);
}

}  // namespace

TEST_CASE("preprocessor", "[ast_translate]") {
    SECTION("object translation") {
        {
            auto const* code = "{}";
            auto expected = nlohmann::json::parse(R"({"type":"empty_map"})");
            auto just_ast = Preprocess(code);
            CHECK(ToJson(just_ast) == expected);
        }

        {
            auto const* code = "{foo: 'foo'}";
            auto expected = nlohmann::json::parse(R"(
        { "type": "singleton_map"
        , "key": "foo"
        , "value": "foo"
        })");
            auto just_ast = Preprocess(code);
            CHECK(ToJson(just_ast) == expected);
        }

        {
            auto const* code = "{foo: 'foo', bar: 'bar'}";
            auto expected = nlohmann::json::parse(R"(
        { "type": "map_union"
        , "$1":
          [ { "type": "singleton_map"
            , "key": "foo"
            , "value": "foo"
            }
          , { "type": "singleton_map"
            , "key": "bar"
            , "value": "bar"
            }
          ]
        })");
            auto just_ast = Preprocess(code);
            CHECK(ToJson(just_ast) == expected);
        }
    }
}

TEST_CASE("preprocessor", "[builtin_functions]") {
    SECTION("env() with mandatory arguments") {
        auto expected = nlohmann::json::parse(R"({"type":"var","name":"foo"})");

        {
            auto const* code = "jst.env('foo')";
            auto just_ast = Preprocess(code);
            CHECK(ToJson(just_ast) == expected);
        }

        {
            auto const* code = "jst.env(name='foo')";
            auto just_ast = Preprocess(code);
            CHECK(ToJson(just_ast) == expected);
        }
    }

    SECTION("env() with all arguments") {
        auto expected = nlohmann::json::parse(
            R"({"type":"var","name":"foo","default":"bar"})");
        {
            auto const* code = "jst.env('foo', 'bar')";
            auto just_ast = Preprocess(code);
            CHECK(ToJson(just_ast) == expected);
        }

        {
            auto const* code = "jst.env('foo', default='bar')";
            auto just_ast = Preprocess(code);
            CHECK(ToJson(just_ast) == expected);
        }

        {
            auto const* code = "jst.env(name='foo', default='bar')";
            auto just_ast = Preprocess(code);
            CHECK(ToJson(just_ast) == expected);
        }

        {
            auto const* code = "jst.env(default='bar', name='foo')";
            auto just_ast = Preprocess(code);
            CHECK(ToJson(just_ast) == expected);
        }
    }

    SECTION("get() with mandatory arguments") {
        auto expected = nlohmann::json::parse(R"(null)");

        {
            auto const* code = "jst.get('foo', {})";
            auto just_ast = Preprocess(code);
            CHECK(ToJson(just_ast) == expected);
        }

        {
            auto const* code = "jst.get('foo', map={})";
            auto just_ast = Preprocess(code);
            CHECK(ToJson(just_ast) == expected);
        }

        {
            auto const* code = "jst.get(map={}, key='foo')";
            auto just_ast = Preprocess(code);
            CHECK(ToJson(just_ast) == expected);
        }

        {
            auto const* code = "jst.get(key='foo', map={})";
            auto just_ast = Preprocess(code);
            CHECK(ToJson(just_ast) == expected);
        }

        {
            auto const* code = "jst.get(map={}, 'foo')";
            auto just_ast = Preprocess(code);
            CHECK(just_ast == nullptr);
        }
    }

    SECTION("get() with all arguments") {
        SECTION("statically evaluated") {
            auto expected = nlohmann::json::parse(R"("bar")");

            {
                auto const* code = "jst.get('foo', {}, 'bar')";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.get('foo', {}, default='bar')";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.get('foo', map={}, default='bar')";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.get('foo', default='bar', map={})";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.get(default='bar', map={}, key='foo')";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code =
                    "jst.get('bar', {[jst.env('foo')]: 'foo', bar: 'bar'})";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.get('foo', {foo: null}, default='bar')";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.get(map={}, 'foo', default='bar')";
                auto just_ast = Preprocess(code);
                CHECK(just_ast == nullptr);
            }
        }

        SECTION("runtime evaluated") {
            {
                auto expected = nlohmann::json::parse(R"(
                    { "type": "lookup"
                    , "key": {"type": "var", "name": "foo"}
                    , "map": {"type": "empty_map"}
                    , "default": "bar"
                    })");
                auto const* code = "jst.get(jst.env('foo'), {}, 'bar')";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                    { "type": "lookup"
                    , "key": "foo"
                    , "map":
                      { "type": "if"
                      , "cond": {"type": "var", "name": "baz"}
                      , "then": {"type": "var", "name": "baz"}
                      , "else": {"type": "empty_map"}
                      }
                    , "default": "bar"
                    })");
                auto const* code = "jst.get('foo', jst.env('baz'), 'bar')";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                    { "type": "lookup"
                    , "key": "foo"
                    , "map":
                      { "type": "map_union"
                      , "$1":
                        [ { "type": "singleton_map"
                          , "key": "foo"
                          , "value": "foo"
                          }
                        , { "type": "singleton_map"
                          , "key": {"type": "var", "name": "bar"}
                          , "value": "bar"
                          }
                        , { "type": "singleton_map"
                          , "key": "baz"
                          , "value": "baz"
                          }
                        ]
                      }
                    })");
                auto const* code = R"(
                  jst.get(
                    'foo', {foo: 'foo', [jst.env('bar')]: 'bar', baz: 'baz'}
                ))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("type errors") {
            {
                auto const* code = "jst.get(['foo'], {}, default='bar')";
                CHECK_FALSE(Preprocess(code));
            }

            {
                auto const* code = "jst.get('foo', [], default='bar')";
                CHECK_FALSE(Preprocess(code));
            }
        }
    }

    SECTION("join() with mandatory arguments") {
        auto expected = nlohmann::json::parse(R"("foobar")");

        {
            auto const* code = "jst.join(['foo', 'bar'])";
            auto just_ast = Preprocess(code);
            CHECK(ToJson(just_ast) == expected);
        }

        {
            auto const* code = "jst.join(items=['foo', 'bar'])";
            auto just_ast = Preprocess(code);
            CHECK(ToJson(just_ast) == expected);
        }
    }

    SECTION("join() with all arguments") {
        SECTION("statically evaluated") {
            auto expected = nlohmann::json::parse(R"("foo-bar")");

            {
                auto const* code = "jst.join(['foo', 'bar'], '-')";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.join(['foo', 'bar'], sep='-')";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.join(items=['foo', 'bar'], sep='-')";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.join(sep='-', items=['foo', 'bar'])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("runtime evaluated") {
            {
                auto expected = nlohmann::json::parse(R"(
                    { "type": "join"
                    , "separator": "-"
                    , "$1": ["foo", {"type": "var", "name": "bar"}]
                    })");
                auto const* code = "jst.join(['foo', jst.env('bar')], '-')";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                    { "type": "join"
                    , "separator": "-"
                    , "$1": {"type": "var", "name": "foo"}
                    })");
                auto const* code = "jst.join(jst.env('foo'), sep='-')";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                    { "type": "join"
                    , "separator": {"type": "var", "name": "baz"}
                    , "$1": ["foo", "bar"]
                    })");
                auto const* code =
                    "jst.join(['foo', 'bar'], sep=jst.env('baz'))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("type errors") {
            {
                auto const* code = "jst.join('foo', sep='-')";
                CHECK_FALSE(Preprocess(code));
            }

            {
                auto const* code = "jst.join(jst.join([]), sep='-')";
                CHECK_FALSE(Preprocess(code));
            }

            {
                auto const* code = "jst.join(['foo', 42], sep='-')";
                CHECK_FALSE(Preprocess(code));
            }

            {
                auto const* code = "jst.join(['foo', 'bar'], sep=['-'])";
                CHECK_FALSE(Preprocess(code));
            }
        }
    }

    SECTION("flatten() with mandatory arguments") {
        SECTION("statically evaluated") {
            auto expected = nlohmann::json::parse(R"(["foo", "bar", "baz"])");

            {
                auto const* code = "jst.flatten([['foo', 'bar'], ['baz']])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code =
                    "jst.flatten(lists=[['foo', 'bar'], ['baz']])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("runtime evaluated") {
            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "++"
                  , "$1":
                    [ ["foo", "bar"]
                    , {"type": "var", "name": "baz"}
                    ]
                  })");

                auto const* code =
                    "jst.flatten([['foo', 'bar'], jst.env('baz')])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "++"
                  , "$1": {"type": "var", "name": "foo"}
                  })");

                auto const* code = "jst.flatten(jst.env('foo'))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("type errors") {
            {
                auto const* code = "jst.flatten('foo')";
                CHECK_FALSE(Preprocess(code));
            }

            {
                auto const* code = "jst.flatten(['foo'])";
                CHECK_FALSE(Preprocess(code));
            }

            {
                auto const* code = "jst.flatten([['foo'], 42])";
                CHECK_FALSE(Preprocess(code));
            }
        }
    }

    SECTION("union() with mandatory arguments") {
        SECTION("statically evaluated") {
            auto expected = nlohmann::json::parse(R"(
                { "type":"map_union"
                , "$1":
                  [ {"type": "singleton_map"
                    , "key": "foo"
                    , "value": "foo"
                    }
                  , { "type":"singleton_map"
                    , "key":"bar"
                    , "value":"bar"
                    }
                  ]
                })");

            {
                auto const* code =
                    "jst.union([{foo:'foo','bar':'baz'},{bar:'bar'}])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code =
                    "jst.union([{foo:'foo','bar':'baz'},{bar:'bar'}], false)";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code =
                    "jst.union([{foo:'foo'},{bar:'bar'}], 'disjoint')";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code =
                    "jst.union(disjoint=false,"
                    "      maps=[{foo:'foo','bar':'baz'},{bar:'bar'}])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = R"(
                  jst.union(
                    disjoint=false,
                    maps=[
                      {foo:'foo','bar':'baz'},
                      {foo:'foo','bar':'bar'}
                    ],
                  )
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code =
                    "jst.union(disjoint='yes', maps=[{foo:'foo'},{bar:'bar'}])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = R"(
                  jst.union(
                    disjoint='yes',
                    maps=[{foo:'foo'},{bar:'bar'},{foo:'foo'}],
                  )
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = R"(
                  jst.union(
                    disjoint='yes',
                    maps=[{foo:'foo'},{bar:'bar'},{foo:'fox'}],
                  )
                )";
                CHECK_FALSE(Preprocess(code));
            }
        }

        SECTION("runtime evaluated") {
            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "map_union"
                  , "$1":
                    [ { "type": "map_union"
                      , "$1":
                        [ { "type": "singleton_map"
                          , "key": "foo"
                          , "value": "foo"
                          }
                        , { "type": "singleton_map"
                          , "key": {"type": "var", "name": "bar"}
                          , "value": "baz"
                          }
                        ]
                      }
                    , { "type": "singleton_map"
                      , "key": "bar"
                      , "value": "bar"
                      }
                    ]
                  })");

                auto const* code =
                    "jst.union([{foo:'foo',[jst.env('bar')]:'baz'},{bar:'bar'}]"
                    ")";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "disjoint_map_union"
                  , "$1":
                    [ { "type": "map_union"
                      , "$1":
                        [ { "type": "singleton_map"
                          , "key": "foo"
                          , "value": "foo"
                          }
                        , { "type": "singleton_map"
                          , "key": {"type": "var", "name": "bar"}
                          , "value": "baz"
                          }
                        ]
                      }
                    , { "type": "singleton_map"
                      , "key": "bar"
                      , "value": "bar"
                      }
                    ]
                  })");

                auto const* code =
                    "jst.union([{foo:'foo',[jst.env('bar')]:'baz'},{bar:'bar'}]"
                    ", true)";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("type errors") {
            {
                auto const* code = "jst.union({foo: 'bar'})";
                CHECK_FALSE(Preprocess(code));
            }

            {
                auto const* code = "jst.union(jst.union([{}]))";
                CHECK_FALSE(Preprocess(code));
            }

            {
                auto const* code = "jst.union([{foo: 'bar'}, 42])";
                CHECK_FALSE(Preprocess(code));
            }
        }
    }

    SECTION("all() with mandatory arguments") {
        SECTION("statically evaluated true") {
            auto expected = nlohmann::json::parse(R"(true)");

            {
                auto const* code = "jst.all(['foo', ['bar']])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.all(args=['foo', ['bar']])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("statically evaluated false") {
            auto expected = nlohmann::json::parse(R"(false)");

            {
                auto const* code = "jst.all(['foo', []])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.all(args=['foo', []])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.all(args=[jst.env('foo'), []])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("runtime evaluated") {
            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "and"
                  , "$1": ["foo", {"type": "var", "name": "bar"}]
                  })");

                auto const* code = "jst.all(['foo', jst.env('bar')])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "and"
                  , "$1": {"type": "var", "name": "foo"}
                  })");

                auto const* code = "jst.all(jst.env('foo'))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }
    }

    SECTION("any() with mandatory arguments") {
        SECTION("statically evaluated true") {
            auto expected = nlohmann::json::parse(R"(true)");

            {
                auto const* code = "jst.any(['', ['bar']])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.any(args=['', ['bar']])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.any(args=[jst.env(''), ['bar']])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("statically evaluated false") {
            auto expected = nlohmann::json::parse(R"(false)");

            {
                auto const* code = "jst.any(['', []])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.any(args=['', []])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("runtime evaluated") {
            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "or"
                  , "$1":
                    [ {"type": "var", "name": "foo"}
                    , {"type": "var", "name": "bar"}
                    ]
                  })");

                auto const* code = "jst.any([jst.env('foo'), jst.env('bar')])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "or"
                  , "$1": {"type": "var", "name": "foo"}
                  })");

                auto const* code = "jst.any(jst.env('foo'))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }
    }

    SECTION("sum() with mandatory arguments") {
        SECTION("statically evaluated") {
            auto expected = nlohmann::json::parse(R"(111.0)");

            {
                auto const* code = "jst.sum([42, 69])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.sum(numbers=[42, 69])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("runtime evaluated") {
            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "+"
                  , "$1": [42.0, {"type": "var", "name": "69"}]
                  })");

                auto const* code = "jst.sum([42, jst.env('69')])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "+"
                  , "$1": {"type": "var", "name": "numbers"}
                  })");

                auto const* code = "jst.sum(jst.env('numbers'))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("type errors") {
            {
                auto const* code = "jst.sum(42)";
                CHECK_FALSE(Preprocess(code));
            }

            {
                auto const* code = "jst.sum([42, 'foo'])";
                CHECK_FALSE(Preprocess(code));
            }
        }
    }

    SECTION("prod() with mandatory arguments") {
        SECTION("statically evaluated") {
            auto expected = nlohmann::json::parse(R"(2898.0)");

            {
                auto const* code = "jst.prod([42, 69])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.prod(numbers=[42, 69])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("runtime evaluated") {
            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "*"
                  , "$1": [42.0, {"type": "var", "name": "69"}]
                  })");

                auto const* code = "jst.prod([42, jst.env('69')])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "*"
                  , "$1": {"type": "var", "name": "numbers"}
                  })");

                auto const* code = "jst.prod(jst.env('numbers'))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("type errors") {
            {
                auto const* code = "jst.prod(42)";
                CHECK_FALSE(Preprocess(code));
            }

            {
                auto const* code = "jst.prod([42, 'foo'])";
                CHECK_FALSE(Preprocess(code));
            }
        }
    }

    SECTION("eq() with mandatory arguments") {
        SECTION("statically evaluated true") {
            auto expected = nlohmann::json::parse(R"(true)");

            {
                auto const* code = "jst.eq('foo', 'foo')";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.eq(lhs='foo', rhs='foo')";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.eq(jst.env('foo'), jst.env('foo'))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
            {
                auto const* code = R"(
                    jst.eq(if jst.env('foo') then 'bar' else 'baz',
                           if jst.env('foo') then 'bar' else 'baz'))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("statically evaluated false") {
            auto expected = nlohmann::json::parse(R"(false)");

            {
                auto const* code = "jst.eq('42', 42)";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.eq(lhs='foo', rhs=['foo'])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.eq(lhs='foo', rhs=[jst.env('foo')])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("runtime evaluated") {
            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "=="
                  , "$1": "foo"
                  , "$2": {"type": "var", "name": "bar"}
                  })");

                auto const* code = "jst.eq('foo', jst.env('bar'))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "=="
                  , "$1": {"type": "var", "name": "foo"}
                  , "$2": {"type": "var", "name": "bar"}
                  })");

                auto const* code = "jst.eq(jst.env('foo'), jst.env('bar'))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "=="
                  , "$1":
                    { "type": "if"
                    , "cond": {"type": "var", "name": "foo"}
                    , "then": "foo"
                    , "else": "bar"
                    }
                  , "$2":
                    { "type": "if"
                    , "cond": {"type": "var", "name": "bar"}
                    , "then": "bar"
                    , "else": "foo"
                    }
                  })");

                auto const* code = R"(
                  jst.eq(if jst.env('foo') then 'foo' else 'bar',
                         if jst.env('bar') then 'bar' else 'foo')
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }
    }

    SECTION("not() with mandatory arguments") {
        SECTION("statically evaluated true") {
            auto expected = nlohmann::json::parse(R"(true)");

            {
                auto const* code = "jst.not(null)";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.not(false)";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.not(0)";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.not('')";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.not([])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.not({})";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.not(expr=false)";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("statically evaluated false") {
            auto expected = nlohmann::json::parse(R"(false)");

            {
                auto const* code = "jst.not(true)";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.not(1)";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.not('yes')";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.not(['yes'])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.not({yes: true})";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.not(expr=true)";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("runtime evaluated") {
            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "not"
                  , "$1": {"type": "var", "name": "foo"}
                  })");

                auto const* code = "jst.not(jst.env('foo'))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }
    }

    SECTION("fail() with mandatory arguments") {
        auto expected = nlohmann::json::parse(R"({"type":"fail","msg":"foo"})");

        {
            auto const* code = "jst.fail('foo')";
            auto just_ast = Preprocess(code);
            CHECK(ToJson(just_ast) == expected);
        }

        {
            auto const* code = "jst.fail(msg='foo')";
            auto just_ast = Preprocess(code);
            CHECK(ToJson(just_ast) == expected);
        }
    }

    SECTION("json_encode() with mandatory arguments") {
        SECTION("statically evaluated") {
            auto expected = nlohmann::json::parse(R"("[\"foo\",\"bar\"]")");

            {
                auto const* code = "jst.json_encode(['foo','bar'])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.json_encode(data=['foo','bar'])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("runtime evaluated") {
            auto expected = nlohmann::json::parse(R"(
                { "type": "json_encode"
                , "$1": ["foo", {"type": "var", "name": "bar"}]
                })");

            {
                auto const* code = "jst.json_encode(['foo',jst.env('bar')])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code =
                    "jst.json_encode(data=['foo',jst.env('bar')])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }
    }

    SECTION("at() with mandatory arguments") {
        SECTION("statically evaluated") {
            auto expected = nlohmann::json::parse(R"("bar")");

            {
                auto const* code = "jst.at('1', [jst.env('foo'), 'bar'])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.at('1', list=[jst.env('foo'), 'bar'])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code =
                    "jst.at(list=[jst.env('foo'), 'bar'], index='1')";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code =
                    "jst.at(list=[jst.env('foo'), 'bar'], index='-1')";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code =
                    "jst.at(list=['bar', jst.env('foo')], index='baz')";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code =
                    "jst.at(list=[jst.env('foo'), 'bar'], index=1)";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code =
                    "jst.at(list=[jst.env('foo'), 'bar'], index=-1)";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code =
                    "jst.at(index='1', list=[jst.env('foo'), 'bar'])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = "jst.at(list=[jst.env('foo'), 'bar'], '1')";
                auto just_ast = Preprocess(code);
                CHECK(just_ast == nullptr);
            }
        }

        SECTION("runtime evaluated") {
            {
                auto expected = nlohmann::json::parse(R"(
                    { "type": "[]"
                    , "list": ["foo", "bar"]
                    , "index": {"type": "var", "name": "1"}
                    }
                )");
                auto const* code = "jst.at(jst.env('1'), ['foo', 'bar'])";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                    { "type": "[]"
                    , "list": {"type": "var", "name": "foo"}
                    , "index": "1"
                    }
                )");
                auto const* code = "jst.at('1', list=jst.env('foo'))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("type errors") {
            {
                auto const* code = "jst.at(1, 'foo')";
                CHECK_FALSE(Preprocess(code));
            }

            {
                auto const* code = "jst.at(true, ['foo', 'bar'])";
                CHECK_FALSE(Preprocess(code));
            }
        }
    }

    SECTION("at() with all arguments") {
        auto expected = nlohmann::json::parse(R"("baz")");

        {
            auto const* code = "jst.at('2', ['foo', 'bar'], 'baz')";
            auto just_ast = Preprocess(code);
            CHECK(ToJson(just_ast) == expected);
        }

        {
            auto const* code = "jst.at('2', ['foo', 'bar'], default='baz')";
            auto just_ast = Preprocess(code);
            CHECK(ToJson(just_ast) == expected);
        }

        {
            auto const* code =
                "jst.at('2', list=['foo', 'bar'], default='baz')";
            auto just_ast = Preprocess(code);
            CHECK(ToJson(just_ast) == expected);
        }

        {
            auto const* code =
                "jst.at('2', default='baz', list=['foo', 'bar'])";
            auto just_ast = Preprocess(code);
            CHECK(ToJson(just_ast) == expected);
        }

        {
            auto const* code =
                "jst.at(default='baz', list=['foo', 'bar'], index='2')";
            auto just_ast = Preprocess(code);
            CHECK(ToJson(just_ast) == expected);
        }

        {
            // TODO(should-fail): positional is not allowed after key-val
            auto const* code =
                "jst.at(list=['foo', 'bar'], '2', default='baz')";
            auto just_ast = Preprocess(code);
            CHECK(just_ast == nullptr);
        }
    }

    SECTION("foreach() with mandatory arguments") {
        SECTION("statically evaluated") {
            {
                auto expected = nlohmann::json::parse(
                    R"([["foo", "bar"], ["foo", "baz"]])");
                auto const* code = R"(
                  local foo = 'foo';
                  local make_list(x) = [foo, x];
                  jst.foreach(make_list, ['bar', 'baz'])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  [ ["foo", "bar"]
                  , ["foo", {"type": "var", "name": "baz"}]
                  ]
                )");
                auto const* code = R"(
                  local foo = 'foo';
                  local make_list(x) = [foo, x];
                  jst.foreach(make_list, ['bar', jst.env('baz')])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("runtime evaluated") {
            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "foreach"
                  , "var": "x"
                  , "range": {"type": "var", "name": "bar"}
                  , "body": ["foo", {"type": "var", "name": "x"}]
                  }
                )");
                auto const* code = R"(
                  local foo = 'foo';
                  local make_list(x) = [foo, x];
                  jst.foreach(make_list, jst.env('bar'))
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("type errors") {
            {
                auto const* code = R"(
                  local foo = 'foo';
                  local make_list() = [foo];
                  jst.foreach(make_list, jst.env('bar'))
                )";
                CHECK_FALSE(Preprocess(code));
            }

            {
                auto const* code = R"(
                  local foo = 'foo';
                  local make_list(x, y) = [foo, x];
                  jst.foreach(make_list, jst.env('bar'))
                )";
                CHECK_FALSE(Preprocess(code));
            }

            {
                auto const* code = R"(
                  local foo = 'foo';
                  local make_list(x) = [foo, jst.env('x')]; // shadows runtime x
                  jst.foreach(make_list, jst.env('bar'))
                )";
                CHECK_FALSE(Preprocess(code));
            }

            {
                auto const* code = R"(
                  local foo = 'foo';
                  local make_list(x) = [foo, x];
                  jst.foreach(make_list, 'bar')
                )";
                CHECK_FALSE(Preprocess(code));
            }

            {
                auto const* code = R"(
                  local foo = 'foo';
                  jst.foreach('foo', ['bar', 'baz'])
                )";
                CHECK_FALSE(Preprocess(code));
            }
        }
    }

    SECTION("foldl() with mandatory arguments") {
        SECTION("statically evaluated") {
            {
                auto expected =
                    nlohmann::json::parse(R"(["foo", "bar", "foo", "baz"])");
                auto const* code = R"(
                  local foo = 'foo';
                  local make_list(x, a) = a + [foo, x];
                  jst.foldl(make_list, [], ['bar', 'baz'])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  ["foo", "bar", "foo", {"type": "var", "name": "baz"}])");
                auto const* code = R"(
                  local foo = 'foo';
                  local make_list(x, a) = a + [foo, x];
                  jst.foldl(make_list, [], ['bar', jst.env('baz')])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "++"
                  , "$1":
                    [ { "type": "++"
                      , "$1":
                        [ {"type": "var", "name": "foo"}
                        , ["foo", "bar"]
                        ]
                      }
                    , ["foo", "baz"]
                    ]
                  }
                )");
                auto const* code = R"(
                  local foo = 'foo';
                  local make_list(x, a) = a + [foo, x];
                  jst.foldl(make_list, jst.env('foo'), ['bar', 'baz'])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("runtime evaluated") {
            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "foldl"
                  , "var": "x"
                  , "accum_var": "a"
                  , "start": {"type": "var", "name": "foo"}
                  , "range": {"type": "var", "name": "bar"}
                  , "body":
                    { "type": "++"
                    , "$1":
                      [ {"type": "var", "name": "a"}
                      , ["foo" , {"type": "var", "name": "x"}]
                      ]
                    }
                  }
                )");
                auto const* code = R"(
                  local foo = 'foo';
                  local make_list(x, a) = a + [foo, x];
                  jst.foldl(make_list, jst.env('foo'), jst.env('bar'))
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("type errors") {
            {
                auto const* code = R"(
                  local foo = 'foo';
                  local make_list(x) = x + [foo, x];
                  jst.foldl(make_list, [], jst.env('bar'))
                )";
                CHECK_FALSE(Preprocess(code));
            }

            {
                auto const* code = R"(
                  local foo = 'foo';
                  local make_list(x, a, z) = a + [foo, x];
                  jst.foldl(make_list, [], jst.env('bar'))
                )";
                CHECK_FALSE(Preprocess(code));
            }

            {
                auto const* code = R"(
                  local foo = 'foo';
                  local make_list(x, a) = [foo, jst.env('x')]; // shadows runtime x
                  jst.foldl(make_list, [], jst.env('bar'))
                )";
                CHECK_FALSE(Preprocess(code));
            }

            {
                auto const* code = R"(
                  local foo = 'foo';
                  local make_list(x, a) = a + [foo, x];
                  jst.foldl(make_list, [], 'bar')
                )";
                CHECK_FALSE(Preprocess(code));
            }

            {
                auto const* code = R"(
                  local foo = 'foo';
                  jst.foldl(foo, [], ['bar', 'baz'])
                )";
                CHECK_FALSE(Preprocess(code));
            }
        }
    }

    SECTION("nub_right() with mandatory arguments") {
        SECTION("statically evaluated") {
            {
                auto expected = nlohmann::json::parse(R"([])");
                auto const* code = R"(jst.nub_right([]))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected =
                    nlohmann::json::parse(R"(["foo", "bar", "baz"])");
                auto const* code = R"(
                  jst.nub_right(['baz', 'bar', 'foo', 'baz', 'bar', 'baz'])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"([1.0, 2.0, 3.0])");
                auto const* code = R"(
                  jst.nub_right(list=[3, 2, 1, 3, 2, 3])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(
                    R"([{"type": "var", "name": "foo"}])");
                auto const* code = R"(
                  jst.nub_right([jst.env('foo')])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  [ { "type": "if"
                    , "cond": {"type": "var", "name": "foo"}
                    , "then": "foo"
                    , "else": "bar"
                    }
                  ]
                )");
                auto const* code = R"(
                  jst.nub_right([if jst.env('foo') then 'foo' else 'bar'])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("runtime evaluated") {
            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "nub_right"
                  , "$1": {"type": "var", "name": "foo"}
                  }
                )");
                auto const* code = R"(jst.nub_right(jst.env('foo')))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "nub_right"
                  , "$1":
                    { "type": "if"
                    , "cond": {"type": "var", "name": "foo"}
                    , "then": ["foo"]
                    , "else": ["bar"]
                    }
                  }
                )");
                auto const* code =
                    R"(jst.nub_right(if jst.env('foo') then ['foo'] else ['bar']))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "nub_right"
                  , "$1":
                    [ {"type": "var", "name": "foo"}
                    , {"type": "var", "name": "bar"}
                    ]
                  }
                )");
                auto const* code =
                    R"(jst.nub_right([jst.env('foo'), jst.env('bar')]))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "nub_right"
                  , "$1":
                    [ { "type": "if"
                      , "cond": {"type": "var", "name": "foo"}
                      , "then": "foo"
                      , "else": "bar"
                      }
                    , { "type": "if"
                      , "cond": {"type": "var", "name": "bar"}
                      , "then": "bar"
                      , "else": "foo"
                      }
                    ]
                  })");
                auto const* code = R"(
                  jst.nub_right([
                    if jst.env('foo') then "foo" else "bar",
                    if jst.env('bar') then "bar" else "foo",
                  ])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("type errors") {
            {
                auto const* code = R"(jst.nub_right('foo'))";
                CHECK_FALSE(Preprocess(code));
            }

            {
                auto const* code = R"(jst.nub_right(jst.join(['foo'])))";
                CHECK_FALSE(Preprocess(code));
            }
        }
    }

    SECTION("nub_left() with mandatory arguments") {
        SECTION("statically evaluated") {
            {
                auto expected = nlohmann::json::parse(R"([])");
                auto const* code = R"(jst.nub_left([]))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected =
                    nlohmann::json::parse(R"(["foo", "bar", "baz"])");
                auto const* code = R"(
                  jst.nub_left(['foo', 'bar', 'foo', 'baz', 'bar', 'foo'])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"([1.0, 2.0, 3.0])");
                auto const* code = R"(
                  jst.nub_left(list=[1, 2, 1, 3, 2, 1])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(
                    R"([{"type": "var", "name": "foo"}])");
                auto const* code = R"(
                  jst.nub_left([jst.env('foo')])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  [ { "type": "if"
                    , "cond": {"type": "var", "name": "foo"}
                    , "then": "foo"
                    , "else": "bar"
                    }
                  ]
                )");
                auto const* code = R"(
                  jst.nub_left([if jst.env('foo') then 'foo' else 'bar'])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("runtime evaluated") {
            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "nub_left"
                  , "$1": {"type": "var", "name": "foo"}
                  }
                )");
                auto const* code = R"(jst.nub_left(jst.env('foo')))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "nub_left"
                  , "$1":
                    { "type": "if"
                    , "cond": {"type": "var", "name": "foo"}
                    , "then": ["foo"]
                    , "else": ["bar"]
                    }
                  }
                )");
                auto const* code =
                    R"(jst.nub_left(if jst.env('foo') then ['foo'] else ['bar']))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "nub_left"
                  , "$1":
                    [ {"type": "var", "name": "foo"}
                    , {"type": "var", "name": "bar"}
                    ]
                  }
                )");
                auto const* code =
                    R"(jst.nub_left([jst.env('foo'), jst.env('bar')]))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "nub_left"
                  , "$1":
                    [ { "type": "if"
                      , "cond": {"type": "var", "name": "foo"}
                      , "then": "foo"
                      , "else": "bar"
                      }
                    , { "type": "if"
                      , "cond": {"type": "var", "name": "bar"}
                      , "then": "bar"
                      , "else": "foo"
                      }
                    ]
                  })");
                auto const* code = R"(
                  jst.nub_left([
                    if jst.env('foo') then "foo" else "bar",
                    if jst.env('bar') then "bar" else "foo",
                  ])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("type errors") {
            {
                auto const* code = R"(jst.nub_left('foo'))";
                CHECK_FALSE(Preprocess(code));
            }

            {
                auto const* code = R"(jst.nub_left(jst.join(['foo'])))";
                CHECK_FALSE(Preprocess(code));
            }
        }
    }

    SECTION("range() with mandatory arguments") {
        SECTION("statically evaluated") {
            {
                auto expected = nlohmann::json::parse(R"([])");
                auto const* code = R"(
                  jst.range(0)
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(["0", "1", "2"])");
                auto const* code = R"(
                  jst.range(size=3)
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(["0", "1", "2"])");
                auto const* code = R"(
                  jst.range('3')
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(["0", "1", "2"])");
                auto const* code = R"(
                  jst.range(-3 * -1)
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"([])");
                auto const* code = R"(
                  jst.range(-3)
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"([])");
                auto const* code = R"(
                  jst.range('-3')
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"([])");
                auto const* code = R"(
                  jst.range('foo')
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {  // wrong type: null
                auto expected = nlohmann::json::parse(R"([])");
                auto const* code = R"(
                  jst.range(null)
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {  // wrong type: bool
                auto expected = nlohmann::json::parse(R"([])");
                auto const* code = R"(
                  jst.range(true)
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {  // wrong type: list
                auto expected = nlohmann::json::parse(R"([])");
                auto const* code = R"(
                  jst.range([])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {  // wrong type: list
                auto expected = nlohmann::json::parse(R"([])");
                auto const* code = R"(
                  jst.range([jst.env('noop')])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {  // wrong type: list
                auto expected = nlohmann::json::parse(R"([])");
                auto const* code =
                    R"(jst.range(if jst.env('foo') then ['noop']))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {  // wrong type: map
                auto expected = nlohmann::json::parse(R"([])");
                auto const* code = R"(
                  jst.range({})
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {  // wrong type: map
                auto expected = nlohmann::json::parse(R"([])");
                auto const* code = R"(
                  jst.range({noop: jst.env('noop')})
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {  // wrong type: map
                auto expected = nlohmann::json::parse(R"([])");
                auto const* code =
                    R"(jst.range(if jst.env('foo') then {a: 0} else {b: 1}))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("runtime evaluated") {
            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "range"
                  , "$1": {"type": "var", "name": "foo"}
                  }
                )");
                auto const* code = R"(jst.range(jst.env('foo')))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "range"
                  , "$1":
                    { "type": "if"
                    , "cond": {"type": "var", "name": "foo"}
                    , "then": "3"
                    , "else": 3.0
                    }
                  }
                )");
                auto const* code =
                    R"(jst.range(if jst.env('foo') then '3' else 3))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }
    }

    SECTION("reverse() with mandatory arguments") {
        SECTION("statically evaluated") {
            {
                auto expected = nlohmann::json::parse(R"([])");
                auto const* code = R"(
                  jst.reverse([])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(["0", "1", "2"])");
                auto const* code = R"(
                  jst.reverse(["2", "1", "0"])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(["0", "1", "2"])");
                auto const* code = R"(
                  jst.reverse(list=["2", "1", "0"])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("runtime evaluated") {
            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "reverse"
                  , "$1": {"type": "var", "name": "foo"}
                  }
                )");
                auto const* code = R"(jst.reverse(jst.env('foo')))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "reverse"
                  , "$1":
                    { "type": "if"
                    , "cond": {"type": "var", "name": "foo"}
                    , "then": ["bar"]
                    , "else": ["baz"]
                    }
                  }
                )");
                auto const* code = R"(
                  jst.reverse(if jst.env('foo') then ['bar'] else ['baz'])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("type errors") {
            {
                auto const* code = R"(jst.reverse(null))";
                CHECK_FALSE(Preprocess(code));
            }

            {
                auto const* code = R"(
                  jst.reverse(if jst.env('foo') then 'bar' else 'baz')
                )";
                CHECK_FALSE(Preprocess(code));
            }
        }
    }

    SECTION("length() with mandatory arguments") {
        SECTION("statically evaluated") {
            {
                auto expected = nlohmann::json::parse(R"(0.0)");
                auto const* code = R"(
                  jst.length([])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(3.0)");
                auto const* code = R"(
                  jst.length(["2", "1", "0"])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(3.0)");
                auto const* code = R"(
                  jst.length(list=["2", "1", "0"])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("runtime evaluated") {
            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "length"
                  , "$1": {"type": "var", "name": "foo"}
                  }
                )");
                auto const* code = R"(jst.length(jst.env('foo')))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "length"
                  , "$1":
                    { "type": "if"
                    , "cond": {"type": "var", "name": "foo"}
                    , "then": ["bar"]
                    , "else": ["baz"]
                    }
                  }
                )");
                auto const* code = R"(
                  jst.length(if jst.env('foo') then ['bar'] else ['baz'])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("type errors") {
            {
                auto const* code = R"(jst.length(null))";
                CHECK_FALSE(Preprocess(code));
            }

            {
                auto const* code = R"(
                  jst.length(if jst.env('foo') then 'bar' else 'baz')
                )";
                CHECK_FALSE(Preprocess(code));
            }
        }
    }

    SECTION("basename() with mandatory arguments") {
        SECTION("statically evaluated") {
            {
                auto expected = nlohmann::json::parse(R"("")");
                auto const* code = R"(
                  jst.basename('')
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(".")");
                auto const* code = R"(
                  jst.basename('.')
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"("bar")");
                auto const* code = R"(
                  jst.basename('foo/../bar')
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"("bar")");
                auto const* code = R"(
                  jst.basename(path='foo/../bar')
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("runtime evaluated") {
            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "basename"
                  , "$1": {"type": "var", "name": "foo"}
                  }
                )");
                auto const* code = R"(jst.basename(jst.env('foo')))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "basename"
                  , "$1":
                    { "type": "if"
                    , "cond": {"type": "var", "name": "foo"}
                    , "then": "bar"
                    , "else": "baz"
                    }
                  }
                )");
                auto const* code = R"(
                  jst.basename(if jst.env('foo') then 'bar' else 'baz')
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("type errors") {
            {
                auto const* code = R"(jst.basename(null))";
                CHECK_FALSE(Preprocess(code));
            }

            {
                auto const* code = R"(
                  jst.basename(if jst.env('foo') then ['bar'] else ['baz'])
                )";
                CHECK_FALSE(Preprocess(code));
            }
        }
    }

    SECTION("join_cmd() with mandatory arguments") {
        SECTION("statically evaluated") {
            {
                auto expected = nlohmann::json::parse(R"("")");
                auto const* code = R"(
                  jst.join_cmd([])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"("'foo'")");
                auto const* code = R"(
                  jst.join_cmd(['foo'])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"("'foo' 'bar'")");
                auto const* code = R"(
                  jst.join_cmd(args=['foo', 'bar'])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"("'foo'\\''s' 'bar'")");
                auto const* code = R"(
                  jst.join_cmd(["foo's", 'bar'])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("runtime evaluated") {
            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "join_cmd"
                  , "$1": {"type": "var", "name": "foo"}
                  }
                )");
                auto const* code = R"(jst.join_cmd(jst.env('foo')))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "join_cmd"
                  , "$1": ["foo" , {"type": "var", "name": "bar"}]
                  }
                )");
                auto const* code = R"(
                  jst.join_cmd(['foo', jst.env('bar')])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("type errors") {
            {
                auto const* code = R"(jst.join_cmd(null))";
                CHECK_FALSE(Preprocess(code));
            }

            {
                auto const* code = R"(
                  jst.join_cmd(['foo', ['bar']])
                )";
                CHECK_FALSE(Preprocess(code));
            }
        }
    }

    SECTION("change_ending() with mandatory arguments") {
        SECTION("statically evaluated") {
            {
                auto expected = nlohmann::json::parse(R"("")");
                auto const* code = R"(
                  jst.change_ending('')
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"("foo")");
                auto const* code = R"(
                  jst.change_ending('foo.c')
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"("foo")");
                auto const* code = R"(
                  jst.change_ending('foo.c', '')
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"("foo.o")");
                auto const* code = R"(
                  jst.change_ending('foo.c', '.o')
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"("foo.o")");
                auto const* code = R"(
                  jst.change_ending('foo.c', ending='.o')
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"("foo.o")");
                auto const* code = R"(
                  jst.change_ending(path='foo.c', ending='.o')
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"("foo.o")");
                auto const* code = R"(
                  jst.change_ending(ending='.o', path='foo.c')
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("runtime evaluated") {
            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "change_ending"
                  , "$1": {"type": "var", "name": "foo"}
                  }
                )");
                auto const* code = R"(jst.change_ending(jst.env('foo')))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "change_ending"
                  , "$1": "foo.c"
                  , "ending": {"type": "var", "name": "ENDING"}
                  }
                )");
                auto const* code =
                    R"(jst.change_ending('foo.c', jst.env('ENDING')))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("type errors") {
            {
                auto const* code = R"(jst.change_ending(null))";
                CHECK_FALSE(Preprocess(code));
            }

            {
                auto const* code = R"(
                  jst.change_ending('foo.c', ending=null)
                )";
                CHECK_FALSE(Preprocess(code));
            }
        }
    }

    SECTION("escape_chars() with mandatory arguments") {
        SECTION("statically evaluated") {
            {
                auto expected = nlohmann::json::parse(R"("")");
                auto const* code = R"(
                  jst.escape_chars('')
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"("foo")");
                auto const* code = R"(
                  jst.escape_chars('foo')
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"("f\\o\\o")");
                auto const* code = R"(
                  jst.escape_chars('foo', 'o')
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"("f\\o\\x")");
                auto const* code = R"(
                  jst.escape_chars('fox', chars='ox')
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"("f\\o\\x")");
                auto const* code = R"(
                  jst.escape_chars(chars='ox', str='fox')
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"("f,o,x")");
                auto const* code = R"(
                  jst.escape_chars('fox', 'ox', prefix=',')
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"("f,o,x")");
                auto const* code = R"(
                  jst.escape_chars(prefix=',', chars='ox', str='fox')
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("runtime evaluated") {
            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "escape_chars"
                  , "$1": {"type": "var", "name": "foo"}
                  }
                )");
                auto const* code = R"(jst.escape_chars(jst.env('foo')))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "escape_chars"
                  , "$1": "foo"
                  , "chars": {"type": "var", "name": "C"}
                  }
                )");
                auto const* code = R"(jst.escape_chars('foo', jst.env('C')))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "escape_chars"
                  , "$1": "foo"
                  , "chars": "o"
                  , "escape_prefix": {"type": "var", "name": "P"}
                  }
                )");
                auto const* code =
                    R"(jst.escape_chars('foo', 'o', jst.env('P')))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("type errors") {
            {
                auto const* code = R"(jst.escape_chars(null))";
                CHECK_FALSE(Preprocess(code));
            }

            {
                auto const* code = R"(
                  jst.escape_chars('foo', chars=null)
                )";
                CHECK_FALSE(Preprocess(code));
            }

            {
                auto const* code = R"(
                  jst.escape_chars('foo', 'o', prefix=null)
                )";
                CHECK_FALSE(Preprocess(code));
            }
        }
    }

    SECTION("enumerate() with mandatory arguments") {
        SECTION("statically evaluated") {
            {
                auto expected =
                    nlohmann::json::parse(R"({"type": "empty_map"})");
                auto const* code = R"(
                  jst.enumerate([])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected =
                    nlohmann::json::parse(R"({"type": "empty_map"})");
                auto const* code = R"(
                  jst.enumerate(items=[])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "singleton_map"
                  , "key": "0000000000"
                  , "value": "foo"
                  }
                )");
                auto const* code = R"(
                  jst.enumerate(items=['foo'])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "map_union"
                  , "$1":
                    [ { "type": "singleton_map"
                      , "key": "0000000000"
                      , "value": "foo"
                      }
                    , { "type": "singleton_map"
                      , "key": "0000000001"
                      , "value": "bar"
                      }
                    ]
                  }
                )");
                auto const* code = R"(
                  jst.enumerate(items=['foo', 'bar'])
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("runtime evaluated") {
            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "enumerate"
                  , "$1": {"type": "var", "name": "foo"}
                  }
                )");
                auto const* code = R"(jst.enumerate(jst.env('foo')))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "map_union"
                  , "$1":
                    [ { "type": "singleton_map"
                      , "key": "0000000000"
                      , "value": "foo"
                      }
                    , { "type": "singleton_map"
                      , "key": "0000000001"
                      , "value": {"type": "var", "name": "bar"}
                      }
                    ]
                  }
                )");
                auto const* code = R"(jst.enumerate(['foo', jst.env('bar')]))";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("type errors") {
            {
                auto const* code = R"(jst.enumerate(null))";
                CHECK_FALSE(Preprocess(code));
            }
        }
    }

    SECTION("to_subdir() with mandatory arguments") {
        SECTION("statically evaluated") {
            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "singleton_map"
                  , "key": "foo"
                  , "value": "foo"
                  }
                )");
                auto const* code = R"(
                  jst.to_subdir({'./foo': 'foo', 'foo': 'foo'})
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "map_union"
                  , "$1":
                    [ { "type": "singleton_map"
                      , "key": "a/foo"
                      , "value": "foo"
                      }
                    , { "type": "singleton_map"
                      , "key": "b/bar"
                      , "value": "bar"
                      }
                    ]
                  }
                )");
                auto const* code = R"(
                  jst.to_subdir({'a/foo': 'foo', 'b/bar': 'bar'})
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "map_union"
                  , "$1":
                    [ { "type": "singleton_map"
                      , "key": "baz/a/foo"
                      , "value": "foo"
                      }
                    , { "type": "singleton_map"
                      , "key": "baz/b/bar"
                      , "value": "bar"
                      }
                    ]
                  }
                )");
                auto const* code = R"(
                  jst.to_subdir({'a/foo': 'foo', 'b/bar': 'bar'}, './baz')
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "map_union"
                  , "$1":
                    [ { "type": "singleton_map"
                      , "key": "baz/foo"
                      , "value": "foo"
                      }
                    , { "type": "singleton_map"
                      , "key": "baz/bar"
                      , "value": "bar"
                      }
                    ]
                  }
                )");
                auto const* code = R"(
                  jst.to_subdir(
                    {'a/foo': 'foo', 'b/bar': 'bar'}, 'baz', flat=true)
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "map_union"
                  , "$1":
                    [ { "type": "singleton_map"
                      , "key": "baz/foo"
                      , "value": "foo"
                      }
                    , { "type": "singleton_map"
                      , "key": "baz/bar"
                      , "value": "bar"
                      }
                    ]
                  }
                )");
                auto const* code = R"(
                  jst.to_subdir(
                    msg='some error',
                    flat=true,
                    subdir='baz',
                    map={'a/foo': 'foo', 'b/bar': 'bar'},
                  )
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected =
                    nlohmann::json::parse(R"({"type": "empty_map"})");
                auto const* code = R"(
                  jst.to_subdir({}, subdir=jst.env('subdir'))
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = R"(
                  jst.to_subdir({'./foo': 'foo', 'foo': 'bar'})
                )";
                CHECK_FALSE(Preprocess(code));
            }

            {
                auto const* code = R"(
                  jst.to_subdir(
                    {'a/foo': 'foo', 'b/foo': 'bar'}, 'baz', flat=['yes'])
                )";
                CHECK_FALSE(Preprocess(code));
            }
        }

        SECTION("runtime evaluated") {
            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "to_subdir"
                  , "$1": {"type": "var", "name": "map"}
                  }
                )");
                auto const* code = R"(
                  jst.to_subdir(jst.env('map'))
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "to_subdir"
                  , "$1":
                    { "type": "map_union"
                    , "$1":
                      [ { "type": "singleton_map"
                        , "key": "foo"
                        , "value": "foo"
                        }
                      , { "type": "singleton_map"
                        , "key": {"type": "var", "name": "bar"}
                        , "value": "bar"
                        }
                      ]
                    }
                  }
                )");
                auto const* code = R"(
                  jst.to_subdir({'foo': 'foo', [jst.env('bar')]: 'bar'})
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "to_subdir"
                  , "msg": "error"
                  , "$1": {"type": "var", "name": "map"}
                  }
                )");
                auto const* code = R"(
                  jst.to_subdir(jst.env('map'), msg='error')
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "to_subdir"
                  , "subdir": {"type": "var", "name": "baz"}
                  , "$1":
                    { "type": "singleton_map"
                    , "key": "foo"
                    , "value": "foo"
                    }
                  }
                )");
                auto const* code = R"(
                  jst.to_subdir({'foo': 'foo'}, jst.env('baz'))
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "to_subdir"
                  , "subdir": "baz"
                  , "flat": {"type": "var", "name": "flat"}
                  , "$1":
                    { "type": "singleton_map"
                    , "key": "foo"
                    , "value": "foo"
                    }
                  }
                )");
                auto const* code = R"(
                  jst.to_subdir({'foo': 'foo'}, 'baz', flat=jst.env('flat'))
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("type errors") {
            {
                auto const* code = R"(jst.to_subdir(''))";
                auto just_ast = Preprocess(code);
            }

            {
                auto const* code = R"(jst.to_subdir({}, subdir=[]))";
                auto just_ast = Preprocess(code);
            }

            {
                auto const* code = R"(jst.to_subdir({}, msg=[]))";
                auto just_ast = Preprocess(code);
            }
        }
    }

    SECTION("from_subdir() with mandatory arguments") {
        SECTION("statically evaluated") {
            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "singleton_map"
                  , "key": "foo"
                  , "value": "foo"
                  }
                )");
                auto const* code = R"(
                  jst.from_subdir({'./foo': 'foo', 'foo': 'foo'})
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "map_union"
                  , "$1":
                    [ { "type": "singleton_map"
                      , "key": "a/foo"
                      , "value": "foo"
                      }
                    , { "type": "singleton_map"
                      , "key": "b/bar"
                      , "value": "bar"
                      }
                    ]
                  }
                )");
                auto const* code = R"(
                  jst.from_subdir({'a/foo': 'foo', 'b/bar': 'bar'})
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "map_union"
                  , "$1":
                    [ { "type": "singleton_map"
                      , "key": "a/foo"
                      , "value": "foo"
                      }
                    , { "type": "singleton_map"
                      , "key": "b/bar"
                      , "value": "bar"
                      }
                    ]
                  }
                )");
                auto const* code = R"(
                  jst.from_subdir(
                    {'baz/a/foo': 'foo', 'baz/b/bar': 'bar'}, './baz')
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected =
                    nlohmann::json::parse(R"({"type": "empty_map"})");
                auto const* code = R"(
                  jst.from_subdir({}, subdir=jst.env('subdir'))
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto const* code = R"(
                  jst.from_subdir({'./foo': 'foo', 'foo': 'bar'})
                )";
                CHECK_FALSE(Preprocess(code));
            }

            {
                auto const* code = R"(
                  jst.from_subdir({'./a/foo': 'foo', 'a/foo': 'bar'}, 'a'))";
                CHECK_FALSE(Preprocess(code));
            }
        }

        SECTION("runtime evaluated") {
            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "from_subdir"
                  , "$1": {"type": "var", "name": "map"}
                  }
                )");
                auto const* code = R"(
                  jst.from_subdir(jst.env('map'))
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "from_subdir"
                  , "$1":
                    { "type": "map_union"
                    , "$1":
                      [ { "type": "singleton_map"
                        , "key": "foo"
                        , "value": "foo"
                        }
                      , { "type": "singleton_map"
                        , "key": {"type": "var", "name": "bar"}
                        , "value": "bar"
                        }
                      ]
                    }
                  }
                )");
                auto const* code = R"(
                  jst.from_subdir({'foo': 'foo', [jst.env('bar')]: 'bar'})
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }

            {
                auto expected = nlohmann::json::parse(R"(
                  { "type": "from_subdir"
                  , "subdir": {"type": "var", "name": "baz"}
                  , "$1":
                    { "type": "singleton_map"
                    , "key": "foo"
                    , "value": "foo"
                    }
                  }
                )");
                auto const* code = R"(
                  jst.from_subdir({'foo': 'foo'}, jst.env('baz'))
                )";
                auto just_ast = Preprocess(code);
                CHECK(ToJson(just_ast) == expected);
            }
        }

        SECTION("type errors") {
            {
                auto const* code = R"(jst.from_subdir(''))";
                auto just_ast = Preprocess(code);
            }

            {
                auto const* code = R"(jst.from_subdir({}, subdir=[]))";
                auto just_ast = Preprocess(code);
            }
        }
    }
}

TEST_CASE("preprocessor", "[arithmetics]") {
    SECTION("multiply") {
        SECTION("simple") {
            auto expected = nlohmann::json::parse(R"(
                { "key": "foo"
                , "type": "singleton_map"
                , "value": 25.0
                })");
            auto const* code = R"(
                {
                    foo: 5 * 5
                }
            )";
            auto just_ast = Preprocess(code);
            CHECK(ToJson(just_ast) == expected);
        }

        SECTION("variable") {
            auto expected = nlohmann::json::parse(R"(
                { "key": "foo"
                , "type": "singleton_map"
                , "value": 
                    { "type": "*"
                    , "$1": [ 5.0, { "type": "var", "name": "MULTIPLIER" } ]
                    }
                })");
            auto const* code = R"(
                local var = jst.env("MULTIPLIER");
                {
                    foo: 5 * var
                }
            )";
            auto just_ast = Preprocess(code);
            CHECK(ToJson(just_ast) == expected);
        }
    }
    SECTION("unary minus") {
        SECTION("simple") {
            auto expected = nlohmann::json::parse(R"(
                { "key": "foo"
                , "type": "singleton_map"
                , "value": -3.0
                })");
            auto const* code = "{ foo: -3 }";
            auto just_ast = Preprocess(code);
            CHECK(ToJson(just_ast) == expected);
        }

        SECTION("plus positive") {
            auto expected = nlohmann::json::parse(R"(
                { "key": "foo"
                , "type": "singleton_map"
                , "value": 2.0
                })");
            auto const* code = "{ foo: -3 + 5 }";
            auto just_ast = Preprocess(code);
            CHECK(ToJson(just_ast) == expected);
        }

        SECTION("variable") {
            auto expected = nlohmann::json::parse(R"(
                { "key": "foo"
                , "type": "singleton_map"
                , "value":
                    { "type": "*"
                    , "$1": [ { "name": "MINUS", "type": "var" }, -1.0 ]
                    }
                })");
            auto const* code = R"(
            local var = jst.env("MINUS");
            { 
                foo: -var
            })";
            auto just_ast = Preprocess(code);
            CHECK(ToJson(just_ast) == expected);
        }
    }

    SECTION("binary minus") {
        SECTION("simple") {
            auto expected = nlohmann::json::parse(R"(
                { "key": "foo"
                , "type": "singleton_map"
                , "value": 2.0
                })");
            auto const* code = "{ foo: 5 - 3 }";
            auto just_ast = Preprocess(code);
            CHECK(ToJson(just_ast) == expected);
        }

        SECTION("negative minus negative") {
            auto expected = nlohmann::json::parse(R"(
                { "key": "foo"
                , "type": "singleton_map"
                , "value": -8.0
                })");
            auto const* code = "{ foo: -5 - 3 }";
            auto just_ast = Preprocess(code);
            CHECK(ToJson(just_ast) == expected);
        }
        SECTION("variable") {
            auto expected = nlohmann::json::parse(R"(
                { "key": "foo"
                , "type": "singleton_map"
                , "value":
                    { "type": "+"
                    , "$1":
                        [ 5.0
                        , { "type": "*"
                          , "$1": [ { "name": "MINUS",  "type": "var" }, -1.0 ]
                          }
                        ]
                    }
                })");
            auto const* code = R"(
            local var = jst.env("MINUS");
            { 
                foo: 5 - var
            })";
            auto just_ast = Preprocess(code);
            CHECK(ToJson(just_ast) == expected);
        }
    }
}

TEST_CASE("preprocessor", "[booleans]") {
    SECTION("==") {
        auto expected = nlohmann::json::parse(R"(
                {
                    "key": "foo",
                    "type": "singleton_map",
                    "value": 3.0
                })");
        auto const* code = R"(
                local f = 'foo';
                local b = 'bar';
                {
                    foo: if f == b then 5 else 3
                }
            )";
        auto just_ast = Preprocess(code);
        CHECK(ToJson(just_ast) == expected);
    }

    SECTION("!=") {
        auto expected = nlohmann::json::parse(R"(
                { "key": "foo"
                , "type": "singleton_map"
                , "value": 5.0
                })");
        auto const* code = R"(
                local f = 'foo';
                local b = 'bar';
                {
                    foo: if f != b then 5 else 3
                }
            )";
        auto just_ast = Preprocess(code);
        CHECK(ToJson(just_ast) == expected);
    }
}

TEST_CASE("preprocessor", "[file imports]") {
    auto expected = nlohmann::json::parse(R"("foo")");

    SECTION("relative") {
        SECTION("same directory") {
            auto const foo =
                justlang::FileLocation{.repo = "", .path = "foo/foo.jst"};
            auto const bar =
                justlang::FileLocation{.repo = "", .path = "foo/bar.jst"};
            auto const baz =
                justlang::FileLocation{.repo = "", .path = "foo/baz.jst"};
            auto files = file_map_t{};
            files[foo.ToString()] = R"(
              local bar = import 'bar.jst';
              bar
            )";
            files[bar.ToString()] = R"(
              local baz = import 'baz.jst';
              baz
            )";
            files[baz.ToString()] = R"(
              'foo'
            )";
            auto just_ast = Preprocess(foo.repo, foo.path, files);
            REQUIRE(just_ast != nullptr);
            CHECK(ToJson(just_ast) == expected);
        }

        SECTION("subdirectory") {
            auto const foo =
                justlang::FileLocation{.repo = "", .path = "foo/foo.jst"};
            auto const bar =
                justlang::FileLocation{.repo = "", .path = "foo/bar.jst"};
            auto const baz =
                justlang::FileLocation{.repo = "", .path = "foo/baz/baz.jst"};
            auto files = file_map_t{};
            files[foo.ToString()] = R"(
              local bar = import 'bar.jst';
              bar
            )";
            files[bar.ToString()] = R"(
              local baz = import 'baz/baz.jst';
              baz
            )";
            files[baz.ToString()] = R"(
              'foo'
            )";
            auto just_ast = Preprocess(foo.repo, foo.path, files);
            REQUIRE(just_ast != nullptr);
            CHECK(ToJson(just_ast) == expected);
        }

        SECTION("parent directory") {
            auto const foo =
                justlang::FileLocation{.repo = "", .path = "foo/foo.jst"};
            auto const bar =
                justlang::FileLocation{.repo = "", .path = "foo/bar.jst"};
            auto const baz =
                justlang::FileLocation{.repo = "", .path = "baz/baz.jst"};
            auto files = file_map_t{};
            files[foo.ToString()] = R"(
              local bar = import 'bar.jst';
              bar
            )";
            files[bar.ToString()] = R"(
              local baz = import '../baz/baz.jst';
              baz
            )";
            files[baz.ToString()] = R"(
              'foo'
            )";
            auto just_ast = Preprocess(foo.repo, foo.path, files);
            REQUIRE(just_ast != nullptr);
            CHECK(ToJson(just_ast) == expected);
        }
    }

    SECTION("absolute") {
        auto const foo =
            justlang::FileLocation{.repo = "", .path = "foo/foo.jst"};
        auto const bar =
            justlang::FileLocation{.repo = "", .path = "foo/bar.jst"};
        auto const baz =
            justlang::FileLocation{.repo = "", .path = "baz/baz.jst"};
        auto files = file_map_t{};
        files[foo.ToString()] = R"(
          local bar = import 'bar.jst';
          bar
        )";
        files[bar.ToString()] = R"(
          local baz = import '/baz/baz.jst';
          baz
        )";
        files[baz.ToString()] = R"(
          'foo'
        )";
        auto just_ast = Preprocess(foo.repo, foo.path, files);
        REQUIRE(just_ast != nullptr);
        CHECK(ToJson(just_ast) == expected);
    }

#ifdef SUPPORT_IMPORT_VIA_EXT_REF
    SECTION("external") {
        auto const foo =
            justlang::FileLocation{.repo = "", .path = "foo/foo.jst"};
        auto const bar =
            justlang::FileLocation{.repo = "bar", .path = "bar/bar.jst"};
        auto const baz =
            justlang::FileLocation{.repo = "baz", .path = "baz/baz.jst"};
        auto files = file_map_t{};
        files[foo.ToString()] = R"(
          local bar = import @'bar//bar/bar.jst';
          bar
        )";
        files[bar.ToString()] = R"(
          local baz = import @'baz//baz//baz.jst';
          baz
        )";
        files[baz.ToString()] = R"(
          'foo'
        )";
        auto just_ast = Preprocess(foo.repo, foo.path, files);
        REQUIRE(just_ast != nullptr);
        CHECK(ToJson(just_ast) == expected);
    }
#endif

    SECTION("cycle detection") {
        SECTION("cycle via self import") {
            auto const foo =
                justlang::FileLocation{.repo = "", .path = "foo.jst"};
            auto files = file_map_t{};
            files[foo.ToString()] = R"(
              local foo = import 'foo.jst';
              foo
            )";
            CHECK(not Preprocess(foo.repo, foo.path, files));
        }

        SECTION("cycle via local file's imports") {
            auto const foo =
                justlang::FileLocation{.repo = "", .path = "foo.jst"};
            auto const bar =
                justlang::FileLocation{.repo = "", .path = "bar.jst"};
            auto const baz =
                justlang::FileLocation{.repo = "", .path = "baz.jst"};
            auto files = file_map_t{};
            files[foo.ToString()] = R"(
              local bar = import 'bar.jst';
              bar
            )";
            files[bar.ToString()] = R"(
              local baz = import 'baz.jst';
              baz
            )";
            files[baz.ToString()] = R"(
              local foo = import 'foo.jst';
              foo
            )";
            CHECK(not Preprocess(foo.repo, foo.path, files));
        }

#ifdef SUPPORT_IMPORT_VIA_EXT_REF
        SECTION("cycle via external file's imports") {
            auto const foo =
                justlang::FileLocation{.repo = "", .path = "foo.jst"};
            auto const bar =
                justlang::FileLocation{.repo = "bar", .path = "bar.jst"};
            auto const baz =
                justlang::FileLocation{.repo = "baz", .path = "baz.jst"};
            auto files = file_map_t{};
            files[foo.ToString()] = R"(
              local bar = import @'bar//bar.jst';
              bar
            )";
            files[bar.ToString()] = R"(
              local baz = import @'baz//baz.jst';
              baz
            )";
            files[baz.ToString()] = R"(
              local foo = import @'""//foo.jst';
              foo
            )";
            CHECK(not Preprocess(foo.repo, foo.path, files));
        }
#endif
    }
}
