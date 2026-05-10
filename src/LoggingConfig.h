/*
 * MidiEditor AI
 *
 * LoggingConfig — single entry point for Qt logging-category configuration
 * (Plan §11.10i, Phase 9.6f). Translates a coarse user-facing level
 * dropdown plus an optional advanced filter-rules string into the format
 * `QLoggingCategory::setFilterRules` expects, and persists/reloads the
 * choice via QSettings.
 *
 * Used by main.cpp at startup and by `CollabSettingsWidget` when the
 * user changes the level or the per-category overrides at runtime.
 */

#ifndef LOGGINGCONFIG_H
#define LOGGINGCONFIG_H

#include <QString>

class LoggingConfig {
public:
    /** \brief Coarse log level shown to the user. Maps to a set of Qt
     *  filter rules covering all built-in categories at once. */
    enum class Level {
        Off = 0,        ///< Suppress every category at every severity
        Errors = 1,     ///< Only critical / fatal
        Warnings = 2,   ///< + warnings (the Qt default)
        Info = 3,       ///< + info (qCInfo)
        Debug = 4,      ///< + debug (qCDebug, very verbose)
    };

    /** \brief Build a filter-rules string for \a level merged with the
     *  user's optional per-category overrides (which take precedence —
     *  matches Qt's own merge semantics). The output is suitable for
     *  passing to `QLoggingCategory::setFilterRules`. */
    static QString buildFilterRules(Level level, const QString &perCategoryOverrides);

    /** \brief Read `Logging/level` + `Logging/perCategory` from the
     *  app-wide QSettings and apply them. Idempotent. */
    static void applyFromSettings();

    /** \brief Persist the supplied configuration and apply it
     *  immediately. Called from the settings widget on accept(). */
    static void applyAndPersist(Level level, const QString &perCategoryOverrides);

    /** \brief Convenience: read just the level, defaulting to Warnings. */
    static Level loadLevel();
    /** \brief Convenience: read the per-category override string. */
    static QString loadPerCategory();

    // ---- Collab-only verbose flag (Plan §11.10i extension) ---------
    //
    // The general Logging settings tab covers everything; the
    // Collaboration tab also exposes a focused "verbose collab logs"
    // checkbox so users debugging a session don't have to learn the
    // Qt logging-rules syntax. When enabled, this appends
    // `midieditor.collab.*=true` to the effective filter rules — it
    // RE-ENABLES every severity for the collab subsystem, layered on
    // top of the user's chosen global level / per-category overrides.

    /** \brief Read `Collab/verboseLogging`, defaulting to false. */
    static bool loadCollabVerbose();
    /** \brief Persist the collab-verbose flag and re-apply rules. */
    static void setCollabVerbose(bool on);
};

#endif // LOGGINGCONFIG_H
