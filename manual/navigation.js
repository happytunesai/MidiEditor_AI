/* MidiEditor AI Manual — Centralized Navigation
 *
 * Builds a categorised left-side sidebar drawer instead of the older flat
 * horizontal pill bar. The sidebar is collapsible on small screens and
 * is auto-injected into every manual page that loads this script.
 */
(function () {
    'use strict';

    // Categorised page tree. Order inside a group preserves the previous
    // flat-list order so existing muscle memory still finds the entry.
    var docGroups = [
        {
            title: 'Getting Started',
            pages: [
                { href: 'docs-index.html', icon: '📖', name: 'Manual Index' },
                { href: 'setup.html', icon: '⚙️', name: 'Setup & Install' }
            ]
        },
        {
            title: 'Editor',
            pages: [
                { href: 'editor-and-components.html', icon: '🖥️', name: 'Editor & Components' },
                { href: 'editing-midi-files.html', icon: '✏️', name: 'Editing MIDI Files' },
                { href: 'playback.html', icon: '▶️', name: 'Playback' },
                { href: 'export-audio.html', icon: '💾', name: 'Export Audio' },
                { href: 'tempo-conversion.html', icon: '⏱️', name: 'Tempo Conversion' },
                { href: 'menu-tools.html', icon: '🔧', name: 'Tools Menu' },
                { href: 'shortcuts.html', icon: '⌨️', name: 'Keyboard Shortcuts' }
            ]
        },
        {
            title: 'AI & Automation',
            pages: [
                { href: 'midipilot.html', icon: '🤖', name: 'MidiPilot' },
                { href: 'prompt-examples.html', icon: '💬', name: 'Prompt Examples' },
                { href: 'mcp-server.html', icon: '🔗', name: 'MCP Server' }
            ]
        },
        {
            title: 'FFXIV',
            pages: [
                { href: 'ffxiv-channel-fixer.html', icon: '🎮', name: 'FFXIV Channel Fixer' },
                { href: 'ffxiv-voice-limiter.html', icon: '🎚️', name: 'Voice Limiter' },
                { href: 'soundfont.html', icon: '🔊', name: 'SoundFonts & FluidSynth' }
            ]
        },
        {
            title: 'Files & Formats',
            pages: [
                { href: 'supported-files.html', icon: '📂', name: 'Supported Files' },
                { href: 'split-channels.html', icon: '✂️', name: 'Split Channels' },
                { href: 'midi-overview.html', icon: '🎹', name: 'MIDI Overview' },
                { href: 'lyrics.html', icon: '🎤', name: 'Lyric Editor' }
            ]
        },
        {
            title: 'Appearance',
            pages: [
                { href: 'themes.html', icon: '🎨', name: 'Themes & Look' }
            ]
        }
    ];

    function getCurrentPage() {
        return window.location.pathname.split('/').pop() || 'index.html';
    }

    function createDocSidebar() {
        var aside = document.createElement('aside');
        aside.className = 'doc-sidebar';
        aside.setAttribute('role', 'navigation');
        aside.setAttribute('aria-label', 'Manual sections');

        var header = document.createElement('div');
        header.className = 'doc-sidebar-header';
        header.innerHTML = '<span class="doc-sidebar-title">Manual</span>'
            + '<button type="button" class="doc-sidebar-close" aria-label="Close manual navigation">&times;</button>';
        aside.appendChild(header);

        var scroller = document.createElement('nav');
        scroller.className = 'doc-sidebar-scroll';

        var currentPage = getCurrentPage();

        docGroups.forEach(function (group) {
            var section = document.createElement('div');
            section.className = 'doc-sidebar-group';

            var heading = document.createElement('div');
            heading.className = 'doc-sidebar-group-title';
            heading.textContent = group.title;
            section.appendChild(heading);

            var list = document.createElement('ul');
            list.className = 'doc-sidebar-list';

            group.pages.forEach(function (page) {
                var li = document.createElement('li');
                var a = document.createElement('a');
                a.href = page.href;
                a.className = 'doc-sidebar-link';
                a.innerHTML = '<span class="icon" aria-hidden="true">' + page.icon + '</span>'
                    + '<span class="label">' + page.name + '</span>';
                if (currentPage === page.href) {
                    a.classList.add('active');
                    a.setAttribute('aria-current', 'page');
                }
                li.appendChild(a);
                list.appendChild(li);
            });

            section.appendChild(list);
            scroller.appendChild(section);
        });

        aside.appendChild(scroller);
        return aside;
    }

    function createSidebarToggle() {
        var btn = document.createElement('button');
        btn.type = 'button';
        btn.className = 'doc-sidebar-toggle';
        btn.setAttribute('aria-label', 'Open manual navigation');
        btn.setAttribute('aria-expanded', 'false');
        btn.innerHTML = '<span class="doc-sidebar-toggle-icon" aria-hidden="true">&#9776;</span>'
            + '<span class="doc-sidebar-toggle-label">Manual</span>';
        return btn;
    }

    function createBackdrop() {
        var div = document.createElement('div');
        div.className = 'doc-sidebar-backdrop';
        div.setAttribute('aria-hidden', 'true');
        return div;
    }

    function wire(toggle, sidebar, backdrop) {
        function open() {
            sidebar.classList.add('is-open');
            backdrop.classList.add('is-open');
            toggle.setAttribute('aria-expanded', 'true');
            document.body.classList.add('doc-sidebar-locked');
        }
        function close() {
            sidebar.classList.remove('is-open');
            backdrop.classList.remove('is-open');
            toggle.setAttribute('aria-expanded', 'false');
            document.body.classList.remove('doc-sidebar-locked');
        }
        toggle.addEventListener('click', function () {
            if (sidebar.classList.contains('is-open')) close(); else open();
        });
        backdrop.addEventListener('click', close);
        var closeBtn = sidebar.querySelector('.doc-sidebar-close');
        if (closeBtn) closeBtn.addEventListener('click', close);
        document.addEventListener('keydown', function (e) {
            if (e.key === 'Escape' && sidebar.classList.contains('is-open')) close();
        });
        // Close drawer after clicking a link on small screens.
        sidebar.querySelectorAll('.doc-sidebar-link').forEach(function (a) {
            a.addEventListener('click', function () {
                if (window.matchMedia('(max-width: 1199px)').matches) close();
            });
        });
    }

    window.addEventListener('DOMContentLoaded', function () {
        // If an older flat horizontal subnav is still in the DOM (cached page),
        // remove it so we don't render both bars at once.
        var legacy = document.querySelector('.doc-subnav');
        if (legacy) legacy.parentNode.removeChild(legacy);

        if (document.querySelector('.doc-sidebar')) return;

        var sidebar = createDocSidebar();
        var toggle = createSidebarToggle();
        var backdrop = createBackdrop();

        document.body.appendChild(sidebar);
        document.body.appendChild(backdrop);

        var siteNav = document.querySelector('.site-nav');
        if (siteNav) {
            siteNav.parentNode.insertBefore(toggle, siteNav.nextSibling);
        } else {
            document.body.insertBefore(toggle, document.body.firstChild);
        }

        wire(toggle, sidebar, backdrop);
    });
})();
