#include <catch2/catch_test_macros.hpp>
#include <string>
#include <string_view>

#include "cemkit/core/sha256.hpp"

using cemkit::core::sha256_hex;

// Expected digests: the FIPS 180-2 Appendix B examples ("abc", the 448-bit message, one million
// 'a'), the empty message, and messages that end at the padding boundaries (55, 56 and 64 bytes);
// all cross-checked with GNU coreutils sha256sum and Python hashlib.
TEST_CASE("sha256 matches the FIPS 180-2 examples", "[GEO-004]") {
  CHECK(sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  CHECK(sha256_hex(std::string(1'000'000, 'a')) ==
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("sha256 handles the empty message and padding boundaries", "[GEO-004]") {
  CHECK(sha256_hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(sha256_hex(std::string(55, 'a')) ==
        "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
  CHECK(sha256_hex(std::string(56, 'a')) ==
        "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
  CHECK(sha256_hex(std::string(64, 'a')) ==
        "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
}

TEST_CASE("sha256 treats bytes as unsigned and keeps embedded zeros", "[GEO-004]") {
  const std::string with_zero{"a\0b", 3};
  const std::string high_bytes{"\xff\x80", 2};
  // Expected values from Python hashlib.
  CHECK(sha256_hex(with_zero) ==
        "59b271ae1bbcb1d31d41929817f4b16fb439eb4f31520b5ad1d5ce98920a7138");
  CHECK(sha256_hex(high_bytes) ==
        "85c61621ebd04403f66d96fe300cf10b3844de7358184f1276cb08790fd135f1");
}
