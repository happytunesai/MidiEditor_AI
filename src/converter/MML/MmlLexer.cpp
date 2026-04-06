#include "MmlLexer.h"

QList<MmlToken> MmlLexer::tokenize(const QString& mml) {
    QList<MmlToken> tokens;
    int i = 0;
    int len = mml.length();

    while (i < len) {
        QChar ch = mml[i].toLower();

        // Skip whitespace
        if (ch.isSpace()) {
            i++;
            continue;
        }

        // Skip line comments  // ...
        if (ch == '/' && i + 1 < len && mml[i + 1] == '/') {
            while (i < len && mml[i] != '\n')
                i++;
            continue;
        }

        // Skip block comments  /* ... */
        if (ch == '/' && i + 1 < len && mml[i + 1] == '*') {
            i += 2;
            while (i + 1 < len && !(mml[i] == '*' && mml[i + 1] == '/'))
                i++;
            if (i + 1 < len)
                i += 2;
            continue;
        }

        // Note letters
        if (ch >= 'a' && ch <= 'g') {
            tokens.append({MmlTokenType::NoteLetter, QString(ch)});
            i++;
            continue;
        }

        // Rest
        if (ch == 'r') {
            tokens.append({MmlTokenType::Rest, QStringLiteral("r")});
            i++;
            continue;
        }

        // Commands that consume a following number: o, t, l, v
        if (ch == 'o') { tokens.append({MmlTokenType::OctaveCmd, QStringLiteral("o")}); i++; continue; }
        if (ch == 't') { tokens.append({MmlTokenType::TempoCmd,  QStringLiteral("t")}); i++; continue; }
        if (ch == 'l') { tokens.append({MmlTokenType::LengthCmd, QStringLiteral("l")}); i++; continue; }
        if (ch == 'v') { tokens.append({MmlTokenType::VolumeCmd, QStringLiteral("v")}); i++; continue; }

        // Instrument
        if (ch == '@') { tokens.append({MmlTokenType::InstrumentCmd, QStringLiteral("@")}); i++; continue; }

        // Sharp / flat
        if (ch == '+' || ch == '#') { tokens.append({MmlTokenType::Sharp, QStringLiteral("+")}); i++; continue; }
        if (ch == '-')              { tokens.append({MmlTokenType::Flat,  QStringLiteral("-")}); i++; continue; }

        // Structural tokens
        if (ch == '.') { tokens.append({MmlTokenType::Dot,        QStringLiteral(".")}); i++; continue; }
        if (ch == '&') { tokens.append({MmlTokenType::Tie,        QStringLiteral("&")}); i++; continue; }
        if (ch == '>') { tokens.append({MmlTokenType::OctaveUp,   QStringLiteral(">")}); i++; continue; }
        if (ch == '<') { tokens.append({MmlTokenType::OctaveDown, QStringLiteral("<")}); i++; continue; }
        if (ch == '[') { tokens.append({MmlTokenType::LoopStart,  QStringLiteral("[")}); i++; continue; }
        if (ch == ']') { tokens.append({MmlTokenType::LoopEnd,    QStringLiteral("]")}); i++; continue; }

        // Number sequences
        if (ch.isDigit()) {
            QString num;
            while (i < len && mml[i].isDigit()) {
                num += mml[i];
                i++;
            }
            tokens.append({MmlTokenType::Number, num});
            continue;
        }

        // Skip any other character
        i++;
    }

    tokens.append({MmlTokenType::EndOfTrack, QString()});
    return tokens;
}
