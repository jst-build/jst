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

#include "src/buildtool/jstlang/ref.hpp"

#include <catch2/catch_test_macros.hpp>

using jstlang::DecodeRefString;
using jstlang::EncodeRefData;
using jstlang::RefContext;
using jstlang::RefData;
using jstlang::RefType;

TEST_CASE("ref", "[decode local]") {
    {
        auto ref = DecodeRefString(R"(:"")");
        CHECK(ref.type == RefType::Local);
        CHECK(ref.target == "");
    }

    {
        auto ref = DecodeRefString(R"(:foo)");
        CHECK(ref.type == RefType::Local);
        CHECK(ref.target == "foo");
    }

    {
        auto ref = DecodeRefString(R"(:"foo:bar")");
        CHECK(ref.type == RefType::Local);
        CHECK(ref.target == "foo:bar");
    }

    {
        auto ref = DecodeRefString(R"(:"foo\n\"bar")");
        CHECK(ref.type == RefType::Local);
        CHECK(ref.target == "foo\n\"bar");
    }
}

TEST_CASE("ref", "[decode absolute]") {
    {
        auto ref = DecodeRefString(R"(//:"")");
        CHECK(ref.type == RefType::Abs);
        CHECK(ref.module.empty());
        CHECK(ref.target == "");
    }

    {
        auto ref = DecodeRefString(R"(//foo:"")");
        CHECK(ref.type == RefType::Abs);
        CHECK(ref.module == "foo");
        CHECK(ref.target == "");
    }

    {
        auto ref = DecodeRefString(R"(//foo:bar)");
        CHECK(ref.type == RefType::Abs);
        CHECK(ref.module == "foo");
        CHECK(ref.target == "bar");
    }

    {
        auto ref = DecodeRefString(R"(//foo/bar)");
        CHECK(ref.type == RefType::Abs);
        CHECK(ref.module == "foo/bar");
        CHECK(ref.target == "bar");
    }

    {
        auto ref = DecodeRefString(R"(//"foo:bar")");
        CHECK(ref.type == RefType::Abs);
        CHECK(ref.module == "foo:bar");
        CHECK(ref.target == "foo:bar");
    }

    {
        auto ref = DecodeRefString(R"(//"foo\n\"bar":"")");
        CHECK(ref.type == RefType::Abs);
        CHECK(ref.module == "foo\n\"bar");
        CHECK(ref.target == "");
    }

    {
        auto ref = DecodeRefString(R"(//"foo\n\"bar")");
        CHECK(ref.type == RefType::Abs);
        CHECK(ref.module == "foo\n\"bar");
        CHECK(ref.target == "foo\n\"bar");
    }
}

