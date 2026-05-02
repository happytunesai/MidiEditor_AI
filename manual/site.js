/* ============================================================
   MidiEditor AI — Website Global JS (site.js)
   Navigation, scroll effects, carousel, counters, reveal, notes
   ============================================================ */
(function () {
    'use strict';

    // ----------------------------------------------------------
    // 1. Sticky nav — add .scrolled class
    // ----------------------------------------------------------
    var nav = document.querySelector('.site-nav');
    if (nav) {
        window.addEventListener('scroll', function () {
            nav.classList.toggle('scrolled', window.scrollY > 20);
        }, { passive: true });
    }

    // ----------------------------------------------------------
    // 2. Mobile hamburger toggle
    // ----------------------------------------------------------
    var burger = document.querySelector('.nav-hamburger');
    var navLinks = document.querySelector('.nav-links');
    if (burger && navLinks) {
        burger.addEventListener('click', function () {
            navLinks.classList.toggle('open');
            burger.setAttribute('aria-expanded',
                navLinks.classList.contains('open') ? 'true' : 'false');
        });
        // Close on clicking a link
        navLinks.querySelectorAll('a').forEach(function (a) {
            a.addEventListener('click', function () {
                navLinks.classList.remove('open');
                burger.setAttribute('aria-expanded', 'false');
            });
        });
    }

    // ----------------------------------------------------------
    // 3. Intersection Observer — reveal on scroll
    // ----------------------------------------------------------
    var reveals = document.querySelectorAll('.reveal, .feature-card');
    if (reveals.length && 'IntersectionObserver' in window) {
        var revealObs = new IntersectionObserver(function (entries) {
            entries.forEach(function (e) {
                if (e.isIntersecting) {
                    e.target.classList.add('visible');
                    revealObs.unobserve(e.target);
                }
            });
        }, { threshold: 0.12 });
        reveals.forEach(function (el) { revealObs.observe(el); });
    } else {
        // Fallback: show everything
        reveals.forEach(function (el) { el.classList.add('visible'); });
    }

    // ----------------------------------------------------------
    // 4. Animated stat counters
    // ----------------------------------------------------------
    function animateCounter(el) {
        var target = parseInt(el.getAttribute('data-target'), 10);
        var suffix = el.getAttribute('data-suffix') || '';
        var duration = 1600;
        var start = 0;
        var startTime = null;
        function step(ts) {
            if (!startTime) startTime = ts;
            var progress = Math.min((ts - startTime) / duration, 1);
            // ease out quad
            var eased = 1 - (1 - progress) * (1 - progress);
            var current = Math.floor(eased * target);
            el.textContent = current + suffix;
            if (progress < 1) requestAnimationFrame(step);
        }
        requestAnimationFrame(step);
    }
    // Shared parse of changelog.html — used by bugfix counter and What's New card
    var changelogDocReady = (typeof fetch === 'function')
        ? fetch('changelog.html')
            .then(function (r) { return r.ok ? r.text() : Promise.reject(); })
            .then(function (html) {
                return new DOMParser().parseFromString(html, 'text/html');
            })
            .catch(function () { return null; })
        : Promise.resolve(null);

    // Dynamic bugfix count from changelog
    var bugfixReady = (function loadBugfixCount() {
        var el = document.getElementById('stat-bugfixes');
        if (!el) return Promise.resolve();
        return changelogDocReady.then(function (doc) {
            if (!doc) return;
            var timeline = doc.querySelector('.cl-timeline');
            if (!timeline) return;
            var text = timeline.textContent;
            var re = /(\d+)\s+bug\s*fix/gi;
            var total = 0, m;
            while ((m = re.exec(text)) !== null) {
                total += parseInt(m[1], 10);
            }
            if (total > 0) {
                total = Math.ceil(total / 5) * 5;
                el.setAttribute('data-target', total);
            }
        });
    })();

    // Dynamic "What's New" card from changelog (latest version)
    (function populateWhatsNew() {
        var card = document.querySelector('.whats-new');
        if (!card) return;
        changelogDocReady.then(function (doc) {
            if (!doc) return; // keep hardcoded fallback
            var latest = doc.querySelector('.cl-version');
            if (!latest) return;
            var version = latest.getAttribute('data-version') || '';
            var dateText = (latest.querySelector('.cl-date') || {}).textContent || '';
            var titleText = (latest.querySelector('.cl-title') || {}).textContent || '';
            var summaryItems = latest.querySelectorAll('.cl-summary > li');
            if (!version || !summaryItems.length) return;

            // Format YYYY-MM-DD → "Month YYYY"
            var months = ['January','February','March','April','May','June',
                          'July','August','September','October','November','December'];
            var prettyDate = dateText;
            var dm = /^(\d{4})-(\d{2})/.exec(dateText);
            if (dm) prettyDate = months[parseInt(dm[2], 10) - 1] + ' ' + dm[1];

            var heading = 'v' + version + ' \u2014 ' + prettyDate;
            var ul = document.createElement('ul');
            summaryItems.forEach(function (li) {
                var clone = li.cloneNode(true);
                clone.removeAttribute('class');
                clone.removeAttribute('style');
                ul.appendChild(clone);
            });

            // Replace heading + bullet list, keep badge and "View Full Changelog" link
            var existingH3 = card.querySelector('h3');
            if (existingH3) existingH3.textContent = heading;
            var existingUl = card.querySelector('ul');
            if (existingUl) {
                existingUl.replaceWith(ul);
            } else if (existingH3) {
                existingH3.insertAdjacentElement('afterend', ul);
            }

            if (titleText) {
                var sub = card.querySelector('.whats-new-subtitle');
                if (!sub) {
                    sub = document.createElement('p');
                    sub.className = 'whats-new-subtitle';
                    if (existingH3) existingH3.insertAdjacentElement('afterend', sub);
                }
                sub.textContent = titleText;
            }
        });
    })();

    var statEls = document.querySelectorAll('.stat-number[data-target]');
    if (statEls.length && 'IntersectionObserver' in window) {
        var statObs = new IntersectionObserver(function (entries) {
            entries.forEach(function (e) {
                if (e.isIntersecting) {
                    // Wait for bugfix fetch before animating that counter
                    if (e.target.id === 'stat-bugfixes' && bugfixReady) {
                        bugfixReady.then(function () {
                            animateCounter(e.target);
                        });
                    } else {
                        animateCounter(e.target);
                    }
                    statObs.unobserve(e.target);
                }
            });
        }, { threshold: 0.5 });
        statEls.forEach(function (el) { statObs.observe(el); });
    }

    // ----------------------------------------------------------
    // 5. Showcase Carousel
    // ----------------------------------------------------------
    var track = document.querySelector('.showcase-track');
    var slides = document.querySelectorAll('.showcase-slide');
    var dots = document.querySelectorAll('.showcase-dots .dot');
    var prevBtn = document.querySelector('.showcase-btn.prev');
    var nextBtn = document.querySelector('.showcase-btn.next');
    var currentSlide = 0;

    function goToSlide(idx) {
        if (!track || !slides.length) return;
        currentSlide = ((idx % slides.length) + slides.length) % slides.length;
        track.style.transform = 'translateX(-' + (currentSlide * 100) + '%)';
        dots.forEach(function (d, i) { d.classList.toggle('active', i === currentSlide); });
    }
    if (prevBtn) prevBtn.addEventListener('click', function () { goToSlide(currentSlide - 1); });
    if (nextBtn) nextBtn.addEventListener('click', function () { goToSlide(currentSlide + 1); });
    dots.forEach(function (dot, i) {
        dot.addEventListener('click', function () { goToSlide(i); });
    });
    // Auto-advance every 6s
    if (slides.length > 1) {
        setInterval(function () { goToSlide(currentSlide + 1); }, 6000);
    }

    // ----------------------------------------------------------
    // 6. Floating music notes (hero background)
    // ----------------------------------------------------------
    var noteContainer = document.querySelector('.floating-notes');
    if (noteContainer && !window.matchMedia('(prefers-reduced-motion: reduce)').matches) {
        var symbols = ['\u266A', '\u266B', '\u266C', '\u2669', '\u{1D11E}', '\u{1D160}'];
        for (var i = 0; i < 15; i++) {
            var span = document.createElement('span');
            span.className = 'note';
            span.textContent = symbols[i % symbols.length];
            span.style.left = (Math.random() * 100) + '%';
            span.style.fontSize = (1 + Math.random() * 1.5) + 'rem';
            span.style.animationDuration = (8 + Math.random() * 12) + 's';
            span.style.animationDelay = (Math.random() * 10) + 's';
            span.style.opacity = '0';
            noteContainer.appendChild(span);
        }
    }

    // ----------------------------------------------------------
    // 7. Scroll-to-top button
    // ----------------------------------------------------------
    var scrollBtn = document.querySelector('.scroll-top');
    if (scrollBtn) {
        window.addEventListener('scroll', function () {
            scrollBtn.classList.toggle('visible', window.scrollY > 400);
        }, { passive: true });
        scrollBtn.addEventListener('click', function () {
            window.scrollTo({ top: 0, behavior: 'smooth' });
        });
    }

    // ----------------------------------------------------------
    // 8. Smooth anchor scrolling (for same-page #links)
    // ----------------------------------------------------------
    document.querySelectorAll('a[href^="#"]').forEach(function (a) {
        a.addEventListener('click', function (e) {
            var target = document.querySelector(a.getAttribute('href'));
            if (target) {
                e.preventDefault();
                target.scrollIntoView({ behavior: 'smooth' });
            }
        });
    });

    // ----------------------------------------------------------
    // 9. Theme Preview Switcher (with auto-carousel)
    // ----------------------------------------------------------
    var themeTabs = document.querySelectorAll('.theme-tab');
    var previewImg = document.querySelector('.theme-preview-img');
    if (themeTabs.length && previewImg) {
        var themeKeys = ['midieditor_ai', 'dark', 'light', 'classic', 'sakura', 'amoled', 'material_dark'];
        var themeMap = {
            midieditor_ai: 'screenshots/MidiEditor_AI_theme.png',
            dark: 'screenshots/dark_theme.png',
            light: 'screenshots/light_theme.png',
            classic: 'screenshots/classic_theme.png',
            sakura: 'screenshots/sakura_theme.png',
            amoled: 'screenshots/amoled_theme.png',
            material_dark: 'screenshots/material_dark_theme.png'
        };
        var currentThemeIdx = 0;
        var themeTimer = null;

        function switchTheme(idx) {
            idx = ((idx % themeKeys.length) + themeKeys.length) % themeKeys.length;
            currentThemeIdx = idx;
            var theme = themeKeys[idx];
            themeTabs.forEach(function (t) { t.classList.remove('active'); });
            themeTabs[idx].classList.add('active');
            previewImg.classList.add('fade-out');
            setTimeout(function () {
                previewImg.src = themeMap[theme];
                previewImg.alt = themeTabs[idx].textContent + ' Theme Preview';
                previewImg.onload = function () {
                    previewImg.classList.remove('fade-out');
                };
            }, 300);
        }

        function resetThemeTimer() {
            if (themeTimer) clearInterval(themeTimer);
            themeTimer = setInterval(function () {
                switchTheme(currentThemeIdx + 1);
            }, 5000);
        }

        themeTabs.forEach(function (tab, i) {
            tab.addEventListener('click', function () {
                if (tab.classList.contains('active')) return;
                switchTheme(i);
                resetThemeTimer();
            });
        });

        // Start auto-advance
        resetThemeTimer();
    }

})();
