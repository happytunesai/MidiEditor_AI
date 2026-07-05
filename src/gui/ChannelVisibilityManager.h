#ifndef CHANNELVISIBILITYMANAGER_H
#define CHANNELVISIBILITYMANAGER_H

#include <QHash>

class MidiFile;

/**
 * \brief Channel visibility manager (per-document since Phase 28)
 *
 * This class provides corruption-proof channel visibility management
 * that doesn't depend on potentially corrupted MidiChannel objects.
 *
 * Phase 28 (multi-document tabs): visibility is tracked per MidiFile so each
 * tab keeps its own channel show/hide state. is/setChannelVisible and
 * resetAllVisible operate on the *active* document's state (set via
 * setActiveFile). When no document is active (nullptr) a shared default state
 * is used, which is also what the headless unit tests exercise.
 */
class ChannelVisibilityManager {
public:
    /**
     * \brief Gets the singleton instance
     * \return Reference to the global visibility manager
     */
    static ChannelVisibilityManager &instance();

    /**
     * \brief Phase 28: select which document's visibility state is active.
     *        A new document defaults to "all channels visible"; switching back
     *        to a document restores its state. nullptr selects the default
     *        state. The state is created lazily on first access.
     */
    void setActiveFile(MidiFile *file);

    /**
     * \brief Phase 28: drop a closed document's visibility state.
     */
    void forgetFile(MidiFile *file);

    /**
     * \brief Checks if a channel is visible
     * \param channel The channel number (0-18)
     * \return True if the channel is visible, false otherwise
     */
    bool isChannelVisible(int channel);

    /**
     * \brief File-scoped query (split view): resolves against the GIVEN
     *        document's state instead of the globally-active one, so a
     *        non-active editor group renders with its own visibility.
     *        nullptr falls back to the active-document behavior.
     */
    bool isChannelVisible(int channel, MidiFile *file);

    /**
     * \brief Sets the visibility of a channel
     * \param channel The channel number (0-18)
     * \param visible True to make the channel visible, false to hide it
     */
    void setChannelVisible(int channel, bool visible);

    /** \brief File-scoped setter counterpart of the query overload. */
    void setChannelVisible(int channel, bool visible, MidiFile *file);

    /**
     * \brief Resets all channels to visible
     */
    void resetAllVisible();

private:
    ChannelVisibilityManager();

    /** \brief 19-channel visibility state for one document (or the default). */
    struct State {
        bool v[19];
    };

    /** \brief Returns the active document's state array, creating it lazily.
     *  Looked up fresh on every call so no pointer is held across a possible
     *  rehash. */
    bool *activeArray();

    /** \brief Returns the GIVEN document's state array (lazily created);
     *  nullptr resolves like activeArray(). */
    bool *arrayFor(MidiFile *file);

    /** \brief Default state used when no document is active (nullptr). */
    bool channelVisibility[19];

    /** \brief Per-document visibility states (Phase 28). */
    QHash<MidiFile *, State> _perFile;

    /** \brief The document whose state is currently active (nullptr = default). */
    MidiFile *_activeFile = nullptr;
};

#endif // CHANNELVISIBILITYMANAGER_H
