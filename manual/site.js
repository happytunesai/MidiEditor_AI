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
    var statEls = document.querySelectorAll('.stat-number[data-target]');
    if (statEls.length && 'IntersectionObserver' in window) {
        var statObs = new IntersectionObserver(function (entries) {
            entries.forEach(function (e) {
                if (e.isIntersecting) {
                    animateCounter(e.target);
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

})();
