#ifndef DPIENVOVERRIDES_H
#define DPIENVOVERRIDES_H

#include <QByteArray>
#include <QByteArrayList>

/**
 * \brief Process-environment overrides that must not outlive the setting that
 *        made them.
 *
 * main() translates "Ignore system UI scaling", "Use rounded scaling" and
 * "Ignore font scaling" into QT_* environment variables before QApplication
 * exists. Environment variables are inherited by every process this one
 * starts - the editor's own restart after a theme change and the updater's
 * relaunch included. Without bookkeeping the child inherits overrides its own
 * settings no longer ask for (the option was switched off in between: the
 * "off" branch sets nothing, so the inherited "on" stays in force), and a
 * value the user had configured outside the editor is lost the first time
 * the editor overrides it (external review 2026-09-06, SP-04).
 *
 * set() records, per variable, what THIS process inherited - the value, or
 * the fact that it was unset - in a backup variable, and lists the name in a
 * marker variable. restoreInherited(), called by the next instance before it
 * applies its own settings, puts every listed variable back to that
 * inherited state and clears the bookkeeping. External values therefore
 * survive any number of in-app restarts, and an option switched off in the
 * meantime is really off. A fresh start from the shell or Explorer carries no
 * marker and is left alone.
 *
 * Limit: a variable inherited with an EMPTY value comes back unset on Windows
 * (qputenv with an empty value removes the variable there); none of the QT_*
 * scaling variables means anything when empty.
 */
namespace DpiEnvOverrides {

/** ';'-separated list of the variable names this instance overrode. */
inline constexpr char kMarkerVariable[] = "MIDIEDITOR_ENV_OVERRIDES";

/** Prefix of the per-variable backup: "1:<value>" when the variable was
 *  inherited with a value, "0" when it was unset. */
inline constexpr char kBackupPrefix[] = "MIDIEDITOR_ENV_PREV_";

/** Sets \p name to \p value, recording the inherited state on the first
 *  override of that name in this process chain. */
void set(const QByteArray &name, const QByteArray &value);

/** Undoes every override a previous instance recorded: restores the
 *  inherited value or unsets the variable, then clears the bookkeeping.
 *  \return the number of variables handled (0 when nothing was recorded). */
int restoreInherited();

/** The names currently listed in the marker (empty when none). */
QByteArrayList recordedNames();

} // namespace DpiEnvOverrides

#endif // DPIENVOVERRIDES_H
