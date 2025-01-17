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

#include "justlang/ref.hpp"

#include <catch2/catch_test_macros.hpp>

using justlang::DecodeRefString;
using justlang::EncodeRefData;
using justlang::RefData;
using justlang::RefType;

TEST_CASE("ref", "[decode local]") {
    {
        auto ref = DecodeRefString(R"(:"")");
        CHECK(ref.type == RefType::Local);
        CHECK(ref.target.empty());
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
        CHECK(ref.target.empty());
    }

    {
        auto ref = DecodeRefString(R"(//foo:"")");
        CHECK(ref.type == RefType::Abs);
        CHECK(ref.module == "foo");
        CHECK(ref.target.empty());
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
        CHECK(ref.target.empty());
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
        CHECK(ref.repo.empty());
        CHECK(ref.module.empty());
        CHECK(ref.target.empty());
    }

    {
        auto ref = DecodeRefString(R"(foo//:"")");
        CHECK(ref.type == RefType::Ext);
        CHECK(ref.repo == "foo");
        CHECK(ref.module.empty());
        CHECK(ref.target.empty());
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
        CHECK(ref.target.empty());
    }

    {
        auto ref = DecodeRefString(R"("foo\nbar\"baz"//:"")");
        CHECK(ref.type == RefType::Ext);
        CHECK(ref.repo == "foo\nbar\"baz");
        CHECK(ref.module.empty());
        CHECK(ref.target.empty());
    }
}

TEST_CASE("ref", "[decode relative]") {
    {
        auto ref = DecodeRefString(R"(./foo:"")");
        CHECK(ref.type == RefType::Rel);
        CHECK(ref.module == "foo");
        CHECK(ref.target.empty());
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
        CHECK(ref.target.empty());
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
        CHECK(ref.target.empty());
    }

    {
        auto ref = DecodeRefString(R"(./"foo\n\"bar")");
        CHECK(ref.type == RefType::Rel);
        CHECK(ref.module == "foo\n\"bar");
        CHECK(ref.target == "foo\n\"bar");
    }
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
