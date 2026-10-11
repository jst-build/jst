// Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
// Copyright 2026 The jst-build authors.
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

#include "src/buildtool/crypto/hasher.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <utility>  // std::move

#include "gsl/gsl"
#include "openssl/evp.h"
#include "openssl/opensslv.h"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

// EVP_MD_CTX is the algorithm-agnostic digest context provided by both
// OpenSSL and BoringSSL. It replaces the previous per-algorithm
// SHA_CTX/SHA256_CTX/SHA512_CTX types and their SHA1_*/SHA256_*/SHA512_*
// functions, which OpenSSL (as of 3.0) marks deprecated in favor of this
// same EVP_MD_CTX API; BoringSSL implements it identically. Since
// EVP_MD_CTX can't be forward-declared without an OpenSSL header, it is
// hidden behind this opaque struct so that hasher.hpp stays free of any
// OpenSSL/BoringSSL includes.
struct Hasher::ShaContext final {
    struct Deleter final {
        void operator()(EVP_MD_CTX* ctx) const noexcept {
            EVP_MD_CTX_free(ctx);
        }
    };
    std::unique_ptr<EVP_MD_CTX, Deleter> ctx;
};

namespace {
inline constexpr int kOpenSslTrue = 1;

#if not defined(OPENSSL_IS_BORINGSSL) and OPENSSL_VERSION_NUMBER >= 0x30000000L
#define JST_HAVE_OPENSSL3
// With OpenSSL 3, initializing a digest from EVP_sha256() and alike fetches
// the implementation implicitly, which takes a global lock and references the
// shared digest object; threads hashing concurrently contend for both. So
// fetch each digest once per thread instead.
struct MdFetcher final {
    EVP_MD* data;
    explicit MdFetcher(char const* name) noexcept
        : data{EVP_MD_fetch(/*ctx=*/nullptr, name, /*properties=*/nullptr)} {}
    MdFetcher(MdFetcher const&) = delete;
    MdFetcher(MdFetcher&&) = delete;
    auto operator=(MdFetcher const&) -> MdFetcher& = delete;
    auto operator=(MdFetcher&&) -> MdFetcher& = delete;
    ~MdFetcher() noexcept { EVP_MD_free(data); }
};
#endif

[[nodiscard]] auto ToEvpMd(Hasher::HashType type) noexcept -> EVP_MD const* {
    switch (type) {
        case Hasher::HashType::SHA1:
#ifdef JST_HAVE_OPENSSL3
            thread_local auto const kMdSha1 = MdFetcher{"SHA1"};
            if (kMdSha1.data != nullptr) {
                return kMdSha1.data;
            }
#endif
            return EVP_sha1();
        case Hasher::HashType::SHA256:
#ifdef JST_HAVE_OPENSSL3
            thread_local auto const kMdSha256 = MdFetcher{"SHA256"};
            if (kMdSha256.data != nullptr) {
                return kMdSha256.data;
            }
#endif
            return EVP_sha256();
        case Hasher::HashType::SHA512:
#ifdef JST_HAVE_OPENSSL3
            thread_local auto const kMdSha512 = MdFetcher{"SHA512"};
            if (kMdSha512.data != nullptr) {
                return kMdSha512.data;
            }
#endif
            return EVP_sha512();
    }
    return nullptr;  // make gcc happy
}
}  // namespace

Hasher::Hasher(std::unique_ptr<ShaContext> sha_ctx) noexcept
    : sha_ctx_{std::move(sha_ctx)} {}

// Explicitly declared and then defaulted dtor and move ctor/operator are needed
// to compile std::unique_ptr of an incomplete type.
Hasher::Hasher(Hasher&& other) noexcept = default;
auto Hasher::operator=(Hasher&& other) noexcept -> Hasher& = default;
Hasher::~Hasher() noexcept = default;

auto Hasher::Create(HashType type) noexcept -> std::optional<Hasher> {
    auto const* md = ToEvpMd(type);
    if (md == nullptr) {
        return std::nullopt;
    }
    auto ctx =
        std::unique_ptr<EVP_MD_CTX, ShaContext::Deleter>{EVP_MD_CTX_new()};
    if (ctx == nullptr) {
        Logger::Log(LogLevel::Error, "Failed to allocate EVP_MD_CTX.");
        return std::nullopt;
    }
    if (EVP_DigestInit_ex(ctx.get(), md, /*impl=*/nullptr) != kOpenSslTrue) {
        Logger::Log(LogLevel::Error, "HashFunction::Initialize failed.");
        return std::nullopt;
    }
    return std::optional<Hasher>{
        Hasher{std::make_unique<ShaContext>(ShaContext{std::move(ctx)})}};
}

auto Hasher::Update(std::string const& data) noexcept -> bool {
    return EVP_DigestUpdate(sha_ctx_->ctx.get(), data.data(), data.size()) ==
           kOpenSslTrue;
}

auto Hasher::Finalize() && noexcept -> HashDigest {
    auto out = std::array<std::uint8_t, EVP_MAX_MD_SIZE>{};
    unsigned int out_len = 0;
    if (EVP_DigestFinal_ex(sha_ctx_->ctx.get(), out.data(), &out_len) ==
        kOpenSslTrue) {
        auto const length = static_cast<std::size_t>(out_len);
        return HashDigest{std::string{out.data(), out.data() + length}};
    }
    Logger::Log(LogLevel::Error, "Failed to compute hash.");
    Ensures(false);
}

auto Hasher::GetHashLength() const noexcept -> std::size_t {
    // The hexadecimal string representation uses two characters per byte.
    static constexpr std::size_t kCharsPerByte = 2;
    return kCharsPerByte *
           gsl::narrow_cast<std::size_t>(EVP_MD_CTX_size(sha_ctx_->ctx.get()));
}
