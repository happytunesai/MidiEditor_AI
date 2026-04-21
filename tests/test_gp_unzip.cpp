/*
 * test_gp_unzip
 *
 * Unit tests for src/converter/GuitarPro/GpUnzip.
 *
 * GpUnzip parses a tiny subset of the ZIP format (local file headers,
 * central directory, EOCD) and supports two compression methods used by
 * GP7/GP8 (.gpx / .gp) containers: STORE (0) and DEFLATE (8).
 *
 * The tests build a ZIP archive in memory by hand (and via zlib for the
 * deflate stream) so no external fixture is required.
 */

#include <QtTest/QtTest>
#include <QObject>
#include <QByteArray>

#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include <zlib.h>

#include "../src/converter/GuitarPro/GpUnzip.h"

namespace {

void appendU16(std::vector<uint8_t> &out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void appendU32(std::vector<uint8_t> &out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void appendBytes(std::vector<uint8_t> &out, const void *data, size_t n) {
    const uint8_t *p = static_cast<const uint8_t *>(data);
    out.insert(out.end(), p, p + n);
}

// Raw DEFLATE (no zlib/gzip wrapper, matches what ZIP stores).
std::vector<uint8_t> deflateRaw(const std::vector<uint8_t> &src) {
    z_stream s;
    std::memset(&s, 0, sizeof(s));
    if (deflateInit2(&s, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK) {
        throw std::runtime_error("deflateInit2 failed");
    }
    s.next_in = const_cast<uint8_t *>(src.data());
    s.avail_in = static_cast<uInt>(src.size());

    std::vector<uint8_t> out(src.size() + 64);
    s.next_out = out.data();
    s.avail_out = static_cast<uInt>(out.size());

    int ret = deflate(&s, Z_FINISH);
    uLong total = s.total_out;
    deflateEnd(&s);
    if (ret != Z_STREAM_END) {
        throw std::runtime_error("deflate did not reach Z_STREAM_END");
    }
    out.resize(total);
    return out;
}

struct EntrySpec {
    std::string name;
    std::vector<uint8_t> payload;
    uint16_t method; // 0 = STORE, 8 = DEFLATE
};

// Build a minimal but valid ZIP archive in memory.
std::vector<uint8_t> buildZip(const std::vector<EntrySpec> &entries) {
    std::vector<uint8_t> archive;
    struct CdInfo {
        uint32_t localHeaderOffset;
        uint32_t crc;
        uint32_t cSize;
        uint32_t uSize;
        uint16_t method;
        std::string name;
    };
    std::vector<CdInfo> cdInfos;

    for (const auto &e : entries) {
        const uint32_t crc = crc32(0L, e.payload.data(),
                                   static_cast<uInt>(e.payload.size()));
        std::vector<uint8_t> stored;
        if (e.method == 8) {
            stored = deflateRaw(e.payload);
        } else if (e.method == 0) {
            stored = e.payload;
        } else {
            // Allow callers to inject unsupported methods to trigger errors.
            stored = e.payload;
        }

        CdInfo info{};
        info.localHeaderOffset = static_cast<uint32_t>(archive.size());
        info.crc = crc;
        info.cSize = static_cast<uint32_t>(stored.size());
        info.uSize = static_cast<uint32_t>(e.payload.size());
        info.method = e.method;
        info.name = e.name;

        // Local file header.
        appendU32(archive, 0x04034B50); // PK\x03\x04
        appendU16(archive, 20);          // version needed
        appendU16(archive, 0);           // flags
        appendU16(archive, e.method);    // compression method
        appendU16(archive, 0);           // mod time
        appendU16(archive, 0);           // mod date
        appendU32(archive, crc);
        appendU32(archive, info.cSize);
        appendU32(archive, info.uSize);
        appendU16(archive, static_cast<uint16_t>(e.name.size()));
        appendU16(archive, 0); // extra length
        appendBytes(archive, e.name.data(), e.name.size());
        appendBytes(archive, stored.data(), stored.size());

        cdInfos.push_back(info);
    }

    const uint32_t cdOffset = static_cast<uint32_t>(archive.size());

    for (const auto &info : cdInfos) {
        appendU32(archive, 0x02014B50); // PK\x01\x02
        appendU16(archive, 20);          // version made by
        appendU16(archive, 20);          // version needed
        appendU16(archive, 0);           // flags
        appendU16(archive, info.method); // method
        appendU16(archive, 0);           // mod time
        appendU16(archive, 0);           // mod date
        appendU32(archive, info.crc);
        appendU32(archive, info.cSize);
        appendU32(archive, info.uSize);
        appendU16(archive, static_cast<uint16_t>(info.name.size()));
        appendU16(archive, 0); // extra
        appendU16(archive, 0); // comment
        appendU16(archive, 0); // disk number
        appendU16(archive, 0); // internal attrs
        appendU32(archive, 0); // external attrs
        appendU32(archive, info.localHeaderOffset);
        appendBytes(archive, info.name.data(), info.name.size());
    }

    const uint32_t cdSize = static_cast<uint32_t>(archive.size()) - cdOffset;

    // EOCD.
    appendU32(archive, 0x06054B50); // PK\x05\x06
    appendU16(archive, 0);           // disk
    appendU16(archive, 0);           // CD start disk
    appendU16(archive, static_cast<uint16_t>(cdInfos.size())); // entries on disk
    appendU16(archive, static_cast<uint16_t>(cdInfos.size())); // total entries
    appendU32(archive, cdSize);
    appendU32(archive, cdOffset);
    appendU16(archive, 0); // comment length

    return archive;
}

QByteArray toQBA(const std::vector<uint8_t> &v) {
    return QByteArray(reinterpret_cast<const char *>(v.data()),
                      static_cast<int>(v.size()));
}

} // namespace

class TestGpUnzip : public QObject {
    Q_OBJECT

private slots:

    void extract_storedEntry_returnsExactBytes() {
        const std::vector<uint8_t> payload{'h','e','l','l','o',' ','w','o','r','l','d'};
        auto zip = buildZip({{"hello.txt", payload, 0}});

        GpUnzip u(zip);
        QVERIFY(u.hasEntry("hello.txt"));
        auto out = u.extract("hello.txt");
        QCOMPARE(toQBA(out), toQBA(payload));
    }

    void extract_deflatedEntry_returnsOriginalBytes() {
        // Build a payload long and repetitive enough that deflate actually
        // shrinks it (so we know the deflate path is exercised, not just
        // a raw copy that happens to round-trip).
        std::vector<uint8_t> payload;
        const std::string seed = "The quick brown fox jumps over the lazy dog. ";
        for (int i = 0; i < 50; ++i) {
            payload.insert(payload.end(), seed.begin(), seed.end());
        }
        auto zip = buildZip({{"big.txt", payload, 8}});

        GpUnzip u(zip);
        QVERIFY(u.hasEntry("big.txt"));
        auto out = u.extract("big.txt");
        QCOMPARE(out.size(), payload.size());
        QCOMPARE(toQBA(out), toQBA(payload));
    }

    void multipleEntries_listedAndIndependentlyExtracted() {
        const std::vector<uint8_t> a{'A','A','A'};
        const std::vector<uint8_t> b{'B','B','B','B'};
        const std::vector<uint8_t> c{'h','i'};
        auto zip = buildZip({
            {"a.bin", a, 0},
            {"b.bin", b, 8},
            {"sub/c.bin", c, 0},
        });

        GpUnzip u(zip);
        QVERIFY(u.hasEntry("a.bin"));
        QVERIFY(u.hasEntry("b.bin"));
        QVERIFY(u.hasEntry("sub/c.bin"));
        QVERIFY(!u.hasEntry("missing"));

        QCOMPARE(toQBA(u.extract("a.bin")), toQBA(a));
        QCOMPARE(toQBA(u.extract("b.bin")), toQBA(b));
        QCOMPARE(toQBA(u.extract("sub/c.bin")), toQBA(c));
    }

    void emptyBuffer_constructsAndExposesNoEntries() {
        std::vector<uint8_t> empty;
        GpUnzip u(empty);
        QVERIFY(!u.hasEntry("anything"));

        bool threw = false;
        try {
            (void)u.extract("anything");
        } catch (const std::runtime_error &) {
            threw = true;
        }
        QVERIFY2(threw, "extract on missing entry must throw");
    }

    void garbageBuffer_withoutEocd_exposesNoEntries() {
        // 64 bytes of non-ZIP bytes — no EOCD signature anywhere.
        std::vector<uint8_t> junk(64, 0xAA);
        GpUnzip u(junk);
        QVERIFY(!u.hasEntry("foo"));
    }

    void extract_unknownEntry_throwsRuntimeError() {
        const std::vector<uint8_t> payload{'x'};
        auto zip = buildZip({{"present.bin", payload, 0}});
        GpUnzip u(zip);

        bool threw = false;
        try {
            (void)u.extract("absent.bin");
        } catch (const std::runtime_error &) {
            threw = true;
        }
        QVERIFY(threw);
    }

    void extract_unsupportedCompressionMethod_throwsRuntimeError() {
        // Method 99 = AE-x / unsupported. buildZip stores the payload
        // verbatim (since we only special-case 0 and 8) but flags it as
        // method 99 in both headers, which is what GpUnzip checks.
        const std::vector<uint8_t> payload{'z','z','z'};
        auto zip = buildZip({{"x.bin", payload, 99}});
        GpUnzip u(zip);
        QVERIFY(u.hasEntry("x.bin"));

        bool threw = false;
        try {
            (void)u.extract("x.bin");
        } catch (const std::runtime_error &) {
            threw = true;
        }
        QVERIFY2(threw, "Unsupported compression method must be rejected");
    }
};

QTEST_APPLESS_MAIN(TestGpUnzip)
#include "test_gp_unzip.moc"
