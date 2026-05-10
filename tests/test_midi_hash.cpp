// Unit tests for MidiHash — SHA-256 of a file on disk, used as the commit
// id in the collaboration history (Plan §6.2).
//
// Pure QCryptographicHash + QFile — no MidiFile dep. Testable in isolation.
//
// Run from a build configured with -DBUILD_TESTING=ON:
//     ctest --test-dir build -V -R MidiHash

#include "MidiHash.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QString>
#include <QTemporaryDir>
#include <QtTest/QtTest>

class TestMidiHash : public QObject {
    Q_OBJECT

private slots:
    void empty_fileHashesToKnownConstant();
    void abc_hashesToKnownConstant();
    void roundTrip_matchesQCryptographicHash();
    void largeFile_streamsCorrectly();
    void missing_returnsEmptyString();
    void deterministic_sameContentSameHash();
    void differentContent_differentHash();

private:
    static QString writeTemp(const QTemporaryDir &dir,
                             const QString &name,
                             const QByteArray &payload) {
        QString path = dir.path() + QChar('/') + name;
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write(payload);
        f.close();
        return path;
    }
};

// SHA-256 of an empty file — well-known constant.
void TestMidiHash::empty_fileHashesToKnownConstant() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString path = writeTemp(dir, QStringLiteral("empty.mid"), QByteArray());
    QCOMPARE(MidiHash::sha256OfFile(path),
             QStringLiteral("e3b0c44298fc1c149afbf4c8996fb924"
                            "27ae41e4649b934ca495991b7852b855"));
}

// SHA-256 of "abc" — well-known constant from the FIPS 180-2 spec.
void TestMidiHash::abc_hashesToKnownConstant() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString path = writeTemp(dir, QStringLiteral("abc.bin"),
                             QByteArrayLiteral("abc"));
    QCOMPARE(MidiHash::sha256OfFile(path),
             QStringLiteral("ba7816bf8f01cfea414140de5dae2223"
                            "b00361a396177a9cb410ff61f20015ad"));
}

// Cross-check against an in-memory QCryptographicHash on the same payload —
// guards against accidental encoding / chunking bugs in MidiHash.
void TestMidiHash::roundTrip_matchesQCryptographicHash() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QByteArray payload = QByteArrayLiteral(
            "MThd\x00\x00\x00\x06\x00\x00\x00\x01\x01\xe0"
            "MTrk\x00\x00\x00\x10");
    QString path = writeTemp(dir, QStringLiteral("synthetic.mid"), payload);

    QString got = MidiHash::sha256OfFile(path);
    QString expected = QString::fromLatin1(
            QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
    QCOMPARE(got, expected);
}

// Files larger than the streaming chunk threshold must still hash correctly.
// MidiHash::sha256OfFile uses QCryptographicHash::addData(QIODevice*) which
// streams; this test verifies it doesn't truncate at a buffer boundary.
void TestMidiHash::largeFile_streamsCorrectly() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QByteArray payload;
    payload.reserve(5 * 1024 * 1024);
    for (int i = 0; i < 5 * 1024 * 1024; ++i) {
        payload.append(static_cast<char>(i & 0xFF));
    }
    QString path = writeTemp(dir, QStringLiteral("large.bin"), payload);

    QString got = MidiHash::sha256OfFile(path);
    QString expected = QString::fromLatin1(
            QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
    QCOMPARE(got, expected);
}

// Missing path must return empty string, not crash and not throw.
void TestMidiHash::missing_returnsEmptyString() {
    QString got = MidiHash::sha256OfFile(
            QStringLiteral("Z:/this/path/does/not/exist.mid"));
    QCOMPARE(got, QString());
}

// Two files with identical content hash to the same value.
void TestMidiHash::deterministic_sameContentSameHash() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QByteArray payload = QByteArrayLiteral("identical content here");
    QString a = writeTemp(dir, QStringLiteral("a.mid"), payload);
    QString b = writeTemp(dir, QStringLiteral("b.mid"), payload);
    QString hashA = MidiHash::sha256OfFile(a);
    QString hashB = MidiHash::sha256OfFile(b);
    QCOMPARE(hashA, hashB);
    QVERIFY(!hashA.isEmpty());
}

// One-byte difference must produce a different hash (avalanche).
void TestMidiHash::differentContent_differentHash() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString a = writeTemp(dir, QStringLiteral("a.mid"),
                          QByteArrayLiteral("payload"));
    QString b = writeTemp(dir, QStringLiteral("b.mid"),
                          QByteArrayLiteral("paylozd"));
    QVERIFY(MidiHash::sha256OfFile(a) != MidiHash::sha256OfFile(b));
}

QTEST_APPLESS_MAIN(TestMidiHash)
#include "test_midi_hash.moc"
