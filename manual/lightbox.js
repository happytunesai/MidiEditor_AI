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
            el.addEventListener('click', function () {
                openLightbox(el.src, el.alt);
            });
        })(img);
    }
})();
