// Tests for GpBinaryReader — the byte-stream extractor used by every
// Guitar Pro 3/4/5 binary parser.
//
// GpBinaryReader is pure STL (std::vector<uint8_t> + std::string) — it has
// no Qt, no MidiFile, no project-internal deps at all. The test compiles
// only that single .cpp and links nothing else from the project.
//
// Coverage focus per the roadmap philosophy ("99% normal use plus high-value
// edge cases"):
//   * little-endian decoding for short / int / float / double — these are
//     load-bearing for every GP parser; flipping endianness would silently
//     corrupt every GP file the user opens.
//   * signed-byte boundary (0x80 → -128) — the overflow cusp.
//   * checkBounds throws on reads past the end — the canonical safety net.
//   * the four flavours of length-prefixed string (size-only, byte-prefix,
//     int-prefix, "int = byteLen + 1" GP convention) — these have caught
//     real GP regressions in the past.
//   * pointer round-trips and atEnd transitions.
//
// Skipped on purpose:
//   * exhaustive count = N>1 tests for primitive readers — same code path as
//     count = 1, just a loop.

#include <QtTest/QtTest>

#include "GpBinaryReader.h"

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

class TestGpBinaryReader : public QObject {
    Q_OBJECT
private slots:
    void defaultConstructed_isEmptyAndAtEnd();
    void setData_resetsPointerToZero();
    void readByte_advancesPointerAndReturnsExactBytes();
    void readSignedByte_treatsHighBitAsNegative();
    void readBool_zeroIsFalseAnyOtherValueIsTrue();
    void readShort_decodesLittleEndian();
    void readInt_decodesLittleEndian();
    void readFloat_roundTripsIeee754();
    void readDouble_roundTripsIeee754();
    void readString_truncatesWhenLengthLessThanSize();
    void readByteSizeString_consumesByteThenString();
    void readIntByteSizeString_treatsIntAsByteLengthPlusOne();
    void readIntSizeString_returnsEmptyOnNegativeLength();
    void skip_advancesPointerAndChecksBounds();
    void readPastEnd_throwsRuntimeError();
    void atEnd_flipsAfterConsumingFinalByte();
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::vector<uint8_t> bytes(std::initializer_list<int> il)
{
    std::vector<uint8_t> result;
    result.reserve(il.size());
    for (int b : il) {
        result.push_back(static_cast<uint8_t>(b & 0xFF));
    }
    return result;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void TestGpBinaryReader::defaultConstructed_isEmptyAndAtEnd()
{
    GpBinaryReader r;
    QCOMPARE(r.dataSize(), static_cast<size_t>(0));
    QCOMPARE(r.getPointer(), 0);
    QVERIFY(r.atEnd());
}

void TestGpBinaryReader::setData_resetsPointerToZero()
{
    GpBinaryReader r(bytes({0x01, 0x02, 0x03}));
    r.setPointer(2);
    QCOMPARE(r.getPointer(), 2);

    r.setData(bytes({0xAA, 0xBB}));
    QCOMPARE(r.getPointer(), 0);
    QCOMPARE(r.dataSize(), static_cast<size_t>(2));
    QVERIFY(!r.atEnd());
}

void TestGpBinaryReader::readByte_advancesPointerAndReturnsExactBytes()
{
    GpBinaryReader r(bytes({0xDE, 0xAD, 0xBE, 0xEF}));

    auto first = r.readByte();
    QCOMPARE(first.size(), static_cast<size_t>(1));
    QCOMPARE(first[0], static_cast<uint8_t>(0xDE));
    QCOMPARE(r.getPointer(), 1);

    auto next3 = r.readByte(3);
    QCOMPARE(next3.size(), static_cast<size_t>(3));
    QCOMPARE(next3[0], static_cast<uint8_t>(0xAD));
    QCOMPARE(next3[1], static_cast<uint8_t>(0xBE));
    QCOMPARE(next3[2], static_cast<uint8_t>(0xEF));
    QCOMPARE(r.getPointer(), 4);
    QVERIFY(r.atEnd());
}

void TestGpBinaryReader::readSignedByte_treatsHighBitAsNegative()
{
    // 0x00 → 0, 0x7F → 127, 0x80 → -128, 0xFF → -1
    GpBinaryReader r(bytes({0x00, 0x7F, 0x80, 0xFF}));
    auto v = r.readSignedByte(4);
    QCOMPARE(v.size(), static_cast<size_t>(4));
    QCOMPARE(static_cast<int>(v[0]), 0);
    QCOMPARE(static_cast<int>(v[1]), 127);
    QCOMPARE(static_cast<int>(v[2]), -128);
    QCOMPARE(static_cast<int>(v[3]), -1);
}

void TestGpBinaryReader::readBool_zeroIsFalseAnyOtherValueIsTrue()
{
    // GP uses single-byte bools where any non-zero byte is "true".
    GpBinaryReader r(bytes({0x00, 0x01, 0xFF, 0x42}));
    auto v = r.readBool(4);
    QCOMPARE(v.size(), static_cast<size_t>(4));
    QCOMPARE(v[0], false);
    QCOMPARE(v[1], true);
    QCOMPARE(v[2], true);
    QCOMPARE(v[3], true);
}

void TestGpBinaryReader::readShort_decodesLittleEndian()
{
    // 0x01 0x00 = 1; 0xFF 0x7F = 0x7FFF = 32767; 0x00 0x80 = -32768 (signed)
    GpBinaryReader r(bytes({0x01, 0x00, 0xFF, 0x7F, 0x00, 0x80}));
    auto shorts = r.readShort(3);
    QCOMPARE(shorts.size(), static_cast<size_t>(3));
    QCOMPARE(static_cast<int>(shorts[0]), 1);
    QCOMPARE(static_cast<int>(shorts[1]), 32767);
    QCOMPARE(static_cast<int>(shorts[2]), -32768);
    QCOMPARE(r.getPointer(), 6);
}

void TestGpBinaryReader::readInt_decodesLittleEndian()
{
    // 0x78 0x56 0x34 0x12 → 0x12345678
    GpBinaryReader r(bytes({0x78, 0x56, 0x34, 0x12}));
    auto v = r.readInt();
    QCOMPARE(v.size(), static_cast<size_t>(1));
    QCOMPARE(v[0], static_cast<int32_t>(0x12345678));
    QCOMPARE(r.getPointer(), 4);
}

void TestGpBinaryReader::readFloat_roundTripsIeee754()
{
    // Build the byte representation of 1.0f and -0.5f in little-endian
    // (matches x86 native layout that the reader memcpys directly).
    float in[2] = { 1.0f, -0.5f };
    std::vector<uint8_t> data(sizeof(in));
    std::memcpy(data.data(), in, sizeof(in));

    GpBinaryReader r(data);
    auto out = r.readFloat(2);
    QCOMPARE(out.size(), static_cast<size_t>(2));
    QCOMPARE(out[0], 1.0f);
    QCOMPARE(out[1], -0.5f);
}

void TestGpBinaryReader::readDouble_roundTripsIeee754()
{
    double in[1] = { 3.141592653589793 };
    std::vector<uint8_t> data(sizeof(in));
    std::memcpy(data.data(), in, sizeof(in));

    GpBinaryReader r(data);
    auto out = r.readDouble();
    QCOMPARE(out.size(), static_cast<size_t>(1));
    QCOMPARE(out[0], 3.141592653589793);
    QCOMPARE(r.getPointer(), 8);
}

void TestGpBinaryReader::readString_truncatesWhenLengthLessThanSize()
{
    // GP convention: a fixed-size string slot where the active length may be
    // shorter than the slot. The pointer must advance the FULL slot size,
    // but the returned string is truncated to `length`.
    GpBinaryReader r(bytes({'H', 'e', 'l', 'l', 'o', '!', '!', '!'}));
    std::string s = r.readString(/*size=*/8, /*length=*/5);
    QCOMPARE(s, std::string("Hello"));
    QCOMPARE(r.getPointer(), 8);
}

void TestGpBinaryReader::readByteSizeString_consumesByteThenString()
{
    // [size_byte = N] [N bytes of string]; here size = 5, content = "Hello"
    GpBinaryReader r(bytes({0x05, 'H', 'e', 'l', 'l', 'o'}));
    std::string s = r.readByteSizeString(/*size=*/5);
    QCOMPARE(s, std::string("Hello"));
    QCOMPARE(r.getPointer(), 6);
}

void TestGpBinaryReader::readIntByteSizeString_treatsIntAsByteLengthPlusOne()
{
    // GP3/4 quirk: the int prefix is (byteSize + 1). Body: 0x06 'H' 'i' '!' (size byte =3, ascii "Hi!").
    // int prefix = 4 (= 3 + 1).
    GpBinaryReader r(bytes({0x04, 0x00, 0x00, 0x00, 0x03, 'H', 'i', '!'}));
    std::string s = r.readIntByteSizeString();
    QCOMPARE(s, std::string("Hi!"));

    // Negative int (after the -1) yields empty, no string read attempted.
    GpBinaryReader r2(bytes({0x00, 0x00, 0x00, 0x00})); // int = 0, d = -1
    std::string s2 = r2.readIntByteSizeString();
    QCOMPARE(s2, std::string());
    QCOMPARE(r2.getPointer(), 4);
}

void TestGpBinaryReader::readIntSizeString_returnsEmptyOnNegativeLength()
{
    // int prefix = -1 (0xFFFFFFFF) → empty string, pointer advances only the int.
    GpBinaryReader r(bytes({0xFF, 0xFF, 0xFF, 0xFF}));
    std::string s = r.readIntSizeString();
    QCOMPARE(s, std::string());
    QCOMPARE(r.getPointer(), 4);

    // Happy path: int = 3, "ABC".
    GpBinaryReader r2(bytes({0x03, 0x00, 0x00, 0x00, 'A', 'B', 'C'}));
    QCOMPARE(r2.readIntSizeString(), std::string("ABC"));
    QCOMPARE(r2.getPointer(), 7);
}

void TestGpBinaryReader::skip_advancesPointerAndChecksBounds()
{
    GpBinaryReader r(bytes({0x01, 0x02, 0x03, 0x04, 0x05}));
    r.skip(3);
    QCOMPARE(r.getPointer(), 3);

    // Negative skip is silently ignored (production contract).
    r.skip(-10);
    QCOMPARE(r.getPointer(), 3);

    // Skipping past end throws.
    bool threw = false;
    try {
        r.skip(99);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    QVERIFY2(threw, "skip past end must throw runtime_error");
}

void TestGpBinaryReader::readPastEnd_throwsRuntimeError()
{
    GpBinaryReader r(bytes({0x01, 0x02}));
    bool threw = false;
    try {
        (void)r.readInt(); // needs 4 bytes, only 2 available
    } catch (const std::runtime_error&) {
        threw = true;
    }
    QVERIFY2(threw, "readInt past end must throw runtime_error");
}

void TestGpBinaryReader::atEnd_flipsAfterConsumingFinalByte()
{
    GpBinaryReader r(bytes({0xAB}));
    QVERIFY(!r.atEnd());
    r.readByte();
    QVERIFY(r.atEnd());
}

QTEST_APPLESS_MAIN(TestGpBinaryReader)
#include "test_gp_binary_reader.moc"