TEST_CASE("ref", "[decode external]") {
    {
        auto ref = DecodeRefString(R"(""//:"")");
        CHECK(ref.type == RefType::Ext);
        CHECK(ref.repo == "");
        CHECK(ref.module.empty());
        CHECK(ref.target == "");
    }

    {
        auto ref = DecodeRefString(R"(foo//:"")");
        CHECK(ref.type == RefType::Ext);
        CHECK(ref.repo == "foo");
        CHECK(ref.module.empty());
        CHECK(ref.target == "");
    }

    {
        auto ref = DecodeRefString(R"(foo//bar:baz)");
        CHECK(ref.type == RefType::Ext);
        CHECK(ref.repo == "foo");
        CHECK(ref.module == "bar");
        CHECK(ref.target == "baz");
    }

    {
        auto ref = DecodeRefString(R"("foo//bar:baz"//:"")");
        CHECK(ref.type == RefType::Ext);
        CHECK(ref.repo == "foo//bar:baz");
        CHECK(ref.module.empty());
        CHECK(ref.target == "");
    }

    {
        auto ref = DecodeRefString(R"("foo\nbar\"baz"//:"")");
        CHECK(ref.type == RefType::Ext);
        CHECK(ref.repo == "foo\nbar\"baz");
        CHECK(ref.module.empty());
        CHECK(ref.target == "");
    }
}

TEST_CASE("ref", "[decode relative]") {
    {
        auto ref = DecodeRefString(R"(./foo:"")");
        CHECK(ref.type == RefType::Rel);
        CHECK(ref.module == "foo");
        CHECK(ref.target == "");
    }

    {
        auto ref = DecodeRefString(R"(./foo:bar)");
        CHECK(ref.type == RefType::Rel);
        CHECK(ref.module == "foo");
        CHECK(ref.target == "bar");
    }

    {
        auto ref = DecodeRefString(R"(./foo/bar)");
        CHECK(ref.type == RefType::Rel);
        CHECK(ref.module == "foo/bar");
        CHECK(ref.target == "bar");
    }

    {
        auto ref = DecodeRefString(R"(./"foo/bar/../baz":"")");
        CHECK(ref.type == RefType::Rel);
        CHECK(ref.module == "foo/baz");
        CHECK(ref.target == "");
    }

    {
        auto ref = DecodeRefString(R"(./"foo/bar/../baz")");
        CHECK(ref.type == RefType::Rel);
        CHECK(ref.module == "foo/baz");
        CHECK(ref.target == "baz");
    }

    {
        auto ref = DecodeRefString(R"(./"foo\n\"bar":"")");
        CHECK(ref.type == RefType::Rel);
        CHECK(ref.module == "foo\n\"bar");
        CHECK(ref.target == "");
    }

    {
        auto ref = DecodeRefString(R"(./"foo\n\"bar")");
        CHECK(ref.type == RefType::Rel);
        CHECK(ref.module == "foo\n\"bar");
        CHECK(ref.target == "foo\n\"bar");
    }
}

TEST_CASE("ref", "[decode default target]") {
    // an empty target segment is only accepted when explicitly allowed
    for (auto const& ref_str : {R"(:)",
                                R"(//foo:)",
                                R"(//:)",
                                R"(./foo:)",
                                R"(repo//foo:)",
                                R"(repo//:)"}) {
        CHECK_THROWS(DecodeRefString(ref_str));
        CHECK_NOTHROW(DecodeRefString(ref_str, RefContext::CLI));
    }

    {
        auto ref = DecodeRefString(R"(:)", RefContext::CLI);
        CHECK(ref.type == RefType::Local);
        CHECK_FALSE(ref.target.has_value());
    }

    {
        auto ref = DecodeRefString(R"(//foo/bar:)", RefContext::CLI);
        CHECK(ref.type == RefType::Abs);
        CHECK(ref.module == "foo/bar");
        CHECK_FALSE(ref.target.has_value());
    }

    {
        auto ref = DecodeRefString(R"(//:)", RefContext::CLI);
        CHECK(ref.type == RefType::Abs);
        CHECK(ref.module.empty());
        CHECK_FALSE(ref.target.has_value());
    }

    {
        auto ref = DecodeRefString(R"(./sub:)", RefContext::CLI);
        CHECK(ref.type == RefType::Rel);
        CHECK(ref.module == "sub");
        CHECK_FALSE(ref.target.has_value());
    }

    {
        auto ref = DecodeRefString(R"(repo//tests:)", RefContext::CLI);
        CHECK(ref.type == RefType::Ext);
        CHECK(ref.repo == "repo");
        CHECK(ref.module == "tests");
        CHECK_FALSE(ref.target.has_value());
    }

    {
        auto ref = DecodeRefString(R"(repo//:)", RefContext::CLI);
        CHECK(ref.type == RefType::Ext);
        CHECK(ref.repo == "repo");
        CHECK(ref.module.empty());
        CHECK_FALSE(ref.target.has_value());
    }

    {
        auto ref = DecodeRefString(R"("weird//repo"//:)", RefContext::CLI);
        CHECK(ref.type == RefType::Ext);
        CHECK(ref.repo == "weird//repo");
        CHECK(ref.module.empty());
        CHECK_FALSE(ref.target.has_value());
    }

    // a named target is never the default one, also the same-name shorthand
    {
        auto ref = DecodeRefString(R"(//foo/bar)", RefContext::CLI);
        CHECK(ref.target == "bar");
        CHECK(ref.target.has_value());
    }

    {
        auto ref = DecodeRefString(R"(//foo:"")", RefContext::CLI);
        CHECK(ref.target == "");
        CHECK(ref.target.has_value());
    }

    // '//' and 'repo//' without a target stay errors
    CHECK_THROWS(DecodeRefString(R"(//)", RefContext::CLI));
    CHECK_THROWS(DecodeRefString(R"(repo//)", RefContext::CLI));
}

TEST_CASE("ref", "[encode local]") {
    {
        auto ref = RefData{
            .type = RefType::Local, .repo = "", .module = "", .target = ""};
        CHECK(EncodeRefData(ref) == ":\"\"");
    }

    {
        auto ref = RefData{.type = RefType::Local,
                           .repo = "baz",
                           .module = "bar",
                           .target = "foo"};
        CHECK(EncodeRefData(ref) == ":foo");
    }

    {
        auto ref = RefData{.type = RefType::Local,
                           .repo = "baz qux",
                           .module = "bar baz",
                           .target = "foo bar"};
        CHECK(EncodeRefData(ref) == ":foo bar");
    }

    {
        auto ref = RefData{.type = RefType::Local,
                           .repo = "baz//qux",
                           .module = "bar//baz",
                           .target = "foo//bar"};
        CHECK(EncodeRefData(ref) == ":foo//bar");
    }

    {
        auto ref = RefData{.type = RefType::Local,
                           .repo = "baz./qux",
                           .module = "bar./baz",
                           .target = "foo./bar"};
        CHECK(EncodeRefData(ref) == ":foo./bar");
    }

    {
        auto ref = RefData{.type = RefType::Local,
                           .repo = "baz:qux",
                           .module = "bar:baz",
                           .target = "foo:bar"};
        CHECK(EncodeRefData(ref) == ":foo:bar");
    }

    {
        auto ref = RefData{.type = RefType::Local,
                           .repo = "baz\nqux",
                           .module = "bar\nbaz",
                           .target = "foo\nbar"};
        CHECK(EncodeRefData(ref) == ":\"foo\\nbar\"");
    }
}

TEST_CASE("ref", "[encode absolute]") {
    {
        auto ref = RefData{
            .type = RefType::Abs, .repo = "", .module = "", .target = ""};
        CHECK(EncodeRefData(ref) == "//:\"\"");
    }

    {
        auto ref = RefData{.type = RefType::Abs,
                           .repo = "baz",
                           .module = "bar",
                           .target = "foo"};
        CHECK(EncodeRefData(ref) == "//bar:foo");
    }

    {
        auto ref = RefData{.type = RefType::Abs,
                           .repo = "baz qux",
                           .module = "bar baz",
                           .target = "foo bar"};
        CHECK(EncodeRefData(ref) == "//bar baz:foo bar");
    }

    {
        auto ref = RefData{.type = RefType::Abs,
                           .repo = "baz//qux",
                           .module = "bar//baz",
                           .target = "foo//bar"};
        CHECK(EncodeRefData(ref) == "//bar//baz:foo//bar");
    }

    {
        auto ref = RefData{.type = RefType::Abs,
                           .repo = "baz./qux",
                           .module = "bar./baz",
                           .target = "foo./bar"};
        CHECK(EncodeRefData(ref) == "//bar./baz:foo./bar");
    }

    {
        auto ref = RefData{.type = RefType::Abs,
                           .repo = "baz:qux",
                           .module = "bar:baz",
                           .target = "foo:bar"};
        CHECK(EncodeRefData(ref) == "//\"bar:baz\":foo:bar");
    }

    {
        auto ref = RefData{.type = RefType::Abs,
                           .repo = "baz\nqux",
                           .module = "bar\nbaz",
                           .target = "foo\nbar"};
        CHECK(EncodeRefData(ref) == "//\"bar\\nbaz\":\"foo\\nbar\"");
    }
}

TEST_CASE("ref", "[encode external]") {
    {
        auto ref = RefData{
            .type = RefType::Ext, .repo = "", .module = "", .target = ""};
        CHECK(EncodeRefData(ref) == "\"\"//:\"\"");
    }

    {
        auto ref = RefData{.type = RefType::Ext,
                           .repo = "baz",
                           .module = "bar",
                           .target = "foo"};
        CHECK(EncodeRefData(ref) == "baz//bar:foo");
    }

    {
        auto ref = RefData{.type = RefType::Ext,
                           .repo = "baz qux",
                           .module = "bar baz",
                           .target = "foo bar"};
        CHECK(EncodeRefData(ref) == "baz qux//bar baz:foo bar");
    }

    {
        auto ref = RefData{.type = RefType::Ext,
                           .repo = "baz//qux",
                           .module = "bar//baz",
                           .target = "foo//bar"};
        CHECK(EncodeRefData(ref) == "\"baz//qux\"//bar//baz:foo//bar");
    }

    {
        auto ref = RefData{.type = RefType::Ext,
                           .repo = "baz./qux",
                           .module = "bar./baz",
                           .target = "foo./bar"};
        CHECK(EncodeRefData(ref) == "baz./qux//bar./baz:foo./bar");
    }

    {
        auto ref = RefData{.type = RefType::Ext,
                           .repo = "baz:qux",
                           .module = "bar:baz",
                           .target = "foo:bar"};
        CHECK(EncodeRefData(ref) == "baz:qux//\"bar:baz\":foo:bar");
    }

    {
        auto ref = RefData{.type = RefType::Ext,
                           .repo = "baz\nqux",
                           .module = "bar\nbaz",
                           .target = "foo\nbar"};
        CHECK(EncodeRefData(ref) ==
              "\"baz\\nqux\"//\"bar\\nbaz\":\"foo\\nbar\"");
    }
}

TEST_CASE("ref", "[encode relative]") {
    {
        auto ref = RefData{
            .type = RefType::Rel, .repo = "", .module = "", .target = ""};
        CHECK(EncodeRefData(ref) == "./:\"\"");
    }

    {
        auto ref = RefData{.type = RefType::Rel,
                           .repo = "baz",
                           .module = "bar",
                           .target = "foo"};
        CHECK(EncodeRefData(ref) == "./bar:foo");
    }

    {
        auto ref = RefData{.type = RefType::Rel,
                           .repo = "baz qux",
                           .module = "bar baz",
                           .target = "foo bar"};
        CHECK(EncodeRefData(ref) == "./bar baz:foo bar");
    }

    {
        auto ref = RefData{.type = RefType::Rel,
                           .repo = "baz//qux",
                           .module = "bar//baz",
                           .target = "foo//bar"};
        CHECK(EncodeRefData(ref) == "./bar//baz:foo//bar");
    }

    {
        auto ref = RefData{.type = RefType::Rel,
                           .repo = "baz./qux",
                           .module = "bar./baz",
                           .target = "foo./bar"};
        CHECK(EncodeRefData(ref) == "./bar./baz:foo./bar");
    }

    {
        auto ref = RefData{.type = RefType::Rel,
                           .repo = "baz:qux",
                           .module = "bar:baz",
                           .target = "foo:bar"};
        CHECK(EncodeRefData(ref) == "./\"bar:baz\":foo:bar");
    }

    {
        auto ref = RefData{.type = RefType::Rel,
                           .repo = "baz\nqux",
                           .module = "bar\nbaz",
                           .target = "foo\nbar"};
        CHECK(EncodeRefData(ref) == "./\"bar\\nbaz\":\"foo\\nbar\"");
    }
}
