/* MidiEditor AI Manual — Centralized Navigation */
(function () {
    'use strict';

    // Define all doc pages and their display names
    var docPages = [
        { href: 'docs-index.html', icon: '📖', name: 'Index' },
        { href: 'midipilot.html', icon: '🤖', name: 'MidiPilot' },
        { href: 'prompt-examples.html', icon: '💬', name: 'Prompts' },
        { href: 'ffxiv-channel-fixer.html', icon: '🎮', name: 'Fix XIV' },
        { href: 'ffxiv-voice-limiter.html', icon: '🎚️', name: 'Voices' },
        { href: 'tempo-conversion.html', icon: '⏱️', name: 'Tempo' },
        { href: 'split-channels.html', icon: '✂️', name: 'Split' },
        { href: 'themes.html', icon: '🎨', name: 'Themes' },
        { href: 'soundfont.html', icon: '🔊', name: 'SoundFonts' },
        { href: 'supported-files.html', icon: '📂', name: 'Files' },
        { href: 'lyrics.html', icon: '🎤', name: 'Lyrics' },
        { href: 'mcp-server.html', icon: '🔗', name: 'MCP' },
        { href: 'midi-overview.html', icon: '🎹', name: 'MIDI' },
        { href: 'editor-and-components.html', icon: '🖥️', name: 'Editor' },
        { href: 'setup.html', icon: '⚙️', name: 'Setup' },
        { href: 'editing-midi-files.html', icon: '✏️', name: 'Editing' },
        { href: 'playback.html', icon: '▶️', name: 'Playback' },
        { href: 'export-audio.html', icon: '💾', name: 'Export' },
        { href: 'menu-tools.html', icon: '🔧', name: 'Tools' }
    ];

    // Create doc-subnav HTML
    function createDocSubnav() {
        var nav = document.createElement('div');
        nav.className = 'doc-subnav';
        nav.setAttribute('role', 'navigation');
        nav.setAttribute('aria-label', 'Manual sections');

        var currentPage = window.location.pathname.split('/').pop() || 'index.html';

        docPages.forEach(function (page) {
            var link = document.createElement('a');
            link.href = page.href;
            link.innerHTML = '<span class="icon">' + page.icon + '</span>' + page.name;

            // Mark as active if this is the current page
            if (currentPage === page.href) {
                link.classList.add('active');
            }

            nav.appendChild(link);
        });

        return nav;
    }

    // Insert doc-subnav after the main site-nav (if it doesn't already exist)
    window.addEventListener('DOMContentLoaded', function () {
        // Don't insert if already present
        if (document.querySelector('.doc-subnav')) {
            return;
        }

        var siteNav = document.querySelector('.site-nav');
        if (siteNav) {
            var docSubnav = createDocSubnav();
            siteNav.parentNode.insertBefore(docSubnav, siteNav.nextSibling);
        }
    });
})();
