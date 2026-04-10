/* MidiEditor AI Manual — Lightbox for thumbnails */
(function () {
    'use strict';

    // Create overlay elements once
    var overlay = document.createElement('div');
    overlay.className = 'lightbox-overlay';

    var closeBtn = document.createElement('button');
    closeBtn.className = 'lightbox-close';
    closeBtn.setAttribute('aria-label', 'Close');
    closeBtn.textContent = '\u00D7';

    var lbImg = document.createElement('img');
    lbImg.alt = '';

    overlay.appendChild(closeBtn);
    overlay.appendChild(lbImg);
    document.body.appendChild(overlay);

    function closeLightbox() {
        overlay.classList.remove('active');
    }

    function openLightbox(src, alt) {
        lbImg.src = src;
        lbImg.alt = alt || '';
        overlay.classList.add('active');
    }

    // Close on overlay click, close button, or Escape key
    overlay.addEventListener('click', closeLightbox);
    lbImg.addEventListener('click', function (e) { e.stopPropagation(); });
    closeBtn.addEventListener('click', closeLightbox);
    document.addEventListener('keydown', function (e) {
        if (e.key === 'Escape') closeLightbox();
    });

    // Attach to all qualifying images (not tool icons, not tiny)
    var imgs = document.querySelectorAll('img');
    for (var i = 0; i < imgs.length; i++) {
        var img = imgs[i];
        var src = img.getAttribute('src') || '';

        // Skip tool icons and very small images
        if (src.indexOf('tools/') !== -1) continue;
        var w = img.getAttribute('width');
        if (w && parseInt(w, 10) < 30) continue;

        img.classList.add('lightbox-thumb');
        (function (el) {
            el.addEventListener('click', function (e) {
                // Prevent <a> wrapper from navigating away
                e.preventDefault();
                e.stopPropagation();
                openLightbox(el.src, el.alt);
            });
        })(img);
    }

    // Also handle <a class="lightbox-link"> wrappers directly
    var links = document.querySelectorAll('a.lightbox-link');
    for (var j = 0; j < links.length; j++) {
        (function (link) {
            link.addEventListener('click', function (e) {
                e.preventDefault();
            });
        })(links[j]);
    }
})();

/* Scroll-to-top button */
(function () {
    'use strict';
    var btn = document.createElement('button');
    btn.className = 'scroll-top';
    btn.setAttribute('aria-label', 'Scroll to top');
    btn.innerHTML = '&#x2191;';
    document.body.appendChild(btn);

    btn.addEventListener('click', function () {
        window.scrollTo({ top: 0, behavior: 'smooth' });
    });

    window.addEventListener('scroll', function () {
        if (window.scrollY > 400) {
            btn.classList.add('visible');
        } else {
            btn.classList.remove('visible');
        }
    }, { passive: true });
})();
